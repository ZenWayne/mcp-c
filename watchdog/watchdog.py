"""Child-process watchdog.

Monitors a parent process by name and its single child process. When the child
terminates while the parent is still alive, the parent is restarted (graceful
SIGTERM with SIGKILL fallback) and re-launched using the context captured at
startup (cmdline, cwd, environ, exe). A sliding-window crash-loop detector with
exponential backoff aborts after too many restarts within a short period.

Usage:
    uv run watchdog.py --parent <parent-name> [--poll-interval 2] ...

Requires: psutil (declared in pyproject.toml).
"""

from __future__ import annotations

import argparse
import logging
import os
import signal
import subprocess
import sys
import time
from collections import deque
from dataclasses import dataclass

import psutil

log = logging.getLogger("watchdog")


class WatchdogError(Exception):
    """Recoverable or fatal watchdog error."""


@dataclass
class ParentContext:
    """Snapshot of how the parent was launched, used to respawn it faithfully."""

    pid: int
    name: str
    cmdline: list[str]
    cwd: str
    environ: dict[str, str]
    exe: str | None
    uid: int


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    p = argparse.ArgumentParser(
        prog="watchdog",
        description=(
            "Monitor a parent process and restart it when its child dies. "
            "Assumes the parent has exactly one child."
        ),
    )
    p.add_argument("--parent", required=True, help="Parent process name to locate.")
    p.add_argument("--poll-interval", type=float, default=2.0,
                   help="Seconds between status checks (default: 2).")
    p.add_argument("--max-restarts", type=int, default=5,
                   help="Max restarts within --restart-window before aborting "
                        "(default: 5; 0 = unlimited, never abort on crash loop).")
    p.add_argument("--restart-window", type=float, default=60.0,
                   help="Sliding window in seconds for crash-loop detection (default: 60).")
    p.add_argument("--base-cooldown", type=float, default=1.0,
                   help="Base cooldown seconds between restarts; doubled each attempt (default: 1).")
    p.add_argument("--max-cooldown", type=float, default=30.0,
                   help="Cap on per-restart cooldown seconds (default: 30).")
    p.add_argument("--grace-timeout", type=float, default=10.0,
                   help="Seconds to wait for SIGTERM before SIGKILL (default: 10).")
    p.add_argument("--recheck-window", type=float, default=1.0,
                   help="Debounce seconds: re-check child before killing parent (default: 1).")
    p.add_argument("--kill-on-exit", action="store_true",
                   help="When the watchdog shuts down (SIGINT/SIGTERM), also "
                        "terminate the watched parent and its children. Use "
                        "this when the watchdog owns the parent process; "
                        "leave unset when the parent is managed elsewhere.")
    p.add_argument("--log-level", default="INFO",
                   choices=["DEBUG", "INFO", "WARNING", "ERROR"],
                   help="Log verbosity (default: INFO).")
    return p.parse_args(argv)


def _name_matches(proc: psutil.Process, name: str) -> bool:
    """Compare a user-supplied name against a process.

    Linux /proc/<pid>/comm is truncated to 15 chars, so a name like
    "fragile_server.sh" is reported as "fragile_server." by proc.name().
    Treat a match if either:
      - the supplied name equals proc.name() exactly, OR
      - proc.name() is a 15-char prefix of the supplied name (truncation), OR
      - the basename of the process's executable (first cmdline element)
        equals the supplied name.
    """
    try:
        comm = proc.info.get("name")
    except (psutil.NoSuchProcess, psutil.AccessDenied):
        return False
    if comm == name:
        return True
    if comm and len(comm) == 15 and name.startswith(comm):
        return True
    try:
        cmdline = proc.cmdline()
    except (psutil.NoSuchProcess, psutil.AccessDenied):
        return False
    if not cmdline:
        return False
    return os.path.basename(cmdline[0]) == name


def find_parent(name: str) -> psutil.Process:
    """Locate a running process by name. Raises WatchdogError on zero/multiple matches.

    The name is matched against both the process's comm name (which Linux
    truncates to 15 chars) and the basename of each cmdline element.
    """
    matches = []
    for proc in psutil.process_iter(["pid", "name"]):
        try:
            if _name_matches(proc, name):
                matches.append(proc)
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            continue
    if not matches:
        raise WatchdogError(f"no process named {name!r} found")
    if len(matches) > 1:
        pids = ", ".join(str(p.pid) for p in matches)
        raise WatchdogError(f"multiple processes named {name!r} (pids: {pids})")
    return matches[0]


def capture_context(proc: psutil.Process) -> ParentContext:
    """Capture how the parent was launched so we can respawn it faithfully.

    environ may be AccessDenied under some configurations; fall back to the
    watchdog's own environment with a warning rather than failing hard.
    """
    try:
        environ = proc.environ()
    except psutil.AccessDenied:
        log.warning("cannot read environ for pid %s; falling back to own env", proc.pid)
        environ = dict(os.environ)
    except psutil.NoSuchProcess:
        raise WatchdogError(f"parent pid {proc.pid} vanished during context capture")

    try:
        cwd = proc.cwd()
    except psutil.AccessDenied:
        cwd = os.getcwd()
        log.warning("cannot read cwd for pid %s; falling back to %s", proc.pid, cwd)
    except psutil.NoSuchProcess:
        raise WatchdogError(f"parent pid {proc.pid} vanished during context capture")

    try:
        exe = proc.exe()
    except (psutil.AccessDenied, psutil.NoSuchProcess):
        exe = None

    try:
        uid = proc.uids().real
    except (psutil.AccessDenied, psutil.NoSuchProcess, AttributeError):
        uid = os.getuid()

    return ParentContext(
        pid=proc.pid,
        name=proc.name(),
        cmdline=list(proc.cmdline()),
        cwd=cwd,
        environ=environ,
        exe=exe,
        uid=uid,
    )


def find_child(parent: psutil.Process) -> psutil.Process:
    """Return the parent's single child. Raises WatchdogError if count != 1."""
    children = parent.children(recursive=False)
    if not children:
        raise WatchdogError(f"parent pid {parent.pid} has no children")
    if len(children) > 1:
        pids = ", ".join(str(c.pid) for c in children)
        raise WatchdogError(f"parent pid {parent.pid} has multiple children (pids: {pids})")
    return children[0]


def is_dead(proc: psutil.Process | None) -> bool:
    """True if the process no longer exists or is a zombie."""
    if proc is None:
        return True
    try:
        if not proc.is_running():
            return True
        return proc.status() == psutil.STATUS_ZOMBIE
    except psutil.NoSuchProcess:
        return True
    except psutil.AccessDenied:
        # Can't inspect status; treat as alive to avoid false positives.
        return False


def restart_parent(ctx: ParentContext, grace_timeout: float) -> psutil.Process:
    """Terminate the current parent (if alive) and respawn it from captured context.

    Returns the new psutil.Process for the respawned parent.
    Raises WatchdogError if respawn fails or the new process can't be located.
    """
    # Best-effort termination of the existing parent.
    try:
        old = psutil.Process(ctx.pid)
        log.info("terminating parent pid %s (SIGTERM)", ctx.pid)
        old.terminate()
        try:
            old.wait(timeout=grace_timeout)
        except psutil.TimeoutExpired:
            log.warning("parent pid %s did not exit after %.1fs; sending SIGKILL",
                        ctx.pid, grace_timeout)
            old.kill()
            try:
                old.wait(timeout=5.0)
            except psutil.TimeoutExpired:
                raise WatchdogError(f"parent pid {ctx.pid} refused SIGKILL")
    except psutil.NoSuchProcess:
        log.info("parent pid %s already gone; skipping terminate", ctx.pid)

    if not ctx.cmdline or not ctx.cmdline[0]:
        raise WatchdogError(f"cannot respawn: captured cmdline is empty for pid {ctx.pid}")

    log.info("respawning parent: cwd=%s cmd=%s", ctx.cwd, " ".join(ctx.cmdline))
    try:
        popen = subprocess.Popen(
            ctx.cmdline,
            cwd=ctx.cwd,
            env=ctx.environ,
            executable=ctx.exe,
            start_new_session=True,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            close_fds=True,
        )
    except OSError as exc:
        raise WatchdogError(f"failed to respawn parent: {exc}") from exc

    # Give the OS a moment, then locate the new psutil.Process.
    time.sleep(0.2)
    try:
        new_proc = psutil.Process(popen.pid)
    except psutil.NoSuchProcess:
        raise WatchdogError("respawned parent exited immediately")

    # Store popen handle on the proc to keep it from being GC'd too early
    # (matters on Windows; harmless on Linux).
    new_proc._watchdog_popen = popen
    return new_proc


def shutdown_parent(ctx: ParentContext, grace_timeout: float) -> None:
    """Terminate the watched parent (and descendants) on watchdog shutdown.

    Sends SIGTERM, waits up to `grace_timeout`, then SIGKILL if still alive.
    Descendants are reaped via `proc.terminate()` recursion so we don't rely
    on process-group membership (the parent was started with
    `start_new_session=True` after a restart, but the *original* parent
    observed at startup may not be in our session).
    """
    try:
        proc = psutil.Process(ctx.pid)
    except psutil.NoSuchProcess:
        log.info("shutdown: parent pid %s already gone", ctx.pid)
        return

    # Reap children first so they don't get orphaned mid-teardown.
    for child in proc.children(recursive=True):
        try:
            child.terminate()
        except psutil.NoSuchProcess:
            pass

    log.info("shutdown: terminating parent pid %s (SIGTERM)", ctx.pid)
    proc.terminate()
    try:
        proc.wait(timeout=grace_timeout)
        return
    except psutil.TimeoutExpired:
        pass

    log.warning("shutdown: parent pid %s did not exit after %.1fs; SIGKILL",
                ctx.pid, grace_timeout)
    try:
        proc.kill()
        proc.wait(timeout=5.0)
    except psutil.NoSuchProcess:
        pass
    except psutil.TimeoutExpired:
        log.error("shutdown: parent pid %s refused SIGKILL; leaving it", ctx.pid)


def monitor_loop(opts: argparse.Namespace, stop=None) -> int:
    """Main monitoring loop. Returns exit code (0 normal, 1 crash-loop abort).

    If `stop` is provided, the loop checks it after each poll and exits 0
    when it becomes truthy (used for graceful SIGINT/SIGTERM shutdown).
    """
    parent = find_parent(opts.parent)
    ctx = capture_context(parent)
    log.info("watching parent %r pid=%s cmd=%s", ctx.name, ctx.pid, " ".join(ctx.cmdline))

    child = find_child(parent)
    log.info("watching child pid=%s", child.pid)

    restart_times: deque[float] = deque()
    attempts_in_window = 0

    while True:
        time.sleep(opts.poll_interval)
        if stop and stop():
            log.info("stop flag set; watchdog exiting")
            if getattr(opts, "kill_on_exit", False):
                shutdown_parent(ctx, opts.grace_timeout)
            return 0

        # Re-acquire handles each iteration; pids can be reused.
        try:
            parent = psutil.Process(ctx.pid) if is_dead(parent) else parent
        except psutil.NoSuchProcess:
            parent = None

        if is_dead(parent):
            # Parent died — out of scope for this watchdog; exit cleanly.
            log.error("parent pid %s died; watchdog exiting (parent death is unhandled)",
                      ctx.pid)
            return 0

        child = find_child_safe(parent)
        if child is not None and not is_dead(child):
            # Stable: child present and alive. Decay backoff if stable long enough.
            if restart_times and (time.monotonic() - restart_times[-1]) > opts.restart_window:
                log.debug("stable for longer than restart-window; resetting counters")
                restart_times.clear()
                attempts_in_window = 0
            continue

        # Child is gone (or missing) but parent alive -> trigger restart.
        log.warning("child of parent pid %s is gone; preparing to restart parent",
                    parent.pid)

        # Debounce: parent may auto-respawn the child on its own.
        time.sleep(opts.recheck_window)
        child = find_child_safe(parent)
        if child is not None and not is_dead(child):
            log.info("child reappeared after debounce; aborting restart")
            continue

        # Crash-loop check.
        now = time.monotonic()
        while restart_times and (now - restart_times[0]) > opts.restart_window:
            restart_times.popleft()
        attempts_in_window = len(restart_times)

        if opts.max_restarts > 0 and attempts_in_window >= opts.max_restarts:
            log.error("crash loop: %d restarts within %.1fs; aborting",
                      attempts_in_window, opts.restart_window)
            return 1

        cooldown = min(opts.base_cooldown * (2 ** attempts_in_window),
                       opts.max_cooldown)
        if opts.max_restarts > 0:
            log.warning("restart attempt %d/%d; cooldown %.1fs",
                        attempts_in_window + 1, opts.max_restarts, cooldown)
        else:
            log.warning("restart attempt %d (unlimited); cooldown %.1fs",
                        attempts_in_window + 1, cooldown)
        time.sleep(cooldown)

        try:
            new_parent = restart_parent(ctx, opts.grace_timeout)
        except WatchdogError as exc:
            log.error("restart failed: %s", exc)
            restart_times.append(now)
            continue

        # Update context with new pid; keep launch params the same.
        ctx = ParentContext(
            pid=new_parent.pid,
            name=ctx.name,
            cmdline=ctx.cmdline,
            cwd=ctx.cwd,
            environ=ctx.environ,
            exe=ctx.exe,
            uid=ctx.uid,
        )
        restart_times.append(time.monotonic())

        # Wait for the new parent to spawn its child before resuming.
        try:
            child = wait_for_child(new_parent, timeout=opts.poll_interval * 5)
        except WatchdogError as exc:
            log.error("respawned parent pid %s did not produce a child: %s",
                      ctx.pid, exc)
            continue
        log.info("re-acquired child pid=%s for parent pid=%s", child.pid, ctx.pid)


def find_child_safe(parent: psutil.Process) -> psutil.Process | None:
    """find_child that returns None on transient errors instead of raising."""
    try:
        return find_child(parent)
    except WatchdogError as exc:
        log.debug("find_child: %s", exc)
        return None
    except psutil.NoSuchProcess:
        return None


def wait_for_child(parent: psutil.Process, timeout: float) -> psutil.Process:
    """Poll for the parent to spawn exactly one child within timeout seconds."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        child = find_child_safe(parent)
        if child is not None and not is_dead(child):
            return child
        time.sleep(0.5)
    raise WatchdogError(f"parent pid {parent.pid} did not spawn a child within {timeout}s")


def main(argv: list[str] | None = None) -> int:
    opts = parse_args(argv)
    logging.basicConfig(
        level=getattr(logging, opts.log_level),
        format="%(asctime)s %(levelname)s %(name)s: %(message)s",
    )

    stop = {"flag": False}

    def _handler(signum, _frame):
        log.info("received signal %s; shutting down", signum)
        stop["flag"] = True

    signal.signal(signal.SIGINT, _handler)
    signal.signal(signal.SIGTERM, _handler)

    try:
        return monitor_loop(opts, stop=lambda: stop["flag"])
    except WatchdogError as exc:
        log.error("watchdog error: %s", exc)
        return 1
    except KeyboardInterrupt:
        return 0


if __name__ == "__main__":
    sys.exit(main())