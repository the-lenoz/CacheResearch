module;

#include <functional>
#include <variant>

export module cache.variant;

import cache.arc;
import cache.lfu;
import cache.lirs;
import cache.lru;
import cache.two_q;

export template <
    typename KeyType,
    typename ValueType,
    typename Hash = std::hash<KeyType>,
    typename KeyEqual = std::equal_to<KeyType>
>
using CacheVariant = std::variant<
    LRUCache<KeyType, ValueType, Hash, KeyEqual>,
    LFUCache<KeyType, ValueType, Hash, KeyEqual>,
    TwoQCache<KeyType, ValueType, Hash, KeyEqual>,
    ARCCache<KeyType, ValueType, Hash, KeyEqual>,
    LIRSCache<KeyType, ValueType, Hash, KeyEqual>
>;
