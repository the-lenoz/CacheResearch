# CacheResearch

A C++20 modules project for researching cache replacement policies.

Implemented policies:

- LRU;
- LFU with LRU tie-breaking;
- 2Q;
- ARC;
- LIRS;
- Belady optimal offline cache.

Online policies can be combined into an exclusive multi-level hierarchy. Cache
interfaces are templated by key and value type, while the CLI simulator uses
`int` keys and an empty payload.

## Build and run

```bash
cmake --preset debug
cmake --build --preset debug
printf '2 6\n1 2 3 1 2 3\n' | ./build/debug/cache_sim configs/belady.conf
```

The program prints the total number of cache hits:

```text
2
```

Use a policy config from `configs/`, including multi-level configurations, to
run online policies through the same command.

Input format:

```text
<cache_size> <request_count>
<request_1> ... <request_n>
```

## Tests

```bash
cmake --preset debug-tests
cmake --build --preset debug-tests
ctest --preset debug-tests
```

## Workload generation and benchmarks

Generate all `5^3 = 125` ordered three-level configurations (repeated policies
are allowed):

```bash
python3 scripts/generate_configs.py --clean
```

Generate workloads grouped by pattern under `workloads/generated/`:

```bash
python3 scripts/generate_workloads.py --clean
```

Available patterns are `loop`, `scan`, `uniform`, `normal`, `hotset`,
`hot_scan`, `phase_change`, and `zipf`. Cache sizes, request counts, seeds, and
selected patterns can be changed through command-line options. The generator
assumes three hierarchy levels by default; pass matching `--levels` when
benchmarking another depth. `loop` and every `phase_change` phase use a working
set of `cache_size * levels + 1`, so the complete hierarchy cannot contain it.
Use `--help` for the full option list.

Run every generated configuration against every workload:

```bash
python3 scripts/benchmark.py
```

Detailed results are written to `results/benchmark.csv`; the best configuration
for each workload is written to `results/best_by_workload.csv`, and the best
aggregate configuration for each pattern is written to
`results/best_by_pattern.csv`. The benchmark also runs Belady with total
hierarchy capacity (`level_count * cache_size`) and reports each configuration's
percentage of the ideal hit count.

The complete pipeline is also available as a CMake target:

```bash
cmake --build --preset release --target benchmark
```

## Shadow-history limits

2Q, ARC, and LIRS accept `max_shadow_items` as the second constructor argument.
Their common `Cache` interface exposes `shadow_size()` and `shadow_capacity()`;
LRU and LFU report zero for both. The factory accepts the same limit as its
optional third argument. This allows a caller to enforce a future combined
budget by choosing `resident_capacity + shadow_capacity <= total_budget`.

Current policy defaults are explicitly marked with `TODO(tuning)` next to the
hard-coded heuristics: A1in/A1out shares in 2Q, ARC history/recency settings,
and resident-HIR/history settings in LIRS.
