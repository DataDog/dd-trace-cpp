"""Summarize the results of benchmark-experiment.sh."""

import json
import statistics
import sys
from typing import Dict, List


def main() -> None:
    command, *paths = sys.argv[1:]
    if command == "compare":
        compare(paths[0], paths[1])
    elif command == "sweep":
        sweep(paths)
    else:
        sys.exit(f"Unknown command: {command}")


def compare(baseline_path: str, candidate_path: str) -> None:
    baseline = load_times(baseline_path)
    candidate = load_times(candidate_path)
    print(f"{'benchmark':40} {'baseline':>10} {'candidate':>10} {'change':>8}   cv baseline/candidate")
    for name, baseline_times in baseline.items():
        candidate_times = candidate[name]
        baseline_mean = statistics.mean(baseline_times)
        candidate_mean = statistics.mean(candidate_times)
        change = percent_change(baseline_mean, candidate_mean)
        print(
            f"{name:40} {baseline_mean:10.0f} {candidate_mean:10.0f} {change:+7.2f}%"
            f"   {coefficient_of_variation(baseline_times):.1f}% / {coefficient_of_variation(candidate_times):.1f}%"
        )


def sweep(paths: List[str]) -> None:
    results = [load_times(path) for path in paths]
    for name in results[0]:
        means = [statistics.mean(result[name]) for result in results]
        changes = [percent_change(means[0], mean) for mean in means]
        print(
            f"{name:40} spread {max(changes) - min(changes):5.2f}%  "
            + " ".join(f"{change:+.1f}" for change in changes)
        )


def load_times(path: str) -> Dict[str, List[float]]:
    with open(path) as file:
        benchmarks = json.load(file)["benchmarks"]
    times: Dict[str, List[float]] = {}
    for benchmark in benchmarks:
        if benchmark.get("run_type") == "iteration":
            times.setdefault(benchmark["run_name"], []).append(benchmark["real_time"])
    return times


def percent_change(reference: float, value: float) -> float:
    return (value - reference) / reference * 100


def coefficient_of_variation(times: List[float]) -> float:
    return statistics.stdev(times) / statistics.mean(times) * 100


if __name__ == "__main__":
    main()
