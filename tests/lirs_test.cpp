#include <gtest/gtest.h>

#include <string>

import cache;
import cache.lirs;

TEST(LIRSCache, PromotesAReusedHIRBlockAndDemotesTheOldLIRBlock) {
    LIRSCache<int, std::string> cache{2};

    static_cast<void>(cache.insert({1, "one"}));
    static_cast<void>(cache.insert({2, "two"}));
    cache.touch(2);

    const auto evicted = cache.insert({3, "three"});

    ASSERT_TRUE(evicted.has_value());
    EXPECT_EQ(evicted->key, 1);
    EXPECT_TRUE(cache.contains(2));
    EXPECT_TRUE(cache.contains(3));
}

TEST(LIRSCache, RecognizesAReusedNonResidentHIRBlock) {
    LIRSCache<int, std::string> cache{2};

    static_cast<void>(cache.insert({1, "one"}));
    static_cast<void>(cache.insert({2, "two"}));
    const auto first_eviction = cache.insert({3, "three"});
    const auto second_eviction = cache.insert({2, "two again"});

    ASSERT_TRUE(first_eviction.has_value());
    EXPECT_EQ(first_eviction->key, 2);
    ASSERT_TRUE(second_eviction.has_value());
    EXPECT_EQ(second_eviction->key, 3);
    EXPECT_TRUE(cache.contains(1));
    EXPECT_TRUE(cache.contains(2));
    ASSERT_NE(cache.find(2), nullptr);
    EXPECT_EQ(*cache.find(2), "two again");
}

TEST(LIRSCache, ExtractsValuesAndHandlesZeroCapacity) {
    LIRSCache<int, std::string> cache{1};
    static_cast<void>(cache.insert({1, "one"}));

    const auto extracted = cache.extract(1);
    ASSERT_TRUE(extracted.has_value());
    EXPECT_EQ(extracted->value, "one");
    EXPECT_EQ(cache.size(), 0);

    LIRSCache<int, std::string> empty{0};
    EXPECT_TRUE(empty.insert({2, "two"}).has_value());
    EXPECT_EQ(empty.size(), 0);
}

TEST(LIRSCache, LimitsNonResidentHIRHistory) {
    LIRSCache<int, std::string> cache{2, 1};

    for (int key = 1; key <= 8; ++key) {
        static_cast<void>(cache.insert({key, std::to_string(key)}));
        EXPECT_LE(cache.shadow_size(), 1);
    }

    EXPECT_EQ(cache.shadow_capacity(), 1);

    LIRSCache<int, std::string> without_shadow{2, 0};
    for (int key = 1; key <= 4; ++key) {
        static_cast<void>(without_shadow.insert({key, std::to_string(key)}));
    }
    EXPECT_EQ(without_shadow.shadow_size(), 0);
}

TEST(LIRSCache, ConstructorsExposeDefaultAndExplicitShadowCapacities) {
    const LIRSCache<int, std::string> zero{0};
    const LIRSCache<int, std::string> default_limit{8};
    const LIRSCache<int, std::string> explicit_limit{8, 11};

    EXPECT_EQ(zero.capacity(), 0);
    EXPECT_EQ(zero.shadow_capacity(), 0);
    EXPECT_EQ(default_limit.capacity(), 8);
    EXPECT_EQ(default_limit.shadow_capacity(), 8);
    EXPECT_EQ(explicit_limit.capacity(), 8);
    EXPECT_EQ(explicit_limit.shadow_capacity(), 11);
}

TEST(LIRSCache, FindDoesNotPromoteAResidentHIRBlock) {
    LIRSCache<int, std::string> cache{2};
    static_cast<void>(cache.insert({1, "one"}));
    static_cast<void>(cache.insert({2, "two"}));

    ASSERT_NE(cache.find(2), nullptr);
    const LIRSCache<int, std::string>& const_cache = cache;
    ASSERT_NE(const_cache.find(2), nullptr);
    const auto evicted = cache.insert({3, "three"});

    ASSERT_TRUE(evicted.has_value());
    EXPECT_EQ(evicted->key, 2);
}

TEST(LIRSCache, DuplicateInsertUsesTheResidentHitPath) {
    LIRSCache<int, std::string> cache{2};
    static_cast<void>(cache.insert({1, "one"}));
    static_cast<void>(cache.insert({2, "old"}));

    EXPECT_FALSE(cache.insert({2, "new"}).has_value());
    const auto evicted = cache.insert({3, "three"});

    ASSERT_TRUE(evicted.has_value());
    EXPECT_EQ(evicted->key, 1);
    ASSERT_NE(cache.find(2), nullptr);
    EXPECT_EQ(*cache.find(2), "new");
}

TEST(LIRSCache, ExtractDoesNotLeaveShadowHistory) {
    LIRSCache<int, std::string> cache{2, 4};
    static_cast<void>(cache.insert({1, "one"}));
    static_cast<void>(cache.insert({2, "two"}));

    const auto hir = cache.extract(2);
    const auto lir = cache.extract(1);

    ASSERT_TRUE(hir.has_value());
    ASSERT_TRUE(lir.has_value());
    EXPECT_EQ(cache.size(), 0);
    EXPECT_EQ(cache.shadow_size(), 0);
}

TEST(LIRSCache, TouchingANonResidentHIRBlockIsANoOp) {
    LIRSCache<int, std::string> cache{2, 4};
    static_cast<void>(cache.insert({1, "one"}));
    static_cast<void>(cache.insert({2, "two"}));
    static_cast<void>(cache.insert({3, "three"}));
    ASSERT_EQ(cache.shadow_size(), 1);

    cache.touch(2);

    EXPECT_EQ(cache.size(), 2);
    EXPECT_EQ(cache.shadow_size(), 1);
    EXPECT_EQ(cache.find(2), nullptr);
}

TEST(LIRSCache, CapacityOneUsesOnlyResidentHIRWithoutHistory) {
    LIRSCache<int, std::string> cache{1, 4};
    static_cast<void>(cache.insert({1, "one"}));

    const auto first = cache.insert({2, "two"});
    const auto second = cache.insert({3, "three"});

    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(first->key, 1);
    EXPECT_EQ(second->key, 2);
    EXPECT_EQ(cache.size(), 1);
    EXPECT_EQ(cache.shadow_size(), 0);
}

TEST(LIRSCache, ClearRemovesNonResidentHIRHistory) {
    LIRSCache<int, std::string> cache{2, 4};
    static_cast<void>(cache.insert({1, "one"}));
    static_cast<void>(cache.insert({2, "two"}));
    static_cast<void>(cache.insert({3, "three"}));
    ASSERT_GT(cache.shadow_size(), 0);

    cache.clear();

    EXPECT_EQ(cache.size(), 0);
    EXPECT_EQ(cache.shadow_size(), 0);
    EXPECT_FALSE(cache.insert({1, "fresh"}).has_value());
    ASSERT_NE(cache.find(1), nullptr);
    EXPECT_EQ(*cache.find(1), "fresh");
}
