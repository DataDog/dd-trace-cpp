#!/usr/bin/env bash
# One-off experiment: are the trace ID benchmarks sensitive to the build path,
# to code alignment, and to the environment size?
# Results go to $ARTIFACTS_DIR. The summary is in summary.txt.

set -euo pipefail

readonly BASELINE_SHA=7059aedd512f5e47cbf1c56a925e7d1a1966915c
readonly CANDIDATE_SHA=95a089095ece54c4e5da9e171f5452ef15b0e360
readonly ALIGNMENT_FLAGS="-falign-functions=64 -falign-loops=64"
readonly BINARY=.build/benchmark/dd_trace_cpp-benchmark
readonly SOURCE_MIRROR=/app/mirror
readonly SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
readonly SUMMARY="$ARTIFACTS_DIR/summary.txt"
readonly PYTHON="$(command -v python3 || echo /app/benchmark-github-tools/.venv/bin/python3)"

main() {
  git clone --quiet --recurse-submodules https://github.com/DataDog/dd-trace-cpp "$SOURCE_MIRROR"

  section "Run 1: default flags, CI paths"
  build "$BASELINE_SHA" /app/baseline ""
  build "$CANDIDATE_SHA" /app/candidate ""
  compare run1 /app/baseline /app/candidate

  section "Run 2: same commit, CI paths"
  build "$BASELINE_SHA" /app/candidate ""
  compare_layout /app/baseline /app/candidate
  compare run2 /app/baseline /app/candidate

  section "Run 3: default flags, same-length paths"
  build "$BASELINE_SHA" /app/base ""
  build "$CANDIDATE_SHA" /app/cand ""
  compare run3 /app/base /app/cand

  section "Run 4: alignment flags, CI paths"
  build "$BASELINE_SHA" /app/baseline "$ALIGNMENT_FLAGS"
  build "$CANDIDATE_SHA" /app/candidate "$ALIGNMENT_FLAGS"
  compare run4 /app/baseline /app/candidate

  section "Run 5: alignment flags, same-length paths"
  build "$BASELINE_SHA" /app/base "$ALIGNMENT_FLAGS"
  build "$CANDIDATE_SHA" /app/cand "$ALIGNMENT_FLAGS"
  compare run5 /app/base /app/cand

  section "Run 6: environment padding sweep, candidate of run 5"
  sweep_environment run6 /app/cand
}

section() {
  printf '\n=== %s\n' "$1" | tee -a "$SUMMARY"
}

build() {
  local -r commit_sha="$1" directory="$2" flags="$3"
  echo "Building $commit_sha in $directory with flags '$flags'"
  rm -rf "$directory"
  cp -a "$SOURCE_MIRROR" "$directory"
  (
    cd "$directory"
    git checkout --quiet "$commit_sha"
    git submodule update --init --quiet
    cmake -S . -B .build -DDD_TRACE_BUILD_BENCHMARK=1 -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_C_FLAGS="$flags" -DCMAKE_CXX_FLAGS="$flags"
    cmake --build .build -j "$(nproc)"
  ) >> "$ARTIFACTS_DIR/build.log" 2>&1
}

compare() {
  local -r label="$1" baseline_directory="$2" candidate_directory="$3"
  # Same order as the CI: candidate first.
  run_benchmark "$candidate_directory" "$ARTIFACTS_DIR/$label-candidate.json"
  run_benchmark "$baseline_directory" "$ARTIFACTS_DIR/$label-baseline.json"
  "$PYTHON" "$SCRIPT_DIR/benchmark-experiment.py" compare \
    "$ARTIFACTS_DIR/$label-baseline.json" "$ARTIFACTS_DIR/$label-candidate.json" | tee -a "$SUMMARY"
}

# Extra arguments are environment variables, as "NAME=value".
run_benchmark() {
  local -r directory="$1" output="$2"
  shift 2
  echo "Running $directory/$BINARY"
  # Run from the source directory, like bin/benchmark does in the CI.
  (
    cd "$directory"
    env "$@" "$BINARY" --benchmark_filter='TraceID|Hex' --benchmark_repetitions=10 \
      --benchmark_format=json --benchmark_out="$output" > /dev/null
  )
}

compare_layout() {
  local -r first="$1/$BINARY" second="$2/$BINARY"
  local moved_functions
  moved_functions="$(diff <(text_symbols "$first") <(text_symbols "$second") | grep -c '^<' || true)"
  echo "Functions at different addresses: $moved_functions" | tee -a "$SUMMARY"
  local binary
  for binary in "$first" "$second"; do
    echo "$binary: $(size -A "$binary" | awk '$1 ~ /^\.(text|rodata|data)$/ {printf "%s=%s ", $1, $2}')" \
      | tee -a "$SUMMARY"
  done
}

text_symbols() {
  nm --defined-only "$1" | awk '$2 ~ /^[tT]$/' | sort -k3
}

sweep_environment() {
  local -r label="$1" directory="$2"
  local outputs=()
  local padding_size output
  for padding_size in $(seq 0 32 512); do
    output="$ARTIFACTS_DIR/$label-padding-$padding_size.json"
    run_benchmark "$directory" "$output" "PADDING=$(printf '%*s' "$padding_size" '' | tr ' ' x)"
    outputs+=("$output")
  done
  echo "Change vs padding 0, for padding sizes: $(seq -s ' ' 0 32 512)" | tee -a "$SUMMARY"
  "$PYTHON" "$SCRIPT_DIR/benchmark-experiment.py" sweep "${outputs[@]}" | tee -a "$SUMMARY"
}

main
