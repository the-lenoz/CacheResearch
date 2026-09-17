# CacheResearch

A project for researching cache replacement policies.

Implemented policies:

- LRU;
- LFU with LRU tie-breaking;
- 2Q;
- ARC;
- LIRS;
- Belady optimal offline cache.

Online policies can be combined into an exclusive multi-level hierarchy. Cache
interfaces are templated by key and value type, while the CLI simulator uses
`int` keys and byte-array pages loaded by a deterministic mock database.

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

By default, the configured capacity limits resident items and shadow history is
additional. Use a shared logical byte budget for resident and shadow items with:

```bash
printf '16 6\n1 2 3 1 2 3\n' \
  | ./build/debug/cache_sim --capacity-includes-shadow configs/arc.conf
```

Input format:

```text
<cache_size> <request_count>
<request_1> ... <request_n>
```

## Hierarchy API

Pass a `slow_get_page(key) -> Value` functor when constructing the hierarchy.
`access(key)` promotes a hit to L1; on a full miss it calls the functor once and
fills the cache. The result exposes `hit()` and `value()`. Explicit `insert(entry)`
remains available for preloading or replacing a value without calling the loader:

```cpp
auto result = hierarchy.access(key);
consume(result.value());
record_hit(result.hit());
```

`find(key)` only observes the currently stored value. It does not count a hit,
change replacement-policy state, or promote an entry. References to cached values
returned by `access()` and pointers from `find()` are invalid after the next
mutating operation. When every level has zero capacity, the access result owns
the loaded value until that result is destroyed.

The CLI mock database generates the same page for the same key on every miss.
It has no persistent backing store or artificial delay. Page size is 32 bytes
by default; `--value-bytes N` sets the actual page size.

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
percentage of the ideal hit count. Configurations containing 2Q, ARC, or LIRS
are run twice: `resident_only` preserves the legacy resident capacity and
`resident_and_shadow` applies `--capacity-includes-shadow`. The
`capacity_mode` CSV column distinguishes these variants. Use
`--value-bytes current 64 256 1024` to vary the actual page size;
the selected value is written to `value_bytes`. `current` uses the compiled
32-byte default. `--config-names` can restrict a focused experiment to
specific config paths relative to `--configs`.

The complete pipeline is also available as a CMake target:

```bash
cmake --build --preset release --target benchmark
```

## Shadow-history limits

2Q, ARC, and LIRS accept `max_shadow_items` as the second constructor argument.
Each policy exposes `shadow_size()` and `shadow_capacity()`;
LRU and LFU report zero for both. The factory accepts the same limit as its
optional third argument.

`CapacityAccounting::resident_and_shadow` treats the configured capacity as a
logical byte budget of
`capacity * (sizeof(KeyType) + logical_value_bytes)`. It selects the largest
resident capacity that preserves the policy's default shadow ratio while
satisfying:

```text
resident_capacity * (sizeof(KeyType) + logical_value_bytes)
    + shadow_capacity * sizeof(KeyType)
    <= configured logical byte budget
```

This intentionally counts logical key/value payload only, not allocator,
container-node, vector-object, or hash-table overhead. A one-item budget
prioritizes one resident item and disables shadow history. CLI option
`--value-bytes N` changes both the actual mock page size and the logical size
used by this planner.

Current policy defaults are explicitly marked with `TODO(tuning)` next to the
still hard-coded algorithmic heuristics: the A1in share in 2Q, ARC's initial
recency target and history trimming preference, and the resident-HIR share and
history removal strategy in LIRS. Shadow capacity itself is already
configurable and is not marked as pending work.
