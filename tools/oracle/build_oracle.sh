#!/usr/bin/env bash
# Build a runnable oracle from ext/agcplusplus: load a rope, tick N times, dump
# erasable cells. Diff its answers against ours.
#
# Upstream's own binary wants sockets, a DSKY, threads and a wall clock, none of
# which a comparison needs. This host links only the Block II core and drives
# cpu.tick() directly. ext/ is never modified: the two things this toolchain
# needs are supplied from outside as -include and -I.
#
#   ORACLE_TRACE=1 ./oracle <rope> <ticks> [octal-addr ...]
#
# prints one line of state per timing pulse to ./AGCPlusPlus.log, which is what
# makes a pulse-by-pulse diff against our --trace possible. That diff is how the
# divide bug in FINDINGS #40 was found.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
src="$here/../../ext/agcplusplus/src"
out="${1:-$here/../../build/oracle}"

[[ -f $src/block2/cpu.cpp ]] || {
    echo "ext/agcplusplus is missing. Run: git submodule update --init ext/agcplusplus" >&2
    exit 1
}
mkdir -p "$(dirname "$out")"

# g++, not clang: upstream declares constexpr doubles initialised by std::pow
# and std::cos, which clang rejects as non-constant expressions.
g++ -std=c++17 -w -O2 \
    -include "$here/prelude.hpp" -I"$here/stub" -I"$src" \
    -o "$out" "$here/oracle_main.cpp" \
    "$src"/block2/{agc,cpu,memory,subinstructions,scaler,cdu,imu}.cpp \
    "$src"/common/util_functions.cpp
echo "built $out"
