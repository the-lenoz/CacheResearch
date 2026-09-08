# CacheResearch

A C++20 modules project for researching cache replacement policies.

Implemented policies:

- LRU;
- LFU with LRU tie-breaking;
- Belady optimal offline cache.

LRU and LFU can be combined into an exclusive multi-level hierarchy. Cache
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

Use `configs/lru.conf`, `configs/lfu.conf`, or a multi-level config such as
`configs/lru_lfu.conf` to run online policies through the same command.

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
