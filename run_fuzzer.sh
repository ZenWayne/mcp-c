#!/bin/bash

MCP_FLAG=flag MCP_AUTH_TOKEN=token AFL_SKIP_CPUFREQ=1 afl-fuzz -i seeds/ -o output/ -- ./build/mcpc
