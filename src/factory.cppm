module;

#include <cstddef>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

export module cache.factory;

import cache;
import cache.arc;
import cache.lfu;
import cache.lirs;
import cache.lru;
import cache.two_q;

export enum class CapacityAccounting {
    resident_only,
    resident_and_shadow
};

export struct CacheCapacityPlan {
    std::size_t resident_items;
    std::size_t shadow_items;
};

namespace cache_factory_detail {

template <typename KeyType, typename ValueType>
[[nodiscard]] std::size_t default_shadow_items(
    std::string_view policy,
    std::size_t resident_items
) {
    if (policy == "LRU" || policy == "LFU") {
        return 0;
    }
    if (policy == "2Q") {
        return TwoQCache<KeyType, ValueType>::default_shadow_capacity(
            resident_items
        );
    }
    if (policy == "ARC") {
        return ARCCache<KeyType, ValueType>::default_shadow_capacity(
            resident_items
        );
    }
    if (policy == "LIRS") {
        return LIRSCache<KeyType, ValueType>::default_shadow_capacity(
            resident_items
        );
    }
    throw std::invalid_argument(
        "unsupported online cache policy: " + std::string{policy}
    );
}

} // namespace cache_factory_detail

export template <typename KeyType, typename ValueType>
[[nodiscard]] CacheCapacityPlan plan_cache_capacity(
    std::string_view policy,
    std::size_t configured_capacity,
    CapacityAccounting accounting,
    std::size_t logical_value_bytes = sizeof(ValueType)
) {
    const std::size_t default_shadow =
        cache_factory_detail::default_shadow_items<KeyType, ValueType>(
            policy,
            configured_capacity
        );
    if (accounting == CapacityAccounting::resident_only || default_shadow == 0) {
        return {configured_capacity, default_shadow};
    }
    if (configured_capacity == 0) {
        return {0, 0};
    }

    if (logical_value_bytes == 0) {
        throw std::invalid_argument("logical value size must be positive");
    }
    if (logical_value_bytes
        > std::numeric_limits<std::size_t>::max() - sizeof(KeyType)) {
        throw std::overflow_error("logical resident item size overflows size_t");
    }
    const std::size_t resident_item_bytes = sizeof(KeyType) + logical_value_bytes;
    constexpr std::size_t shadow_item_bytes = sizeof(KeyType);
    if (configured_capacity
        > std::numeric_limits<std::size_t>::max() / resident_item_bytes) {
        throw std::overflow_error("cache capacity byte budget overflows size_t");
    }
    const std::size_t byte_budget = configured_capacity * resident_item_bytes;

    const auto fits_budget = [
        byte_budget,
        resident_item_bytes,
        shadow_item_bytes
    ](
        std::size_t resident_items,
        std::size_t shadow_items
    ) {
        if (resident_items > byte_budget / resident_item_bytes) {
            return false;
        }
        const std::size_t resident_bytes = resident_items * resident_item_bytes;
        return shadow_items <= (byte_budget - resident_bytes) / shadow_item_bytes;
    };

    std::size_t lower = 0;
    std::size_t upper = configured_capacity;
    while (lower < upper) {
        const std::size_t middle = lower + (upper - lower + 1) / 2;
        const std::size_t shadow =
            cache_factory_detail::default_shadow_items<KeyType, ValueType>(
                policy,
                middle
            );
        if (fits_budget(middle, shadow)) {
            lower = middle;
        } else {
            upper = middle - 1;
        }
    }

    // A one-item budget cannot also hold the minimum one-key history. Keeping
    // the resident item is more useful than creating a history-only cache.
    if (lower == 0) {
        return {1, 0};
    }
    return {
        lower,
        cache_factory_detail::default_shadow_items<KeyType, ValueType>(
            policy,
            lower
        )
    };
}

export template <
    typename KeyType,
    typename ValueType,
    typename Hash = std::hash<KeyType>,
    typename KeyEqual = std::equal_to<KeyType>
>
[[nodiscard]] std::unique_ptr<Cache<KeyType, ValueType>> make_cache(
    std::string_view policy,
    std::size_t capacity,
    std::optional<std::size_t> shadow_capacity = std::nullopt
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
    if (policy == "2Q") {
        if (shadow_capacity) {
            return std::make_unique<TwoQCache<KeyType, ValueType, Hash, KeyEqual>>(
                capacity,
                *shadow_capacity
            );
        }
        return std::make_unique<TwoQCache<KeyType, ValueType, Hash, KeyEqual>>(
            capacity
        );
    }
    if (policy == "ARC") {
        if (shadow_capacity) {
            return std::make_unique<ARCCache<KeyType, ValueType, Hash, KeyEqual>>(
                capacity,
                *shadow_capacity
            );
        }
        return std::make_unique<ARCCache<KeyType, ValueType, Hash, KeyEqual>>(
            capacity
        );
    }
    if (policy == "LIRS") {
        if (shadow_capacity) {
            return std::make_unique<LIRSCache<KeyType, ValueType, Hash, KeyEqual>>(
                capacity,
                *shadow_capacity
            );
        }
        return std::make_unique<LIRSCache<KeyType, ValueType, Hash, KeyEqual>>(
            capacity
        );
    }

    throw std::invalid_argument("unsupported online cache policy: " + std::string{policy});
}

export template <
    typename KeyType,
    typename ValueType,
    typename Hash = std::hash<KeyType>,
    typename KeyEqual = std::equal_to<KeyType>
>
[[nodiscard]] std::unique_ptr<Cache<KeyType, ValueType>> make_cache(
    std::string_view policy,
    std::size_t configured_capacity,
    CapacityAccounting accounting,
    std::size_t logical_value_bytes = sizeof(ValueType)
) {
    if (accounting == CapacityAccounting::resident_only) {
        return make_cache<KeyType, ValueType, Hash, KeyEqual>(
            policy,
            configured_capacity
        );
    }

    const CacheCapacityPlan plan = plan_cache_capacity<KeyType, ValueType>(
        policy,
        configured_capacity,
        accounting,
        logical_value_bytes
    );
    return make_cache<KeyType, ValueType, Hash, KeyEqual>(
        policy,
        plan.resident_items,
        plan.shadow_items
    );
}
