#include <gtest/gtest.h>

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>

import cache;
import cache.factory;

TEST(CacheFactory, CreatesEveryOnlinePolicy) {
    const auto lru = make_cache<int, std::string>("LRU", 2);
    const auto lfu = make_cache<int, std::string>("LFU", 3);
    const auto two_q = make_cache<int, std::string>("2Q", 4);
    const auto arc = make_cache<int, std::string>("ARC", 5);
    const auto lirs = make_cache<int, std::string>("LIRS", 6);

    EXPECT_EQ(lru->capacity(), 2);
    EXPECT_EQ(lfu->capacity(), 3);
    EXPECT_EQ(two_q->capacity(), 4);
    EXPECT_EQ(arc->capacity(), 5);
    EXPECT_EQ(lirs->capacity(), 6);
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

    EXPECT_EQ(two_q->shadow_capacity(), 2);
    EXPECT_EQ(arc->shadow_capacity(), 3);
    EXPECT_EQ(lirs->shadow_capacity(), 4);
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

    EXPECT_EQ(two_q->capacity(), 8);
    EXPECT_EQ(two_q->shadow_capacity(), 4);
    EXPECT_EQ(arc->capacity(), 8);
    EXPECT_EQ(arc->shadow_capacity(), 8);
    EXPECT_EQ(lirs->capacity(), 8);
    EXPECT_EQ(lirs->shadow_capacity(), 8);
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
            cache->capacity() * resident_item_bytes
                + cache->shadow_capacity() * shadow_item_bytes,
            byte_budget
        );
        EXPECT_LT(cache->capacity(), configured_capacity);
        EXPECT_GT(cache->shadow_capacity(), 0);
    };
    expect_fits(two_q);
    expect_fits(arc);
    expect_fits(lirs);

    EXPECT_EQ(two_q->shadow_capacity(), two_q->capacity() / 2);
    EXPECT_EQ(arc->shadow_capacity(), arc->capacity());
    EXPECT_EQ(lirs->shadow_capacity(), lirs->capacity());

    EXPECT_GT(
        (two_q->capacity() + 1) * resident_item_bytes
            + ((two_q->capacity() + 1) / 2) * shadow_item_bytes,
        byte_budget
    );
    EXPECT_GT(
        (arc->capacity() + 1) * resident_item_bytes
            + (arc->capacity() + 1) * shadow_item_bytes,
        byte_budget
    );
    EXPECT_GT(
        (lirs->capacity() + 1) * resident_item_bytes
            + (lirs->capacity() + 1) * shadow_item_bytes,
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

    EXPECT_EQ(lru->capacity(), 8);
    EXPECT_EQ(lru->shadow_capacity(), 0);
    EXPECT_EQ(lfu->capacity(), 8);
    EXPECT_EQ(lfu->shadow_capacity(), 0);
}

TEST(CacheFactory, PreservesOneResidentItemBeforeAllocatingShadowHistory) {
    for (const std::string_view policy : {"2Q", "ARC", "LIRS"}) {
        const auto cache = make_cache<int, std::string>(
            policy,
            1,
            CapacityAccounting::resident_and_shadow
        );

        EXPECT_EQ(cache->capacity(), 1);
        EXPECT_EQ(cache->shadow_capacity(), 0);
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
