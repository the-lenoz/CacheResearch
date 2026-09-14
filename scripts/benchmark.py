#!/usr/bin/env python3

from __future__ import annotations

import argparse
import csv
import os
import subprocess
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
SHADOW_POLICIES = frozenset({"2Q", "ARC", "LIRS"})
RESIDENT_ONLY = "resident_only"
RESIDENT_AND_SHADOW = "resident_and_shadow"
CURRENT_VALUE_BYTES = "current"


@dataclass(frozen=True)
class Workload:
    path: Path
    name: str
    pattern: str
    cache_size: int
    request_count: int


@dataclass(frozen=True)
class CacheConfig:
    path: Path
    name: str
    levels: int
    policies: tuple[str, ...]


@dataclass(frozen=True)
class ExecutionResult:
    hits: int | None
    seconds: float
    error: str = ""


def positive_integer(value: str) -> int:
    parsed = int(value)
    if parsed <= 0:
        raise argparse.ArgumentTypeError("value must be positive")
    return parsed


def positive_float(value: str) -> float:
    parsed = float(value)
    if parsed <= 0:
        raise argparse.ArgumentTypeError("value must be positive")
    return parsed


def value_bytes_argument(value: str) -> int | None:
    if value == CURRENT_VALUE_BYTES:
        return None
    return positive_integer(value)


def value_bytes_label(value: int | None) -> str:
    return CURRENT_VALUE_BYTES if value is None else str(value)


def relative_name(path: Path, root: Path) -> str:
    try:
        return str(path.relative_to(root))
    except ValueError:
        return str(path)


def load_workloads(directory: Path) -> list[Workload]:
    workloads = []
    for path in sorted(directory.rglob("*.trace")):
        with path.open("r", encoding="utf-8") as input_file:
            first_line = input_file.readline()
        if not first_line:
            raise ValueError(f"empty workload: {path}")
        fields = first_line.split()
        if len(fields) != 2:
            raise ValueError(f"invalid workload header in {path}")
        cache_size, request_count = map(int, fields)
        if cache_size < 0 or request_count < 0:
            raise ValueError(f"negative size in workload header: {path}")
        workloads.append(
            Workload(
                path=path,
                name=relative_name(path, directory),
                pattern=path.parent.name,
                cache_size=cache_size,
                request_count=request_count,
            )
        )
    return workloads


def load_configs(directory: Path) -> list[CacheConfig]:
    configs = []
    for path in sorted(directory.rglob("*.conf")):
        fields = path.read_text(encoding="utf-8").split()
        if not fields:
            raise ValueError(f"empty config: {path}")
        levels = int(fields[0])
        policies = tuple(fields[1:])
        if levels <= 0 or len(policies) != levels:
            raise ValueError(f"invalid level count in {path}")
        if "BELADY" in policies:
            continue
        configs.append(
            CacheConfig(
                path=path,
                name=relative_name(path, directory),
                levels=levels,
                policies=policies,
            )
        )
    return configs


def input_with_capacity(workload: Workload, capacity: int) -> str:
    lines = workload.path.read_text(encoding="utf-8").splitlines()
    header = lines[0].split()
    header[0] = str(capacity)
    lines[0] = " ".join(header)
    return "\n".join(lines) + "\n"


def run_simulation(
    executable: Path,
    config: Path,
    workload: Workload,
    timeout: float,
    capacity_override: int | None = None,
    capacity_mode: str = RESIDENT_ONLY,
    value_bytes: int | None = None,
) -> ExecutionResult:
    command = [str(executable)]
    if capacity_mode == RESIDENT_AND_SHADOW:
        command.append("--capacity-includes-shadow")
    elif capacity_mode != RESIDENT_ONLY:
        return ExecutionResult(None, 0.0, f"unknown capacity mode: {capacity_mode}")
    if value_bytes is not None:
        command.extend(("--value-bytes", str(value_bytes)))
    command.append(str(config))

    started = time.perf_counter()
    try:
        if capacity_override is None:
            with workload.path.open("r", encoding="utf-8") as input_stream:
                process = subprocess.run(
                    command,
                    stdin=input_stream,
                    capture_output=True,
                    text=True,
                    timeout=timeout,
                    check=False,
                )
        else:
            process = subprocess.run(
                command,
                input=input_with_capacity(workload, capacity_override),
                capture_output=True,
                text=True,
                timeout=timeout,
                check=False,
            )
    except subprocess.TimeoutExpired:
        return ExecutionResult(None, time.perf_counter() - started, "timeout")
    except OSError as error:
        return ExecutionResult(None, time.perf_counter() - started, str(error))

    elapsed = time.perf_counter() - started
    if process.returncode != 0:
        message = process.stderr.strip() or f"exit code {process.returncode}"
        return ExecutionResult(None, elapsed, message)

    fields = process.stdout.split()
    if len(fields) != 1:
        return ExecutionResult(None, elapsed, f"unexpected output: {process.stdout!r}")
    try:
        hits = int(fields[0])
    except ValueError:
        return ExecutionResult(None, elapsed, f"non-integer output: {fields[0]!r}")
    return ExecutionResult(hits, elapsed)


def write_csv(path: Path, rows: list[dict[str, object]], fields: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def parse_arguments() -> argparse.Namespace:
    default_jobs = min(8, os.cpu_count() or 1)
    parser = argparse.ArgumentParser(
        description="Benchmark every cache config against every generated workload."
    )
    parser.add_argument(
        "--executable",
        type=Path,
        default=PROJECT_ROOT / "build" / "release" / "cache_sim",
    )
    parser.add_argument(
        "--configs",
        type=Path,
        default=PROJECT_ROOT / "configs" / "generated" / "3-level",
    )
    parser.add_argument(
        "--workloads",
        type=Path,
        default=PROJECT_ROOT / "workloads" / "generated",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=PROJECT_ROOT / "results" / "benchmark.csv",
    )
    parser.add_argument(
        "--summary",
        type=Path,
        default=PROJECT_ROOT / "results" / "best_by_workload.csv",
    )
    parser.add_argument(
        "--pattern-summary",
        type=Path,
        default=PROJECT_ROOT / "results" / "best_by_pattern.csv",
    )
    parser.add_argument(
        "--belady-config",
        type=Path,
        default=PROJECT_ROOT / "configs" / "belady.conf",
    )
    parser.add_argument(
        "--no-belady",
        action="store_true",
        help="skip the ideal-cache baseline",
    )
    parser.add_argument(
        "--value-bytes",
        nargs="+",
        type=value_bytes_argument,
        default=[None],
        metavar="BYTES",
        help=(
            "logical value sizes to benchmark; use 'current' for sizeof(DefaultValue) "
            "(default: current)"
        ),
    )
    parser.add_argument(
        "--config-names",
        nargs="+",
        metavar="NAME",
        help="benchmark only configs with these paths relative to --configs",
    )
    parser.add_argument("--jobs", type=positive_integer, default=default_jobs)
    parser.add_argument("--timeout", type=positive_float, default=30.0)
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    executable = arguments.executable.resolve()
    if not executable.is_file():
        raise SystemExit(f"cache executable not found: {executable}")
    if not arguments.configs.is_dir():
        raise SystemExit(
            f"config directory not found: {arguments.configs}\n"
            "run scripts/generate_configs.py first"
        )
    if not arguments.workloads.is_dir():
        raise SystemExit(
            f"workload directory not found: {arguments.workloads}\n"
            "run scripts/generate_workloads.py first"
        )

    configs = load_configs(arguments.configs)
    if arguments.config_names:
        requested_names = set(arguments.config_names)
        available_names = {config.name for config in configs}
        missing_names = sorted(requested_names - available_names)
        if missing_names:
            raise SystemExit(
                "requested configs not found: " + ", ".join(missing_names)
            )
        configs = [config for config in configs if config.name in requested_names]
    workloads = load_workloads(arguments.workloads)
    if not configs:
        raise SystemExit(f"no online configs found in {arguments.configs}")
    if not workloads:
        raise SystemExit(f"no workloads found in {arguments.workloads}")

    ideal_results: dict[tuple[Path, int], ExecutionResult] = {}
    level_counts = sorted({config.levels for config in configs})
    if not arguments.no_belady:
        if not arguments.belady_config.is_file():
            raise SystemExit(f"Belady config not found: {arguments.belady_config}")
        for workload in workloads:
            for levels in level_counts:
                ideal_results[(workload.path, levels)] = run_simulation(
                    executable,
                    arguments.belady_config,
                    workload,
                    arguments.timeout,
                    capacity_override=workload.cache_size * levels,
                )

    value_sizes = list(dict.fromkeys(arguments.value_bytes))
    tasks = [
        (workload, config, capacity_mode, value_bytes)
        for workload in workloads
        for config in configs
        for value_bytes in value_sizes
        for capacity_mode in (
            (RESIDENT_ONLY, RESIDENT_AND_SHADOW)
            if any(policy in SHADOW_POLICIES for policy in config.policies)
            else (RESIDENT_ONLY,)
        )
    ]
    completed = 0
    progress_step = max(1, len(tasks) // 20)
    executions: list[
        tuple[Workload, CacheConfig, str, int | None, ExecutionResult]
    ] = []

    with ThreadPoolExecutor(max_workers=arguments.jobs) as executor:
        pending = {
            executor.submit(
                run_simulation,
                executable,
                config.path,
                workload,
                arguments.timeout,
                capacity_mode=capacity_mode,
                value_bytes=value_bytes,
            ): (workload, config, capacity_mode, value_bytes)
            for workload, config, capacity_mode, value_bytes in tasks
        }
        for future in as_completed(pending):
            workload, config, capacity_mode, value_bytes = pending[future]
            executions.append(
                (workload, config, capacity_mode, value_bytes, future.result())
            )
            completed += 1
            if completed % progress_step == 0 or completed == len(tasks):
                print(f"completed {completed}/{len(tasks)} runs")

    executions.sort(
        key=lambda item: (
            item[0].name,
            item[1].name,
            value_bytes_label(item[3]),
            item[2],
        )
    )
    rows = []
    failed = 0
    for workload, config, capacity_mode, value_bytes, execution in executions:
        ideal = ideal_results.get((workload.path, config.levels))
        hits = execution.hits
        ideal_hits = ideal.hits if ideal else None
        ideal_failed = not arguments.no_belady and ideal_hits is None
        if hits is None or ideal_failed:
            failed += 1

        if hits is None:
            status = "failed"
            error = execution.error
        elif ideal_failed:
            status = "ideal_failed"
            error = ideal.error if ideal else "missing Belady result"
        else:
            status = "ok"
            error = ""

        hit_rate = None if hits is None else (
            hits / workload.request_count if workload.request_count else 1.0
        )
        ideal_hit_rate = None if ideal_hits is None else (
            ideal_hits / workload.request_count if workload.request_count else 1.0
        )

        rows.append(
            {
                "pattern": workload.pattern,
                "workload": workload.name,
                "cache_size_per_level": workload.cache_size,
                "total_capacity": workload.cache_size * config.levels,
                "requests": workload.request_count,
                "config": config.name,
                "levels": config.levels,
                "policies": ">".join(config.policies),
                "capacity_mode": capacity_mode,
                "value_bytes": value_bytes_label(value_bytes),
                "hits": "" if hits is None else hits,
                "misses": "" if hits is None else workload.request_count - hits,
                "hit_rate": "" if hit_rate is None else f"{hit_rate:.8f}",
                "ideal_hits": "" if ideal_hits is None else ideal_hits,
                "ideal_hit_rate": ""
                if ideal_hit_rate is None
                else f"{ideal_hit_rate:.8f}",
                "percent_of_ideal": ""
                if hits is None or ideal_hits is None
                else f"{(100.0 if ideal_hits == 0 else hits * 100 / ideal_hits):.4f}",
                "seconds": f"{execution.seconds:.6f}",
                "status": status,
                "error": error.replace("\n", " "),
            }
        )

    fields = list(rows[0])
    write_csv(arguments.output, rows, fields)

    successful = [row for row in rows if row["status"] == "ok"]
    best_by_workload: dict[tuple[str, str], dict[str, object]] = {}
    for row in successful:
        summary_key = (str(row["workload"]), str(row["value_bytes"]))
        previous = best_by_workload.get(summary_key)
        is_better = previous is None or int(row["hits"]) > int(previous["hits"])
        is_stable_tie_winner = (
            previous is not None
            and int(row["hits"]) == int(previous["hits"])
            and (str(row["config"]), str(row["capacity_mode"]))
            < (str(previous["config"]), str(previous["capacity_mode"]))
        )
        if is_better or is_stable_tie_winner:
            best_by_workload[summary_key] = row
    summary_rows = [best_by_workload[key] for key in sorted(best_by_workload)]
    write_csv(arguments.summary, summary_rows, fields)

    aggregates: dict[tuple[str, str, str, str], dict[str, object]] = {}
    for row in successful:
        key = (
            str(row["pattern"]),
            str(row["config"]),
            str(row["capacity_mode"]),
            str(row["value_bytes"]),
        )
        aggregate = aggregates.setdefault(
            key,
            {
                "pattern": row["pattern"],
                "config": row["config"],
                "capacity_mode": row["capacity_mode"],
                "value_bytes": row["value_bytes"],
                "levels": row["levels"],
                "policies": row["policies"],
                "workloads": 0,
                "total_requests": 0,
                "total_hits": 0,
                "total_ideal_hits": 0,
                "total_seconds": 0.0,
            },
        )
        aggregate["workloads"] = int(aggregate["workloads"]) + 1
        aggregate["total_requests"] = (
            int(aggregate["total_requests"]) + int(row["requests"])
        )
        aggregate["total_hits"] = int(aggregate["total_hits"]) + int(row["hits"])
        if row["ideal_hits"] != "":
            aggregate["total_ideal_hits"] = (
                int(aggregate["total_ideal_hits"]) + int(row["ideal_hits"])
            )
        aggregate["total_seconds"] = (
            float(aggregate["total_seconds"]) + float(row["seconds"])
        )

    aggregate_rows = []
    for aggregate in aggregates.values():
        requests = int(aggregate["total_requests"])
        hits = int(aggregate["total_hits"])
        ideal_hits = int(aggregate["total_ideal_hits"])
        aggregate_rows.append(
            {
                **aggregate,
                "hit_rate": f"{(hits / requests if requests else 1.0):.8f}",
                "percent_of_ideal": ""
                if arguments.no_belady
                else f"{(100.0 if ideal_hits == 0 else hits * 100 / ideal_hits):.4f}",
                "total_seconds": f"{float(aggregate['total_seconds']):.6f}",
            }
        )

    best_by_pattern: dict[tuple[str, str], dict[str, object]] = {}
    for row in aggregate_rows:
        pattern = str(row["pattern"])
        pattern_key = (pattern, str(row["value_bytes"]))
        previous = best_by_pattern.get(pattern_key)
        is_better = previous is None or float(row["hit_rate"]) > float(
            previous["hit_rate"]
        )
        is_stable_tie_winner = (
            previous is not None
            and float(row["hit_rate"]) == float(previous["hit_rate"])
            and (str(row["config"]), str(row["capacity_mode"]))
            < (str(previous["config"]), str(previous["capacity_mode"]))
        )
        if is_better or is_stable_tie_winner:
            best_by_pattern[pattern_key] = row

    pattern_fields = [
        "pattern",
        "config",
        "capacity_mode",
        "value_bytes",
        "levels",
        "policies",
        "workloads",
        "total_requests",
        "total_hits",
        "hit_rate",
        "total_ideal_hits",
        "percent_of_ideal",
        "total_seconds",
    ]
    pattern_rows = [best_by_pattern[key] for key in sorted(best_by_pattern)]
    write_csv(arguments.pattern_summary, pattern_rows, pattern_fields)

    print(f"wrote {len(rows)} rows to {arguments.output}")
    print(f"wrote {len(summary_rows)} best configurations to {arguments.summary}")
    print(f"wrote {len(pattern_rows)} pattern winners to {arguments.pattern_summary}")
    if failed:
        print(f"{failed} runs failed")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
