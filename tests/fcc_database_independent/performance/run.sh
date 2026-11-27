#!/bin/sh
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
{ date -u; uname -a; c++ --version; lscpu | head -25; } > environment.log
c++ -std=c++17 -O2 -Wall -Wextra -Werror "$src/benchmark.cpp" -o ./fcc_benchmark 2> build.log
./fcc_benchmark "${1:-100000}" "${2:-100000}" > results.csv 2> execution.log
