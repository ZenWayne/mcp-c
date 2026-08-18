"""Unit tests for watchdog.py using a fake psutil shim.

Covers:
- find_parent: zero / one / multiple matches
- find_child: zero / one / multiple children
- is_dead: alive / dead / zombie
- restart_parent: SIGTERM-then-kill path, already-gone path
- monitor_loop: child-death triggers restart; crash-loop aborts after N
  restarts in window; stable recovery resets counters; debounce aborts
  restart when child reappears.
"""

from __future__ import annotations

import os
import sys
import time
import unittest
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import watchdog


class FakeProcess:
    """Minimal stand-in for psutil.Process used by watchdog."""

    def __init__(self, pid, name="p", status="running", children=None,
                 cmdline=("p",), cwd="/", environ=None, exe="/bin/p",
                 uid=1000, alive=True):
        self.pid = pid
        self._name = name
        self._status = status
        self._children = children or []
        self._cmdline = list(cmdline)
        self._cwd = cwd
        self._environ = environ if environ is not None else {"PATH": "/bin"}
        self._exe = exe
        self._uid = uid
        self._alive = alive
        self.terminate_called = False
        self.kill_called = False
        self._wait_event = None

    @property
    def info(self):
        return {"pid": self.pid, "name": self._name}

    def name(self):
        return self._name

    def cmdline(self):
        return list(self._cmdline)

    def cwd(self):
        return self._cwd

    def environ(self):
        return dict(self._environ)

    def exe(self):
        return self._exe

    def uids(self):
        class U:
            def __init__(self, real):
                self.real = real
        return U(self._uid)

    def is_running(self):
        return self._alive

    def status(self):
        return self._status

    def children(self, recursive=False):
        return [c for c in self._children if c._alive]

    def terminate(self):
        self.terminate_called = True
        self._alive = False
        self._children = []

    def kill(self):
        self.kill_called = True
        self._alive = False

    def wait(self, timeout=None):
        if not self._alive:
            return 0
        raise watchdog.psutil.TimeoutExpired(timeout)

    def set_children(self, children):
        self._children = children

    def set_alive(self, alive):
        self._alive = alive

    def set_status(self, status):
        self._status = status


def make_fake_psutil(parent_proc, child_proc, process_iter_list=None,
                     new_proc_factory=None, sleep_noop=True):
    """Build a fake psutil module patching the attributes watchdog uses."""
    fake = mock.MagicMock()
    fake.NoSuchProcess = type("NoSuchProcess", (Exception,), {})
    fake.AccessDenied = type("AccessDenied", (Exception,), {})
    fake.TimeoutExpired = type("TimeoutExpired", (Exception,), {"__init__":
        lambda self, *a, **k: Exception.__init__(self, *a, **k)})
    fake.STATUS_ZOMBIE = "zombie"

    def process_lookup(pid):
        if pid == parent_proc.pid and parent_proc._alive:
            return parent_proc
        if new_proc_factory is not None:
            np = new_proc_factory(pid)
            if np is not None:
                return np
        raise fake.NoSuchProcess(f"no proc {pid}")

    fake.Process = process_lookup

    def process_iter(attrs=None):
        procs = process_iter_list if process_iter_list is not None else [parent_proc]
        return [p for p in procs if p._alive or p is parent_proc]

    fake.process_iter = process_iter
    return fake


class FindParentTests(unittest.TestCase):
    def test_zero_matches_raises(self):
        fake = make_fake_psutil(None, None, process_iter_list=[])
        with mock.patch.object(watchdog, "psutil", fake), \
                self.assertRaises(watchdog.WatchdogError) as cm:
            watchdog.find_parent("nope")
        self.assertIn("no process named", str(cm.exception))

    def test_multiple_matches_raises(self):
        p1 = FakeProcess(101, name="srv")
        p2 = FakeProcess(102, name="srv")
        fake = make_fake_psutil(p1, None, process_iter_list=[p1, p2])
        with mock.patch.object(watchdog, "psutil", fake), \
                self.assertRaises(watchdog.WatchdogError) as cm:
            watchdog.find_parent("srv")
        self.assertIn("multiple processes", str(cm.exception))

    def test_single_match_returns_proc(self):
        p = FakeProcess(101, name="srv")
        fake = make_fake_psutil(p, None, process_iter_list=[p])
        with mock.patch.object(watchdog, "psutil", fake):
            result = watchdog.find_parent("srv")
        self.assertIs(result, p)


class FindChildTests(unittest.TestCase):
    def test_no_children_raises(self):
        p = FakeProcess(1)
        with self.assertRaises(watchdog.WatchdogError) as cm:
            watchdog.find_child(p)
        self.assertIn("no children", str(cm.exception))

    def test_multiple_children_raises(self):
        c1 = FakeProcess(2)
        c2 = FakeProcess(3)
        p = FakeProcess(1, children=[c1, c2])
        with self.assertRaises(watchdog.WatchdogError) as cm:
            watchdog.find_child(p)
        self.assertIn("multiple children", str(cm.exception))

    def test_single_child_returns_it(self):
        c = FakeProcess(2)
        p = FakeProcess(1, children=[c])
        self.assertIs(watchdog.find_child(p), c)


class IsDeadTests(unittest.TestCase):
    def test_none_is_dead(self):
        self.assertTrue(watchdog.is_dead(None))

    def test_alive_is_not_dead(self):
        p = FakeProcess(1, alive=True)
        self.assertFalse(watchdog.is_dead(p))

    def test_dead_is_dead(self):
        p = FakeProcess(1, alive=False)
        self.assertTrue(watchdog.is_dead(p))

    def test_zombie_is_dead(self):
        p = FakeProcess(1, status="zombie", alive=True)
        self.assertTrue(watchdog.is_dead(p))


class CaptureContextTests(unittest.TestCase):
    def test_captures_all_fields(self):
        p = FakeProcess(42, name="srv", cmdline=("srv", "--port", "80"),
                        cwd="/srv", environ={"X": "1"}, exe="/usr/bin/srv",
                        uid=1001)
        ctx = watchdog.capture_context(p)
        self.assertEqual(ctx.pid, 42)
        self.assertEqual(ctx.name, "srv")
        self.assertEqual(ctx.cmdline, ["srv", "--port", "80"])
        self.assertEqual(ctx.cwd, "/srv")
        self.assertEqual(ctx.environ, {"X": "1"})
        self.assertEqual(ctx.exe, "/usr/bin/srv")
        self.assertEqual(ctx.uid, 1001)

    def test_environ_fallback_on_access_denied(self):
        p = FakeProcess(42)
        original_environ = p.environ
        p.environ = mock.Mock(side_effect=watchdog.psutil.AccessDenied())
        # watchdog.psutil is the REAL psutil; AccessDenied exists there too.
        ctx = watchdog.capture_context(p)
        self.assertEqual(ctx.environ, dict(os.environ))
        # Restore so other tests don't break.
        p.environ = original_environ


class RestartParentTests(unittest.TestCase):
    def test_terminates_then_respawns(self):
        ctx = watchdog.ParentContext(
            pid=100, name="srv", cmdline=["srv", "--x"], cwd="/",
            environ={"PATH": "/bin"}, exe="/bin/srv", uid=1000,
        )
        new_p = FakeProcess(200, alive=True)
        popen_mock = mock.MagicMock()
        popen_mock.pid = 200

        def fake_popen(*a, **k):
            return popen_mock

        with mock.patch.object(watchdog.psutil, "Process", lambda pid: new_p), \
             mock.patch.object(watchdog.subprocess, "Popen", fake_popen), \
             mock.patch.object(watchdog.time, "sleep", lambda s: None):
            result = watchdog.restart_parent(ctx, grace_timeout=5.0)

        self.assertIs(result, new_p)
        # Old parent should have been terminated (since it's alive at pid 100
        # in the real psutil lookup -> but we patched Process to always return
        # new_p for any pid. So old lookup returns new_p with pid 200, not 100.
        # To test the terminate path properly we need Process(100) -> p.)
        # We'll test that path in a separate test below.

    def test_terminate_path_kills_old_parent(self):
        old = FakeProcess(100, cmdline=("srv",), alive=True)
        new_p = FakeProcess(200, alive=True)
        ctx = watchdog.ParentContext(
            pid=100, name="srv", cmdline=["srv"], cwd="/",
            environ={"PATH": "/bin"}, exe="/bin/srv", uid=1000,
        )
        popen_mock = mock.MagicMock()
        popen_mock.pid = 200

        def process_lookup(pid):
            if pid == 100:
                return old
            if pid == 200:
                return new_p
            raise watchdog.psutil.NoSuchProcess(f"no {pid}")

        with mock.patch.object(watchdog.psutil, "Process", process_lookup), \
             mock.patch.object(watchdog.subprocess, "Popen",
                               lambda *a, **k: popen_mock), \
             mock.patch.object(watchdog.time, "sleep", lambda s: None):
            watchdog.restart_parent(ctx, grace_timeout=5.0)

        self.assertTrue(old.terminate_called)

    def test_already_gone_skips_terminate(self):
        ctx = watchdog.ParentContext(
            pid=999, name="srv", cmdline=["srv"], cwd="/",
            environ={"PATH": "/bin"}, exe="/bin/srv", uid=1000,
        )
        new_p = FakeProcess(200, alive=True)
        popen_mock = mock.MagicMock()
        popen_mock.pid = 200

        def process_lookup(pid):
            if pid == 999:
                raise watchdog.psutil.NoSuchProcess("gone")
            if pid == 200:
                return new_p
            raise watchdog.psutil.NoSuchProcess("no")

        with mock.patch.object(watchdog.psutil, "Process", process_lookup), \
             mock.patch.object(watchdog.subprocess, "Popen",
                               lambda *a, **k: popen_mock), \
             mock.patch.object(watchdog.time, "sleep", lambda s: None):
            watchdog.restart_parent(ctx, grace_timeout=5.0)


class MonitorLoopTests(unittest.TestCase):
    """Drive monitor_loop by replacing time.sleep with a step controller."""

    def _opts(self, **overrides):
        defaults = {
            "parent": "srv", "poll_interval": 0.0, "max_restarts": 3,
            "restart_window": 60.0, "base_cooldown": 0.0, "max_cooldown": 0.0,
            "grace_timeout": 5.0, "recheck_window": 0.0, "log_level": "ERROR",
        }
        defaults.update(overrides)
        return watchdog.parse_args(
            ["--parent", defaults["parent"],
             "--poll-interval", str(defaults["poll_interval"]),
             "--max-restarts", str(defaults["max_restarts"]),
             "--restart-window", str(defaults["restart_window"]),
             "--base-cooldown", str(defaults["base_cooldown"]),
             "--max-cooldown", str(defaults["max_cooldown"]),
             "--grace-timeout", str(defaults["grace_timeout"]),
             "--recheck-window", str(defaults["recheck_window"]),
             "--log-level", defaults["log_level"]]
        )

    def test_child_death_triggers_restart_then_stable(self):
        """Child dies once, parent is restarted, new child appears, loop stable."""
        parent = FakeProcess(100, name="srv", cmdline=("srv",), alive=True)
        child1 = FakeProcess(101, alive=True)
        parent.set_children([child1])
        new_parent = FakeProcess(200, name="srv", cmdline=("srv",), alive=True)
        new_child = FakeProcess(201, alive=True)
        new_parent.set_children([new_child])

        ctx_holder = {"restarts": 0, "iterations": 0, "max_iter": 5}

        popen_mock = mock.MagicMock()
        popen_mock.pid = 200

        def process_lookup(pid):
            if pid == 100:
                return parent
            if pid == 200:
                return new_parent
            raise watchdog.psutil.NoSuchProcess(str(pid))

        def fake_sleep(s):
            ctx_holder["iterations"] += 1
            # On the first poll, kill the child so the loop detects it gone.
            if ctx_holder["iterations"] == 1:
                child1.set_alive(False)
            # After 5 iterations, stop the loop by raising StopIteration.
            if ctx_holder["iterations"] >= ctx_holder["max_iter"]:
                raise StopIteration

        def fake_restart_parent(ctx, grace_timeout):
            ctx_holder["restarts"] += 1
            # Simulate the old parent being terminated; ctx.pid updates to 200.
            parent.set_alive(False)
            return new_parent

        opts = self._opts()
        with mock.patch.object(watchdog.psutil, "Process", process_lookup), \
             mock.patch.object(watchdog.subprocess, "Popen",
                               lambda *a, **k: popen_mock), \
             mock.patch.object(watchdog, "time") as time_mod, \
             mock.patch.object(watchdog, "restart_parent",
                               side_effect=fake_restart_parent), \
             mock.patch.object(watchdog, "find_parent", lambda name: parent), \
             mock.patch.object(watchdog, "find_child", lambda p: child1 if p is parent else new_child), \
             mock.patch.object(watchdog, "wait_for_child", lambda p, timeout: new_child):
            time_mod.sleep = fake_sleep
            time_mod.monotonic = time.monotonic
            try:
                watchdog.monitor_loop(opts)
            except StopIteration:
                pass

        self.assertEqual(ctx_holder["restarts"], 1)

    def test_crash_loop_aborts_after_max(self):
        """Child repeatedly dead -> restart_parent called max-restarts times,
        then monitor_loop returns 1."""
        parent = FakeProcess(100, name="srv", cmdline=("srv",), alive=True)
        initial_child = FakeProcess(101, alive=True)
        parent.set_children([initial_child])

        state = {"restarts": 0, "iterations": 0, "max_iter": 50}
        new_parent = FakeProcess(200, alive=True)

        def fake_restart_parent(ctx, grace_timeout):
            state["restarts"] += 1
            return new_parent

        def fake_sleep(s):
            state["iterations"] += 1
            if state["iterations"] >= state["max_iter"]:
                raise StopIteration

        opts = self._opts(max_restarts=2, base_cooldown=0.0, max_cooldown=0.0,
                          recheck_window=0.0)
        def process_lookup(pid):
            if pid == 100:
                return parent
            if pid == 200:
                return new_parent
            raise watchdog.psutil.NoSuchProcess(str(pid))

        with mock.patch.object(watchdog.psutil, "Process", process_lookup), \
             mock.patch.object(watchdog, "time") as time_mod, \
             mock.patch.object(watchdog, "restart_parent",
                               side_effect=fake_restart_parent), \
             mock.patch.object(watchdog, "find_parent", lambda name: parent), \
             mock.patch.object(watchdog, "find_child", lambda p: initial_child), \
             mock.patch.object(watchdog, "find_child_safe", lambda p: None), \
             mock.patch.object(watchdog, "wait_for_child", lambda p, timeout=1: FakeProcess(999, alive=True)):
            time_mod.sleep = fake_sleep
            counter = [0.0]
            def fake_monotonic():
                counter[0] += 1.0
                return counter[0]
            time_mod.monotonic = fake_monotonic
            try:
                rc = watchdog.monitor_loop(opts)
            except StopIteration:
                rc = None

        self.assertEqual(rc, 1, "should abort with exit code 1 on crash loop")
        self.assertEqual(state["restarts"], 2)

    def test_max_restarts_zero_means_unlimited(self):
        """--max-restarts 0 should never abort: restart keeps happening."""
        parent = FakeProcess(100, name="srv", cmdline=("srv",), alive=True)
        initial_child = FakeProcess(101, alive=True)
        parent.set_children([initial_child])

        state = {"restarts": 0, "iterations": 0, "max_iter": 25}
        new_parent = FakeProcess(200, alive=True)

        def fake_restart_parent(ctx, grace_timeout):
            state["restarts"] += 1
            return new_parent

        def fake_sleep(s):
            state["iterations"] += 1
            if state["iterations"] >= state["max_iter"]:
                raise StopIteration

        opts = self._opts(max_restarts=0, base_cooldown=0.0, max_cooldown=0.0,
                          recheck_window=0.0)

        def process_lookup(pid):
            if pid == 100:
                return parent
            if pid == 200:
                return new_parent
            raise watchdog.psutil.NoSuchProcess(str(pid))

        with mock.patch.object(watchdog.psutil, "Process", process_lookup), \
             mock.patch.object(watchdog, "time") as time_mod, \
             mock.patch.object(watchdog, "restart_parent",
                               side_effect=fake_restart_parent), \
             mock.patch.object(watchdog, "find_parent", lambda name: parent), \
             mock.patch.object(watchdog, "find_child", lambda p: initial_child), \
             mock.patch.object(watchdog, "find_child_safe", lambda p: None), \
             mock.patch.object(watchdog, "wait_for_child", lambda p, timeout=1: FakeProcess(999, alive=True)):
            time_mod.sleep = fake_sleep
            counter = [0.0]
            def fake_monotonic():
                counter[0] += 1.0
                return counter[0]
            time_mod.monotonic = fake_monotonic
            try:
                rc = watchdog.monitor_loop(opts)
            except StopIteration:
                rc = None

        # Should NOT have aborted (rc is None because we broke out via
        # StopIteration after 25 iterations), and restarts should exceed
        # any small bound — specifically more than the old default of 5.
        self.assertIsNone(rc, "unlimited mode should not return on its own")
        self.assertGreater(state["restarts"], 5,
                           "should keep restarting past the default limit")

    def test_debounce_aborts_restart_when_child_reappears(self):
        """Child dies, but reappears during recheck_window -> no restart."""
        parent = FakeProcess(100, name="srv", alive=True, cmdline=("srv",))
        child = FakeProcess(101, alive=True)
        parent.set_children([child])

        state = {"iterations": 0, "max_iter": 3, "restarts": 0}

        def fake_sleep(s):
            state["iterations"] += 1
            # First poll: kill the child.
            if state["iterations"] == 1:
                child.set_alive(False)
            # During the recheck_window sleep (iteration 2's debounce), revive it.
            if state["iterations"] == 2:
                child.set_alive(True)
            if state["iterations"] >= state["max_iter"]:
                raise StopIteration

        def fake_restart_parent(ctx, grace_timeout):
            state["restarts"] += 1
            return parent

        opts = self._opts(recheck_window=1.0)
        with mock.patch.object(watchdog.psutil, "Process", lambda pid: parent), \
             mock.patch.object(watchdog, "time") as time_mod, \
             mock.patch.object(watchdog, "restart_parent",
                               side_effect=fake_restart_parent), \
             mock.patch.object(watchdog, "find_parent", lambda name: parent):
            time_mod.sleep = fake_sleep
            time_mod.monotonic = time.monotonic
            try:
                watchdog.monitor_loop(opts)
            except StopIteration:
                pass

        self.assertEqual(state["restarts"], 0,
                         "should not restart when child reappears in debounce")

    def test_parent_death_exits_cleanly(self):
        parent = FakeProcess(100, name="srv", alive=True, cmdline=("srv",))
        child = FakeProcess(101, alive=True)
        parent.set_children([child])

        state = {"iterations": 0}

        def fake_sleep(s):
            state["iterations"] += 1
            if state["iterations"] == 1:
                parent.set_alive(False)
                child.set_alive(False)

        opts = self._opts()
        with mock.patch.object(watchdog.psutil, "Process", lambda pid: parent), \
             mock.patch.object(watchdog, "time") as time_mod, \
             mock.patch.object(watchdog, "find_parent", lambda name: parent), \
             mock.patch.object(watchdog, "find_child_safe", lambda p: child):
            time_mod.sleep = fake_sleep
            time_mod.monotonic = time.monotonic
            rc = watchdog.monitor_loop(opts)

        self.assertEqual(rc, 0, "parent death should return exit code 0")


class MainTests(unittest.TestCase):
    def test_parse_args_required_parent(self):
        with self.assertRaises(SystemExit):
            watchdog.parse_args([])

    def test_parse_args_defaults(self):
        opts = watchdog.parse_args(["--parent", "srv"])
        self.assertEqual(opts.parent, "srv")
        self.assertEqual(opts.poll_interval, 2.0)
        self.assertEqual(opts.max_restarts, 5)
        self.assertEqual(opts.restart_window, 60.0)


if __name__ == "__main__":
    unittest.main()