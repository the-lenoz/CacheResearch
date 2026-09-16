#include <gtest/gtest.h>

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

import cache;
import cache.arc;
import cache.lfu;
import cache.lirs;
import cache.lru;
import cache.two_q;

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

template <typename CacheType>
void expect_move_only_payload_support() {
    CacheType cache{1};
    EXPECT_FALSE(cache.insert({1, std::make_unique<int>(7)}).has_value());

    auto* stored = cache.find(1);
    ASSERT_NE(stored, nullptr);
    ASSERT_NE(*stored, nullptr);
    EXPECT_EQ(**stored, 7);

    const auto evicted = cache.insert({2, std::make_unique<int>(9)});
    ASSERT_TRUE(evicted.has_value());
    ASSERT_NE(evicted->value, nullptr);
    EXPECT_EQ(*evicted->value, 7);
}

template <typename CacheType>
void expect_custom_key_support() {
    CacheType cache{2};
    EXPECT_FALSE(cache.insert({OpaqueKey{1}, "one"}).has_value());
    EXPECT_FALSE(cache.insert({OpaqueKey{2}, "two"}).has_value());

    ASSERT_NE(cache.find(OpaqueKey{1}), nullptr);
    EXPECT_EQ(*cache.find(OpaqueKey{1}), "one");
    const auto extracted = cache.extract(OpaqueKey{2});
    ASSERT_TRUE(extracted.has_value());
    EXPECT_EQ(extracted->key.id, 2);
}

} // namespace

TEST(CacheEntry, StoresTypedPayload) {
    const CacheEntry<int, std::string> entry{42, "payload"};

    EXPECT_EQ(entry.key, 42);
    EXPECT_EQ(entry.value, "payload");
}

template <typename CacheType>
class OnlineCacheContractTest : public ::testing::Test {};

using OnlineCacheImplementations = ::testing::Types<
    LRUCache<int, std::string>,
    LFUCache<int, std::string>,
    TwoQCache<int, std::string>,
    ARCCache<int, std::string>,
    LIRSCache<int, std::string>
>;

TYPED_TEST_SUITE(OnlineCacheContractTest, OnlineCacheImplementations);

TYPED_TEST(OnlineCacheContractTest, ConstructorCreatesAnEmptyCache) {
    TypeParam cache{3};

    EXPECT_EQ(cache.capacity(), 3);
    EXPECT_EQ(cache.size(), 0);
    EXPECT_EQ(cache.shadow_size(), 0);
    EXPECT_LE(cache.shadow_size(), cache.shadow_capacity());
    EXPECT_EQ(cache.find(1), nullptr);

    const TypeParam& const_cache = cache;
    EXPECT_EQ(const_cache.find(1), nullptr);
}

TYPED_TEST(OnlineCacheContractTest, ZeroCapacityReturnsTheInputEntryUnchanged) {
    TypeParam cache{0};

    const auto evicted = cache.insert({7, "seven"});

    ASSERT_TRUE(evicted.has_value());
    EXPECT_EQ(evicted->key, 7);
    EXPECT_EQ(evicted->value, "seven");
    EXPECT_EQ(cache.capacity(), 0);
    EXPECT_EQ(cache.size(), 0);
    EXPECT_EQ(cache.shadow_size(), 0);
    EXPECT_EQ(cache.find(7), nullptr);
}

TYPED_TEST(OnlineCacheContractTest, MissingKeyOperationsAreNoOps) {
    TypeParam cache{2};
    EXPECT_FALSE(cache.insert({1, "one"}).has_value());

    cache.touch(99);
    cache.erase(99);
    EXPECT_FALSE(cache.extract(99).has_value());

    EXPECT_EQ(cache.size(), 1);
    ASSERT_NE(cache.find(1), nullptr);
    EXPECT_EQ(*cache.find(1), "one");
}

TYPED_TEST(OnlineCacheContractTest, DuplicateInsertUpdatesWithoutGrowingOrEvicting) {
    TypeParam cache{2};
    EXPECT_FALSE(cache.insert({1, "old"}).has_value());
    EXPECT_FALSE(cache.insert({2, "two"}).has_value());

    const auto evicted = cache.insert({1, "new"});

    EXPECT_FALSE(evicted.has_value());
    EXPECT_EQ(cache.size(), 2);
    ASSERT_NE(cache.find(1), nullptr);
    EXPECT_EQ(*cache.find(1), "new");
    EXPECT_LE(cache.shadow_size(), cache.shadow_capacity());
}

TYPED_TEST(OnlineCacheContractTest, ClearRemovesAllStateAndAllowsReuse) {
    TypeParam cache{2};
    for (int key = 1; key <= 8; ++key) {
        static_cast<void>(cache.insert({key, std::to_string(key)}));
    }

    cache.clear();

    EXPECT_EQ(cache.size(), 0);
    EXPECT_EQ(cache.shadow_size(), 0);
    for (int key = 1; key <= 8; ++key) {
        EXPECT_EQ(cache.find(key), nullptr);
    }

    EXPECT_FALSE(cache.insert({42, "reused"}).has_value());
    ASSERT_NE(cache.find(42), nullptr);
    EXPECT_EQ(*cache.find(42), "reused");
}

TYPED_TEST(OnlineCacheContractTest, ReportedVictimsKeepMembershipConsistent) {
    TypeParam cache{3};
    std::unordered_map<int, std::string> expected;

    for (int step = 0; step < 40; ++step) {
        const int key = (step * 7) % 11;
        const std::string value = "value-" + std::to_string(step);
        const bool already_resident = expected.contains(key);
        const auto evicted = cache.insert({key, value});

        if (already_resident) {
            EXPECT_FALSE(evicted.has_value());
        } else if (evicted) {
            EXPECT_EQ(expected.erase(evicted->key), 1);
        }
        expected.insert_or_assign(key, value);

        cache.touch((step * 5) % 13);
        if (step % 6 == 5) {
            const int extracted_key = (step * 3) % 11;
            const auto extracted = cache.extract(extracted_key);
            EXPECT_EQ(extracted.has_value(), expected.erase(extracted_key) == 1);
        }

        EXPECT_EQ(cache.size(), expected.size());
        EXPECT_LE(cache.size(), cache.capacity());
        EXPECT_LE(cache.shadow_size(), cache.shadow_capacity());
        for (int candidate = 0; candidate < 11; ++candidate) {
            const auto found = expected.find(candidate);
            const auto* stored = cache.find(candidate);
            ASSERT_EQ(stored != nullptr, found != expected.end());
            if (stored) {
                EXPECT_EQ(*stored, found->second);
            }
        }
    }
}

TEST(OnlineCacheContract, SupportsMoveOnlyPayloads) {
    expect_move_only_payload_support<LRUCache<int, std::unique_ptr<int>>>();
    expect_move_only_payload_support<LFUCache<int, std::unique_ptr<int>>>();
    expect_move_only_payload_support<TwoQCache<int, std::unique_ptr<int>>>();
    expect_move_only_payload_support<ARCCache<int, std::unique_ptr<int>>>();
    expect_move_only_payload_support<LIRSCache<int, std::unique_ptr<int>>>();
}

TEST(OnlineCacheContract, SupportsCustomHashAndEquality) {
    expect_custom_key_support<
        LRUCache<OpaqueKey, std::string, OpaqueHash, OpaqueEqual>
    >();
    expect_custom_key_support<
        LFUCache<OpaqueKey, std::string, OpaqueHash, OpaqueEqual>
    >();
    expect_custom_key_support<
        TwoQCache<OpaqueKey, std::string, OpaqueHash, OpaqueEqual>
    >();
    expect_custom_key_support<
        ARCCache<OpaqueKey, std::string, OpaqueHash, OpaqueEqual>
    >();
    expect_custom_key_support<
        LIRSCache<OpaqueKey, std::string, OpaqueHash, OpaqueEqual>
    >();
}
