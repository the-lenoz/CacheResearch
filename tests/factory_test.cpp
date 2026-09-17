#include <gtest/gtest.h>

#include <cstddef>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

import cache;
import cache.arc;
import cache.factory;
import cache.lfu;
import cache.lirs;
import cache.lru;
import cache.two_q;
import cache.variant;

namespace {
struct OpaqueKey {
    int id;
};

struct OpaqueHash {
    std::size_t operator()(const OpaqueKey& key) const noexcept {
        return std::hash<int>{}(key.id);
    }
};

struct OpaqueEqual {
    bool operator()(const OpaqueKey& left, const OpaqueKey& right) const noexcept {
        return left.id == right.id;
    }
};

template <typename Variant>
std::size_t capacity(const Variant& selected) {
    return std::visit([&](const auto& cache) { return cache.capacity(); }, selected);
}

template <typename Variant>
std::size_t shadow_capacity(const Variant& selected) {
    return std::visit([&](const auto& cache) { return cache.shadow_capacity(); }, selected);
}
}

TEST(CacheFactory, CreatesEveryOnlinePolicy) {
    const auto lru = make_cache<int, std::string>("LRU", 2);
    const auto lfu = make_cache<int, std::string>("LFU", 3);
    const auto two_q = make_cache<int, std::string>("2Q", 4);
    const auto arc = make_cache<int, std::string>("ARC", 5);
    const auto lirs = make_cache<int, std::string>("LIRS", 6);

    EXPECT_TRUE((std::holds_alternative<LRUCache<int, std::string>>(lru)));
    EXPECT_TRUE((std::holds_alternative<LFUCache<int, std::string>>(lfu)));
    EXPECT_TRUE((std::holds_alternative<TwoQCache<int, std::string>>(two_q)));
    EXPECT_TRUE((std::holds_alternative<ARCCache<int, std::string>>(arc)));
    EXPECT_TRUE((std::holds_alternative<LIRSCache<int, std::string>>(lirs)));
    EXPECT_EQ(capacity(lru), 2);
    EXPECT_EQ(capacity(lfu), 3);
    EXPECT_EQ(capacity(two_q), 4);
    EXPECT_EQ(capacity(arc), 5);
    EXPECT_EQ(capacity(lirs), 6);
}

TEST(CacheFactory, VariantPreservesCustomHashAndKeyEquality) {
    auto selected = make_cache<OpaqueKey, std::string, OpaqueHash, OpaqueEqual>(
        "ARC", 2
    );
    EXPECT_TRUE((std::holds_alternative<
        ARCCache<OpaqueKey, std::string, OpaqueHash, OpaqueEqual>>(selected)));
    std::visit([&](auto& cache) {
        EXPECT_FALSE(cache.insert({OpaqueKey{7}, "seven"}).has_value());
        const auto* value = cache.find(OpaqueKey{7});
        ASSERT_NE(value, nullptr);
        EXPECT_EQ(*value, "seven");
    }, selected);
}

TEST(CacheFactory, MovingPopulatedVariantsKeepsPolicyIndexesValid) {
    for (const std::string_view policy : {"LRU", "LFU", "2Q", "ARC", "LIRS"}) {
        std::vector<CacheVariant<int, std::string>> caches;
        caches.reserve(1);
        caches.push_back(make_cache<int, std::string>(policy, 2));
        std::visit([&](auto& cache) {
            EXPECT_FALSE(cache.insert({1, "one"}).has_value());
            EXPECT_FALSE(cache.insert({2, "two"}).has_value());
        }, caches.front());

        caches.push_back(make_cache<int, std::string>("LRU", 1));
        std::visit([&](const auto& cache) {
            ASSERT_NE(cache.find(1), nullptr);
            ASSERT_NE(cache.find(2), nullptr);
            EXPECT_EQ(*cache.find(1), "one");
            EXPECT_EQ(*cache.find(2), "two");
        }, caches.front());
    }
}

TEST(CacheFactory, RejectsUnknownPolicy) {
    EXPECT_THROW(
        (static_cast<void>(make_cache<int, std::string>("UNKNOWN", 2))),
        std::invalid_argument
    );
}

TEST(CacheFactory, PassesExplicitShadowLimitToHistoryBasedPolicies) {
    const auto two_q = make_cache<int, std::string>("2Q", 8, 2);
    const auto arc = make_cache<int, std::string>("ARC", 8, 3);
    const auto lirs = make_cache<int, std::string>("LIRS", 8, 4);

    EXPECT_EQ(shadow_capacity(two_q), 2);
    EXPECT_EQ(shadow_capacity(arc), 3);
    EXPECT_EQ(shadow_capacity(lirs), 4);
}

TEST(CacheFactory, KeepsLegacyDefaultsInResidentOnlyMode) {
    const auto two_q = make_cache<int, std::string>(
        "2Q",
        8,
        CapacityAccounting::resident_only
    );
    const auto arc = make_cache<int, std::string>(
        "ARC",
        8,
        CapacityAccounting::resident_only
    );
    const auto lirs = make_cache<int, std::string>(
        "LIRS",
        8,
        CapacityAccounting::resident_only
    );

    EXPECT_EQ(capacity(two_q), 8);
    EXPECT_EQ(shadow_capacity(two_q), 4);
    EXPECT_EQ(capacity(arc), 8);
    EXPECT_EQ(shadow_capacity(arc), 8);
    EXPECT_EQ(capacity(lirs), 8);
    EXPECT_EQ(shadow_capacity(lirs), 8);
}

TEST(CacheFactory, FitsResidentAndShadowItemsIntoOneLogicalByteBudget) {
    constexpr std::size_t configured_capacity = 16;
    constexpr std::size_t resident_item_bytes = sizeof(int) + sizeof(std::string);
    constexpr std::size_t shadow_item_bytes = sizeof(int);
    constexpr std::size_t byte_budget = configured_capacity * resident_item_bytes;

    const auto two_q = make_cache<int, std::string>(
        "2Q",
        configured_capacity,
        CapacityAccounting::resident_and_shadow
    );
    const auto arc = make_cache<int, std::string>(
        "ARC",
        configured_capacity,
        CapacityAccounting::resident_and_shadow
    );
    const auto lirs = make_cache<int, std::string>(
        "LIRS",
        configured_capacity,
        CapacityAccounting::resident_and_shadow
    );

    const auto expect_fits = [=](const auto& cache) {
        EXPECT_LE(
            capacity(cache) * resident_item_bytes
                + shadow_capacity(cache) * shadow_item_bytes,
            byte_budget
        );
        EXPECT_LT(capacity(cache), configured_capacity);
        EXPECT_GT(shadow_capacity(cache), 0);
    };
    expect_fits(two_q);
    expect_fits(arc);
    expect_fits(lirs);

    EXPECT_EQ(shadow_capacity(two_q), capacity(two_q) / 2);
    EXPECT_EQ(shadow_capacity(arc), capacity(arc));
    EXPECT_EQ(shadow_capacity(lirs), capacity(lirs));

    EXPECT_GT(
        (capacity(two_q) + 1) * resident_item_bytes
            + ((capacity(two_q) + 1) / 2) * shadow_item_bytes,
        byte_budget
    );
    EXPECT_GT(
        (capacity(arc) + 1) * resident_item_bytes
            + (capacity(arc) + 1) * shadow_item_bytes,
        byte_budget
    );
    EXPECT_GT(
        (capacity(lirs) + 1) * resident_item_bytes
            + (capacity(lirs) + 1) * shadow_item_bytes,
        byte_budget
    );
}

TEST(CacheFactory, LeavesPoliciesWithoutShadowHistoryAtFullCapacity) {
    const auto lru = make_cache<int, std::string>(
        "LRU",
        8,
        CapacityAccounting::resident_and_shadow
    );
    const auto lfu = make_cache<int, std::string>(
        "LFU",
        8,
        CapacityAccounting::resident_and_shadow
    );

    EXPECT_EQ(capacity(lru), 8);
    EXPECT_EQ(shadow_capacity(lru), 0);
    EXPECT_EQ(capacity(lfu), 8);
    EXPECT_EQ(shadow_capacity(lfu), 0);
}

TEST(CacheFactory, PreservesOneResidentItemBeforeAllocatingShadowHistory) {
    for (const std::string_view policy : {"2Q", "ARC", "LIRS"}) {
        const auto cache = make_cache<int, std::string>(
            policy,
            1,
            CapacityAccounting::resident_and_shadow
        );

        EXPECT_EQ(capacity(cache), 1);
        EXPECT_EQ(shadow_capacity(cache), 0);
    }
}

TEST(CacheFactory, LargerLogicalValuesReduceRelativeShadowCost) {
    constexpr std::size_t configured_capacity = 64;

    const auto current = plan_cache_capacity<int, DefaultValue>(
        "ARC",
        configured_capacity,
        CapacityAccounting::resident_and_shadow
    );
    const auto bytes64 = plan_cache_capacity<int, DefaultValue>(
        "ARC",
        configured_capacity,
        CapacityAccounting::resident_and_shadow,
        64
    );
    const auto bytes256 = plan_cache_capacity<int, DefaultValue>(
        "ARC",
        configured_capacity,
        CapacityAccounting::resident_and_shadow,
        256
    );
    const auto bytes1024 = plan_cache_capacity<int, DefaultValue>(
        "ARC",
        configured_capacity,
        CapacityAccounting::resident_and_shadow,
        1024
    );

    EXPECT_LE(current.resident_items, bytes64.resident_items);
    EXPECT_LE(bytes64.resident_items, bytes256.resident_items);
    EXPECT_LE(bytes256.resident_items, bytes1024.resident_items);
    EXPECT_EQ(bytes64.resident_items, bytes64.shadow_items);
    EXPECT_EQ(bytes256.resident_items, bytes256.shadow_items);
    EXPECT_EQ(bytes1024.resident_items, bytes1024.shadow_items);

    const auto expect_fits = [=](const CacheCapacityPlan plan, std::size_t value_bytes) {
        const std::size_t resident_bytes = sizeof(int) + value_bytes;
        const std::size_t budget = configured_capacity * resident_bytes;
        EXPECT_LE(
            plan.resident_items * resident_bytes
                + plan.shadow_items * sizeof(int),
            budget
        );
    };
    expect_fits(bytes64, 64);
    expect_fits(bytes256, 256);
    expect_fits(bytes1024, 1024);
}

TEST(CacheFactory, RejectsZeroLogicalValueSize) {
    EXPECT_THROW(
        (static_cast<void>(plan_cache_capacity<int, DefaultValue>(
            "ARC",
            8,
            CapacityAccounting::resident_and_shadow,
            0
        ))),
        std::invalid_argument
    );
}
