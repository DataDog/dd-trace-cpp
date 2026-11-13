#!/usr/bin/env bash

set -ex

# More repetitions --> better representativeness of measurements --> less false negatives and false positives.
# However, the larger is repetitions count - the more time it will take to run benchmarks. So this is a tradeoff
# between rate of false positives / false negatives and time spent on running benchmarks.
#
# Must be at least REPETITIONS_COUNT=3 for confidence intervals calculation to work at all.
REPETITIONS_COUNT=10

# -- Candidate release

CANDIDATE_SRC="/app/candidate/"
CANDIDATE_BRANCH=$UPSTREAM_BRANCH
CANDIDATE_COMMIT_SHA=$UPSTREAM_COMMIT_SHA

# Clone your candidate release
git clone --recurse-submodules --branch $CANDIDATE_BRANCH https://github.com/DataDog/dd-trace-cpp/ "$CANDIDATE_SRC" && \
  cd "$CANDIDATE_SRC" && \
  git checkout $CANDIDATE_COMMIT_SHA

# Run benchmarks for candidate release
cd "$CANDIDATE_SRC"
bin/benchmark --benchmark_repetitions=$REPETITIONS_COUNT --benchmark_format=json --benchmark_out="$ARTIFACTS_DIR/candidate.json"

# -- Baseline release

if [ -z "$BASELINE_COMMIT_SHA" ]; then
  BASELINE_BRANCH=$(github-find-merge-into-branch --for-repo="$UPSTREAM_PROJECT_NAME" --for-pr="$UPSTREAM_BRANCH" || :)

  if [ ! -z "$BASELINE_BRANCH" ]; then
    cd "$CANDIDATE_SRC"
    BASELINE_COMMIT_SHA=$(git merge-base origin/$BASELINE_BRANCH origin/$CANDIDATE_BRANCH)
  fi
fi

if [ ! -z "$BASELINE_COMMIT_SHA" ]; then
  BASELINE_SRC="/app/baseline/"

  # Clone your baseline release
  git clone --recurse-submodules --branch $BASELINE_BRANCH https://github.com/DataDog/dd-trace-cpp/ "$BASELINE_SRC" && \
    cd "$BASELINE_SRC" && \
    git checkout $BASELINE_COMMIT_SHA

  # Run benchmarks for baseline release
  cd "$BASELINE_SRC"
  bin/benchmark --benchmark_repetitions=$REPETITIONS_COUNT --benchmark_format=json --benchmark_out="$ARTIFACTS_DIR/baseline.json"
fi
