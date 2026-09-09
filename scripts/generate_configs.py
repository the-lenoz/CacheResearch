#!/usr/bin/env python3

from __future__ import annotations

import argparse
import itertools
import re
from pathlib import Path


SUPPORTED_POLICIES = ("LRU", "LFU", "2Q", "ARC", "LIRS")
PROJECT_ROOT = Path(__file__).resolve().parents[1]


def positive_integer(value: str) -> int:
    parsed = int(value)
    if parsed <= 0:
        raise argparse.ArgumentTypeError("value must be positive")
    return parsed


def slug(policy: str) -> str:
    return re.sub(r"[^a-z0-9]+", "_", policy.lower()).strip("_")


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate every ordered cache-policy configuration."
    )
    parser.add_argument(
        "--levels",
        type=positive_integer,
        default=3,
        help="number of cache levels (default: 3)",
    )
    parser.add_argument(
        "--policies",
        nargs="+",
        default=list(SUPPORTED_POLICIES),
        help="policies to combine (default: all online policies)",
    )
    parser.add_argument(
        "--output",
        type=Path,
        help="output directory (default: configs/generated/<levels>-level)",
    )
    parser.add_argument(
        "--without-repetition",
        action="store_true",
        help="do not use the same policy more than once in a config",
    )
    parser.add_argument(
        "--clean",
        action="store_true",
        help="remove existing .conf files from the output directory first",
    )
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    policies = [policy.upper() for policy in arguments.policies]
    unsupported = sorted(set(policies) - set(SUPPORTED_POLICIES))
    if unsupported:
        raise SystemExit(f"unsupported policies: {', '.join(unsupported)}")
    if len(set(policies)) != len(policies):
        raise SystemExit("the policy list must not contain duplicates")
    if arguments.without_repetition and arguments.levels > len(policies):
        raise SystemExit("there are fewer policies than requested unique levels")

    output = arguments.output or (
        PROJECT_ROOT / "configs" / "generated" / f"{arguments.levels}-level"
    )
    output.mkdir(parents=True, exist_ok=True)

    if arguments.clean:
        for old_config in output.glob("*.conf"):
            old_config.unlink()

    combinations = (
        itertools.permutations(policies, arguments.levels)
        if arguments.without_repetition
        else itertools.product(policies, repeat=arguments.levels)
    )

    generated = 0
    for combination in combinations:
        filename = "_".join(slug(policy) for policy in combination) + ".conf"
        contents = "\n".join((str(arguments.levels), *combination)) + "\n"
        (output / filename).write_text(contents, encoding="utf-8")
        generated += 1

    print(f"generated {generated} configs in {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
