#include <gtest/gtest.h>

#include <string>

import cache;
import cache.arc;

TEST(ARCCache, PromotesResidentHitsToTheFrequentList) {
    ARCCache<int, std::string> cache{2};

    static_cast<void>(cache.insert({1, "one"}));
    static_cast<void>(cache.insert({2, "two"}));
    cache.touch(1);

    const auto evicted = cache.insert({3, "three"});

    ASSERT_TRUE(evicted.has_value());
    EXPECT_EQ(evicted->key, 2);
    EXPECT_TRUE(cache.contains(1));
    EXPECT_TRUE(cache.contains(3));
}

TEST(ARCCache, AdaptsAfterARecencyGhostHit) {
    ARCCache<int, std::string> cache{2};

    static_cast<void>(cache.insert({1, "one"}));
    static_cast<void>(cache.insert({2, "two"}));
    cache.touch(1);
    static_cast<void>(cache.insert({3, "three"}));

    const auto evicted = cache.insert({2, "two again"});

    ASSERT_TRUE(evicted.has_value());
    EXPECT_EQ(evicted->key, 1);
    EXPECT_TRUE(cache.contains(2));
    EXPECT_TRUE(cache.contains(3));
    ASSERT_NE(cache.find(2), nullptr);
    EXPECT_EQ(*cache.find(2), "two again");
}

TEST(ARCCache, ExtractsValuesAndHandlesZeroCapacity) {
    ARCCache<int, std::string> cache{1};
    static_cast<void>(cache.insert({1, "one"}));

    const auto extracted = cache.extract(1);
    ASSERT_TRUE(extracted.has_value());
    EXPECT_EQ(extracted->value, "one");
    EXPECT_EQ(cache.size(), 0);

    ARCCache<int, std::string> empty{0};
    EXPECT_TRUE(empty.insert({2, "two"}).has_value());
    EXPECT_EQ(empty.size(), 0);
}

TEST(ARCCache, LimitsCombinedB1AndB2History) {
    ARCCache<int, std::string> cache{2, 1};

    static_cast<void>(cache.insert({1, "one"}));
    static_cast<void>(cache.insert({2, "two"}));
    cache.touch(1);

    for (int key = 3; key <= 8; ++key) {
        static_cast<void>(cache.insert({key, std::to_string(key)}));
        EXPECT_LE(cache.shadow_size(), 1);
    }

    EXPECT_EQ(cache.shadow_capacity(), 1);

    ARCCache<int, std::string> without_shadow{2, 0};
    static_cast<void>(without_shadow.insert({1, "one"}));
    static_cast<void>(without_shadow.insert({2, "two"}));
    without_shadow.touch(1);
    static_cast<void>(without_shadow.insert({3, "three"}));
    EXPECT_EQ(without_shadow.shadow_size(), 0);
}

TEST(ARCCache, ConstructorsExposeDefaultAndExplicitShadowCapacities) {
    const ARCCache<int, std::string> zero{0};
    const ARCCache<int, std::string> default_limit{8};
    const ARCCache<int, std::string> explicit_limit{8, 11};

    EXPECT_EQ(zero.capacity(), 0);
    EXPECT_EQ(zero.shadow_capacity(), 0);
    EXPECT_EQ(default_limit.capacity(), 8);
    EXPECT_EQ(default_limit.shadow_capacity(), 8);
    EXPECT_EQ(explicit_limit.capacity(), 8);
    EXPECT_EQ(explicit_limit.shadow_capacity(), 11);
}

TEST(ARCCache, FindDoesNotPromoteARecentEntry) {
    ARCCache<int, std::string> cache{2};
    static_cast<void>(cache.insert({1, "one"}));
    static_cast<void>(cache.insert({2, "two"}));

    ASSERT_NE(cache.find(1), nullptr);
    const ARCCache<int, std::string>& const_cache = cache;
    ASSERT_NE(const_cache.find(1), nullptr);
    const auto evicted = cache.insert({3, "three"});

    ASSERT_TRUE(evicted.has_value());
    EXPECT_EQ(evicted->key, 1);
}

TEST(ARCCache, DuplicateInsertPromotesARecentEntry) {
    ARCCache<int, std::string> cache{2};
    static_cast<void>(cache.insert({1, "old"}));
    static_cast<void>(cache.insert({2, "two"}));

    EXPECT_FALSE(cache.insert({1, "new"}).has_value());
    const auto evicted = cache.insert({3, "three"});

    ASSERT_TRUE(evicted.has_value());
    EXPECT_EQ(evicted->key, 2);
    ASSERT_NE(cache.find(1), nullptr);
    EXPECT_EQ(*cache.find(1), "new");
}

TEST(ARCCache, HandlesBothB1AndB2GhostHits) {
    ARCCache<int, std::string> cache{2, 2};
    static_cast<void>(cache.insert({1, "one"}));
    static_cast<void>(cache.insert({2, "two"}));
    cache.touch(1);
    static_cast<void>(cache.insert({3, "three"}));

    const auto after_b1_hit = cache.insert({2, "two again"});
    ASSERT_TRUE(after_b1_hit.has_value());
    EXPECT_EQ(after_b1_hit->key, 1);

    const auto after_b2_hit = cache.insert({1, "one again"});
    ASSERT_TRUE(after_b2_hit.has_value());
    EXPECT_EQ(after_b2_hit->key, 3);
    EXPECT_TRUE(cache.contains(1));
    EXPECT_TRUE(cache.contains(2));
    EXPECT_LE(cache.shadow_size(), cache.shadow_capacity());
}

TEST(ARCCache, ExtractDoesNotCreateGhostHistory) {
    ARCCache<int, std::string> cache{2, 4};
    static_cast<void>(cache.insert({1, "one"}));

    const auto extracted = cache.extract(1);

    ASSERT_TRUE(extracted.has_value());
    EXPECT_EQ(extracted->key, 1);
    EXPECT_EQ(cache.size(), 0);
    EXPECT_EQ(cache.shadow_size(), 0);
}

TEST(ARCCache, TouchingAGhostKeyIsANoOp) {
    ARCCache<int, std::string> cache{2, 4};
    static_cast<void>(cache.insert({1, "one"}));
    static_cast<void>(cache.insert({2, "two"}));
    cache.touch(1);
    static_cast<void>(cache.insert({3, "three"}));
    ASSERT_EQ(cache.shadow_size(), 1);

    cache.touch(2);

    EXPECT_EQ(cache.size(), 2);
    EXPECT_EQ(cache.shadow_size(), 1);
    EXPECT_EQ(cache.find(2), nullptr);
}

TEST(ARCCache, ClearRemovesHistoryAndRestoresInitialState) {
    ARCCache<int, std::string> cache{2, 4};
    static_cast<void>(cache.insert({1, "one"}));
    static_cast<void>(cache.insert({2, "two"}));
    cache.touch(1);
    static_cast<void>(cache.insert({3, "three"}));
    static_cast<void>(cache.insert({2, "two again"}));
    ASSERT_GT(cache.shadow_size(), 0);

    cache.clear();

    EXPECT_EQ(cache.size(), 0);
    EXPECT_EQ(cache.shadow_size(), 0);
    static_cast<void>(cache.insert({1, "fresh one"}));
    static_cast<void>(cache.insert({2, "fresh two"}));
    const auto evicted = cache.insert({3, "fresh three"});
    ASSERT_TRUE(evicted.has_value());
    EXPECT_EQ(evicted->key, 1);
}
