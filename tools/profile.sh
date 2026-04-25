#!/usr/bin/env bash
#
# Sample-profile a synthesis_bench benchmark and print a flat profile.
#
# Usage:
#   tools/profile.sh <benchmark_filter> [duration_seconds]
#
# Examples:
#   tools/profile.sh BM_TractToTube
#   tools/profile.sh BM_SynthesisAddTube_Block 30
#
# Builds (or rebuilds) build/mac-relwithdebinfo so symbols are available,
# launches the benchmark in the background, attaches `sample`, then prints
# the per-function flat profile sorted by self time.
#
# macOS-only. On Linux use `perf record` / `perf report`; on Windows use
# the Visual Studio profiler or VTune.

set -euo pipefail

if [[ "$(uname)" != "Darwin" ]]; then
  echo "tools/profile.sh: macOS only (uses 'sample' and 'dsymutil')" >&2
  exit 1
fi

if [[ $# -lt 1 ]]; then
  echo "Usage: $0 <benchmark_filter> [duration_seconds]" >&2
  exit 1
fi

FILTER=$1
DURATION=${2:-15}
BUILD_DIR=build/mac-relwithdebinfo
BENCH=$BUILD_DIR/synthesis_bench
DYLIB=$BUILD_DIR/libVocalTractLabApi.dylib

# Configure once.
if [[ ! -f $BUILD_DIR/CMakeCache.txt ]]; then
  cmake -S . -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=third_party/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo
fi

cmake --build "$BUILD_DIR" --target synthesis_bench >/dev/null

# Make sure dSYMs are up to date so 'sample' can symbolicate.
[[ ! -d $BENCH.dSYM || $BENCH -nt $BENCH.dSYM ]] && dsymutil "$BENCH" >/dev/null
[[ ! -d $DYLIB.dSYM || $DYLIB -nt $DYLIB.dSYM ]] && dsymutil "$DYLIB" >/dev/null

# Run the benchmark long enough for the sampler to get a useful population.
LOG=$(mktemp)
SAMPLE=$(mktemp)
trap 'rm -f "$LOG" "$SAMPLE"' EXIT

"$BENCH" --benchmark_filter="$FILTER" \
         --benchmark_min_time=$((DURATION + 5))s >"$LOG" 2>&1 &
BENCH_PID=$!
sleep 1
sample "$BENCH_PID" "$DURATION" -file "$SAMPLE" >/dev/null 2>&1
wait $BENCH_PID

echo "=== benchmark result ==="
grep -E '^BM_|---|RTF=' "$LOG" | tail -5

echo
echo "=== flat profile (samples by self time) ==="
awk '/^Sort by top of stack/{p=1; next} /^Binary Images/{exit} p && /[A-Za-z]/' "$SAMPLE" | head -30
