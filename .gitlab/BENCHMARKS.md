# Benchmarks

GitLab CI configuration for the benchmarks that run on the
[Benchmarking Platform](https://datadoghq.atlassian.net/wiki/spaces/APMINT/pages/2419261562/Benchmarking+Platform).

## Layout

- `benchmarks.yml`: Google Benchmark microbenchmarks.
    - `benchmarks` clones `benchmarking-platform` (`dd-trace-cpp` branch), runs the benchmark,
      converts and analyzes results, uploads and comments on the PR.
    - `check-big-regressions` fails on regressions above the threshold defined on
      `bp-runner.fail-on-regression.yml` in the `dd-trace-cpp` branch of
      [benchmarking-platform](https://github.com/DataDog/benchmarking-platform).

## Marking a benchmark as flaky

Add it to `FLAKY_BENCHMARKS_REGEX` in the `benchmarks` job's `variables` in `benchmarks.yml`.

The benchmark still runs and reports, but doesn't fail the gate.

- The regex matches anywhere in the scenario name.
    - `BM_Trace` quarantines every scenario containing it, like `BM_TraceTinyCCSource`.
    - Anchor with `^...$` to target one scenario.

```yaml
FLAKY_BENCHMARKS_REGEX: "^BM_TraceTinyCCSource$"
```

Open a ticket to fix or remove it. See
[Flaky Benchmarks Monitoring](https://datadoghq.atlassian.net/wiki/spaces/APMINT/pages/7223313012/Flaky+Benchmarks+Monitoring).
