#!/bin/bash

AFL_SKIP_CPUFREQ=1 afl-fuzz -i seeds/ -o output/ -- ./build/mcpc
