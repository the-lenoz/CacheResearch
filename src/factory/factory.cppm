module;

#include <cstddef>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

export module cache.factory;

import cache;
import cache.lfu;
import cache.lru;

export template <
    typename KeyType,
    typename ValueType,
    typename Hash = std::hash<KeyType>,
    typename KeyEqual = std::equal_to<KeyType>
>
[[nodiscard]] std::unique_ptr<Cache<KeyType, ValueType>> make_cache(
    std::string_view policy,
    std::size_t capacity
) {
    if (policy == "LRU") {
        return std::make_unique<LRUCache<KeyType, ValueType, Hash, KeyEqual>>(
            capacity
        );
    }
    if (policy == "LFU") {
        return std::make_unique<LFUCache<KeyType, ValueType, Hash, KeyEqual>>(
            capacity
        );
    }

    throw std::invalid_argument("unsupported online cache policy: " + std::string{policy});
}
