#!/bin/bash

# Clean up existing
cd build && rm -rf * && cd ..

# Generate Makefiles
CC=afl-clang-fast CXX=afl-clang-fast++ cmake -B build -S .

# Do the build
CC=afl-clang-fast CXX=afl-clang-fast++ cmake --build build/ -j12