#!/usr/bin/env bash

set -euo pipefail

# Temporarily enable uploading from inside pr commenter script, till dd-trace-cpp repo has an update
"$(dirname "$0")/upload-results-to-benchmarking-api.sh" || :

# Post comparison with baseline when running on a PR
BASELINE_BRANCH=$(github-find-merge-into-branch --for-repo="$UPSTREAM_PROJECT_NAME" --for-pr="$UPSTREAM_BRANCH")
if [ -n "$BASELINE_BRANCH" ]; then
  cat "$ARTIFACTS_DIR/comparison-baseline-vs-candidate.md" | pr-commenter --for-repo="$UPSTREAM_PROJECT_NAME" --for-pr="$UPSTREAM_BRANCH" --header="Benchmarks" --on-duplicate=replace
fi
