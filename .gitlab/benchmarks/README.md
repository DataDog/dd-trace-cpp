# Microbenchmarks

The pipeline runs [Google Benchmark](https://github.com/google/benchmark)
microbenchmarks from `benchmark/` on the Benchmarking Platform, and compares them
against a baseline to detect regressions on every PR.

Files in this directory:

- **`steps/`** — the run, analyze, upload, and PR-comment scripts called by the
  `benchmarks` CI job in [`../benchmarks.yml`](../benchmarks.yml).
- **`container/`** — Dockerfile and install script for the benchmarks CI image.
- **`bp-runner.fail-on-regression.yml`** — gate config. The
  `check-big-regressions` job runs it with `bp-runner` and fails the pipeline on
  regressions above `regression_threshold`.

Execution flow on each PR:

1. The `benchmarks` job collects hardware info, runs `benchmark/` twice
   (candidate and baseline), converts and analyzes the results, uploads them to
   S3 and the BP API, and comments on the PR.
2. The `check-big-regressions` job runs
   `bp-runner.fail-on-regression.yml` and fails on regressions above the
   threshold.

## How to add a benchmark

1. Write it in [`benchmark/`](../../benchmark/) using Google Benchmark, and add
   the source file to `benchmark/CMakeLists.txt`.
2. Verify it runs locally (see below).

New benchmarks that don't exist on the baseline branch are skipped from the
baseline comparison on the first run.

## How to update a benchmark

Edit the source in `benchmark/` or the scripts in `steps/`. No image rebuild is
needed for those: the image only carries the toolchain, not the benchmarks.
Rebuild the image (see below) only when `container/` changes.

## How to mark a benchmark as flaky

Add it to `FLAKY_BENCHMARKS_REGEX` in the `benchmarks` job's `variables` in
[`../benchmarks.yml`](../benchmarks.yml). The benchmark still runs and reports,
but it does not fail the gate.

- The regex matches anywhere in the scenario name.
    - `BM_Trace` quarantines every scenario containing it, like
      `BM_TraceTinyCCSource`.
    - Anchor with `^...$` to target one scenario.

```yaml
FLAKY_BENCHMARKS_REGEX: "^BM_TraceTinyCCSource$"
```

Open a ticket to fix or remove it. See
[Flaky Benchmarks Monitoring](https://datadoghq.atlassian.net/wiki/x/dAKLrgE)
for more details.

## How to run a benchmark locally

From the repository root:

```bash
bin/benchmark --benchmark_repetitions=10 --benchmark_format=json --benchmark_out=candidate.json
```

`bin/benchmark` builds `dd-trace-cpp` and the benchmark, then runs it.

To run the full CI steps, mimic the `benchmarks` job script: set
`ARTIFACTS_DIR`, `UPSTREAM_PROJECT_NAME`, `UPSTREAM_BRANCH`, and
`UPSTREAM_COMMIT_SHA`, put this directory's `steps/` on `PATH`, then call the
scripts in the same order as the job. Some scripts need tools that only exist in
the CI image (`benchmark_analyzer`, `github-find-merge-into-branch`,
`pr-commenter`).

## How to rebuild the CI image

The `BENCHMARKS_CI_IMAGE` used by the benchmark jobs is built from
`container/`. Rebuild and repin it whenever `container/` changes.

1. Edit the files under `container/`. Push the branch.
2. On the pipeline, run the manual `build-benchmark-ci-images` job. It is
   defined by
   [`build-ci-images.template.yml`](https://github.com/DataDog/benchmarking-platform-tools/blob/main/images/templates/gitlab/build-ci-images.template.yml)
   and pushes `registry.ddbuild.io/ci/benchmarking-platform:dd-trace-cpp-<pipeline id>`.
3. Copy the produced tag and the job URL, and pin
   `BENCHMARKS_CI_IMAGE` in [`../benchmarks.yml`](../benchmarks.yml) to it:

```yaml
# Image built here: <build-benchmark-ci-images job URL>
BENCHMARKS_CI_IMAGE: registry.ddbuild.io/ci/benchmarking-platform:dd-trace-cpp-<pipeline id>
```

4. Push the update and verify a green pipeline before merging.
