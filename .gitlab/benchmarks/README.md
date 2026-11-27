# Microbenchmarks

The pipeline runs [Google Benchmark](https://github.com/google/benchmark)
microbenchmarks from `benchmark/` on the Benchmarking Platform, and compares them
against a baseline to detect regressions on every PR.

Files in this directory:

- **`bp-runner.fail-on-regression.yml`**: `bp-runner` config for the regression
  gate.
- **`bp-runner.pr-comment.yml`**: `bp-runner` config for the PR comment.
- **`bp-runner.yml`**: `bp-runner` config for the benchmark run.
- **`container/`**: Dockerfile and install script for the benchmarks CI image.

Execution flow on each PR:

1. The `benchmarks` job runs `bp-runner .gitlab/benchmarks/bp-runner.yml
   --debug`: it collects hardware info, runs `benchmark/` twice (candidate and
   baseline), converts and analyzes the results, and uploads them to S3.
2. The `benchmarks-pr-comment` job runs
   `bp-runner .gitlab/benchmarks/bp-runner.pr-comment.yml --debug`, posts
   the comparison on the PR, and uploads the results to the BP API.
3. The `check-big-regressions` job runs
   `bp-runner.fail-on-regression.yml` and fails on regressions above the
   threshold.

## How to Add a Benchmark

1. Write it in [`benchmark/`](../../benchmark/) using Google Benchmark, and add
   the source file to `benchmark/CMakeLists.txt`.
2. Verify it runs locally (see below).

New benchmarks that don't exist on the baseline branch are skipped from the
baseline comparison on the first run.

## How to Update a Benchmark

Edit the source in `benchmark/`. The experiment only builds and runs what is in
`benchmark/`, so no config change is needed to add, edit, or remove a
benchmark. Edit `bp-runner.yml` when the flow itself changes (fetch, run,
analyze, upload), and `container/` only when the toolchain changes.

## How to Mark a Benchmark as Flaky

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

## How to Run a Benchmark Locally

From the repository root:

```bash
bin/benchmark --benchmark_repetitions=10 --benchmark_format=json --benchmark_out=candidate.json
```

`bin/benchmark` builds `dd-trace-cpp` and the benchmark, then runs it.

To run the full experiment locally, install `bp-runner` and run it against the
config, like the job does:

```bash
export ARTIFACTS_DIR="$(pwd)/reports"
bp-runner .gitlab/benchmarks/bp-runner.yml --debug
```

Some steps need tools that only exist in the CI image (`benchmark_analyzer`,
`github-find-merge-into-branch`, `pr-commenter`).

## How to Rebuild the CI Image

The `BENCHMARKS_CI_IMAGE` used by the benchmark jobs is built from
`container/`. Rebuild and repin it whenever `container/` changes.

1. Edit the files under `container/`. Push the branch.
2. On the pipeline, run the manual `build-benchmark-ci-images` job. It is
   defined by
   [`build-ci-images.template.yml`](https://github.com/DataDog/benchmarking-platform-tools/blob/main/images/templates/gitlab/build-ci-images.template.yml)
   and pushes `registry.ddbuild.io/ci/benchmarking-platform:dd-trace-cpp-<pipeline id>`.
3. Copy the produced tag, and pin `BENCHMARKS_CI_IMAGE` in
   [`../benchmarks.yml`](../benchmarks.yml) to it:

```yaml
BENCHMARKS_CI_IMAGE: registry.ddbuild.io/ci/benchmarking-platform:dd-trace-cpp-<pipeline id>
```

4. Push the update and verify a green pipeline before merging.
