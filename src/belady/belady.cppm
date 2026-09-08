module;

#include <cstddef>
#include <vector>

export module cache.belady;

import cache;

export class BeladyCache {
public:
    BeladyCache(std::size_t capacity, const std::vector<Key>& trace);

    [[nodiscard]] std::size_t run();
};
