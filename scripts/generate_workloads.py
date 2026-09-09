#!/usr/bin/env python3

from __future__ import annotations

import argparse
import bisect
import math
import random
from collections.abc import Callable
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]


def positive_integer(value: str) -> int:
    parsed = int(value)
    if parsed <= 0:
        raise argparse.ArgumentTypeError("value must be positive")
    return parsed


def loop_pattern(
    rng: random.Random, request_count: int, total_capacity: int, key_space: int
) -> list[int]:
    working_set = max(1, total_capacity + 1)
    start = rng.randrange(working_set)
    return [(start + index) % working_set for index in range(request_count)]


def scan_pattern(
    rng: random.Random, request_count: int, total_capacity: int, key_space: int
) -> list[int]:
    del total_capacity
    start = rng.randrange(key_space)
    return [(start + index) % key_space for index in range(request_count)]


def uniform_pattern(
    rng: random.Random, request_count: int, total_capacity: int, key_space: int
) -> list[int]:
    del total_capacity
    return [rng.randrange(key_space) for _ in range(request_count)]


def normal_pattern(
    rng: random.Random, request_count: int, total_capacity: int, key_space: int
) -> list[int]:
    del total_capacity
    mean = (key_space - 1) / 2
    deviation = max(1.0, key_space / 6)
    requests = []
    for _ in range(request_count):
        key = round(rng.gauss(mean, deviation))
        requests.append(min(key_space - 1, max(0, key)))
    return requests


def hotset_pattern(
    rng: random.Random, request_count: int, total_capacity: int, key_space: int
) -> list[int]:
    hot_size = max(1, total_capacity // 4)
    cold_start = min(hot_size, key_space - 1)
    requests = []
    for _ in range(request_count):
        if rng.random() < 0.8:
            requests.append(rng.randrange(hot_size))
        else:
            requests.append(rng.randrange(cold_start, key_space))
    return requests


def hot_scan_pattern(
    rng: random.Random, request_count: int, total_capacity: int, key_space: int
) -> list[int]:
    hot_size = max(1, total_capacity // 4)
    scan_space = max(1, key_space - hot_size)
    block_size = max(16, total_capacity * 2)
    scan_position = 0
    requests = []

    for index in range(request_count):
        in_hot_block = (index // block_size) % 2 == 0
        if in_hot_block:
            requests.append(rng.randrange(hot_size))
        else:
            requests.append(hot_size + scan_position % scan_space)
            scan_position += 1
    return requests


def phase_change_pattern(
    rng: random.Random, request_count: int, total_capacity: int, key_space: int
) -> list[int]:
    phase_count = 4
    phase_length = max(1, math.ceil(request_count / phase_count))
    working_set = max(1, total_capacity + 1)
    phase_offsets = [rng.randrange(working_set) for _ in range(phase_count)]
    requests = []

    for index in range(request_count):
        phase = index // phase_length
        first_key = (phase * working_set) % key_space
        position_in_phase = index % phase_length
        offset = phase_offsets[min(phase, phase_count - 1)]
        requests.append(
            (first_key + (offset + position_in_phase) % working_set) % key_space
        )
    return requests


def zipf_pattern(
    rng: random.Random, request_count: int, total_capacity: int, key_space: int
) -> list[int]:
    del total_capacity
    exponent = 1.1
    weights = [1.0 / (rank**exponent) for rank in range(1, key_space + 1)]
    cumulative = []
    total = 0.0
    for weight in weights:
        total += weight
        cumulative.append(total)

    return [
        bisect.bisect_left(cumulative, rng.random() * total)
        for _ in range(request_count)
    ]


PATTERNS: dict[str, Callable[[random.Random, int, int, int], list[int]]] = {
    "loop": loop_pattern,
    "scan": scan_pattern,
    "uniform": uniform_pattern,
    "normal": normal_pattern,
    "hotset": hotset_pattern,
    "hot_scan": hot_scan_pattern,
    "phase_change": phase_change_pattern,
    "zipf": zipf_pattern,
}


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate categorized synthetic cache workloads."
    )
    parser.add_argument(
        "--patterns",
        nargs="+",
        default=["all"],
        choices=("all", *PATTERNS),
        help="patterns to generate (default: all)",
    )
    parser.add_argument(
        "--cache-sizes",
        nargs="+",
        type=positive_integer,
        default=[16, 64],
        help="cache sizes stored in trace headers",
    )
    parser.add_argument(
        "--levels",
        type=positive_integer,
        default=3,
        help="hierarchy levels used to size working sets (default: 3)",
    )
    parser.add_argument(
        "--requests",
        type=positive_integer,
        default=10_000,
        help="requests per workload (default: 10000)",
    )
    parser.add_argument(
        "--seeds",
        nargs="+",
        type=int,
        default=[1, 2, 3],
        help="random seeds (default: 1 2 3)",
    )
    parser.add_argument(
        "--key-space-multiplier",
        type=positive_integer,
        default=8,
        help="key-space size relative to cache size (default: 8)",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=PROJECT_ROOT / "workloads" / "generated",
        help="output root",
    )
    parser.add_argument(
        "--clean",
        action="store_true",
        help="remove existing .trace files from the output tree first",
    )
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    selected_patterns = list(PATTERNS) if "all" in arguments.patterns else arguments.patterns
    largest_working_set = max(arguments.cache_sizes) * arguments.levels + 1
    if "loop" in selected_patterns and arguments.requests < largest_working_set:
        raise SystemExit(
            "loop requires at least total_capacity + 1 requests to exceed the hierarchy"
        )
    if (
        "phase_change" in selected_patterns
        and math.ceil(arguments.requests / 4) < largest_working_set
    ):
        raise SystemExit(
            "each phase_change phase requires at least total_capacity + 1 requests"
        )

    if arguments.clean and arguments.output.exists():
        for old_workload in arguments.output.rglob("*.trace"):
            old_workload.unlink()

    generated = 0
    for pattern_name in selected_patterns:
        pattern_directory = arguments.output / pattern_name
        pattern_directory.mkdir(parents=True, exist_ok=True)
        generator = PATTERNS[pattern_name]

        for cache_size in arguments.cache_sizes:
            total_capacity = cache_size * arguments.levels
            key_space = max(
                total_capacity + 2,
                total_capacity * arguments.key_space_multiplier,
            )
            for seed in arguments.seeds:
                derived_seed = (
                    seed * 1_000_003
                    + cache_size * 101
                    + arguments.levels * 10_007
                    + sum(ord(character) for character in pattern_name)
                )
                requests = generator(
                    random.Random(derived_seed),
                    arguments.requests,
                    total_capacity,
                    key_space,
                )
                filename = (
                    f"{pattern_name}_l{arguments.levels}_c{cache_size}"
                    f"_n{arguments.requests}_s{seed}.trace"
                )
                contents = (
                    f"{cache_size} {len(requests)}\n"
                    + " ".join(map(str, requests))
                    + "\n"
                )
                (pattern_directory / filename).write_text(contents, encoding="utf-8")
                generated += 1

    print(f"generated {generated} workloads in {arguments.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
