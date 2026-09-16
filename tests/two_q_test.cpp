#include <gtest/gtest.h>

#include <string>

import cache;
import cache.two_q;

TEST(TwoQCache, ProtectsEntriesPromotedToTheMainQueue) {
    TwoQCache<int, std::string> cache{2};

    static_cast<void>(cache.insert({1, "one"}));
    cache.touch(1);
    static_cast<void>(cache.insert({2, "two"}));

    const auto evicted = cache.insert({3, "three"});

    ASSERT_TRUE(evicted.has_value());
    EXPECT_EQ(evicted->key, 2);
    EXPECT_TRUE(cache.contains(1));
    EXPECT_TRUE(cache.contains(3));
}

TEST(TwoQCache, PromotesARecentGhostHitToTheMainQueue) {
    TwoQCache<int, std::string> cache{2};

    static_cast<void>(cache.insert({1, "one"}));
    static_cast<void>(cache.insert({2, "two"}));
    const auto first_eviction = cache.insert({3, "three"});
    const auto second_eviction = cache.insert({1, "one again"});

    ASSERT_TRUE(first_eviction.has_value());
    EXPECT_EQ(first_eviction->key, 1);
    ASSERT_TRUE(second_eviction.has_value());
    EXPECT_EQ(second_eviction->key, 2);
    ASSERT_NE(cache.find(1), nullptr);
    EXPECT_EQ(*cache.find(1), "one again");
}

TEST(TwoQCache, ExtractsValuesAndHandlesZeroCapacity) {
    TwoQCache<int, std::string> cache{1};
    static_cast<void>(cache.insert({1, "one"}));

    const auto extracted = cache.extract(1);
    ASSERT_TRUE(extracted.has_value());
    EXPECT_EQ(extracted->value, "one");
    EXPECT_EQ(cache.size(), 0);

    TwoQCache<int, std::string> empty{0};
    EXPECT_TRUE(empty.insert({2, "two"}).has_value());
    EXPECT_EQ(empty.size(), 0);
}

TEST(TwoQCache, LimitsShadowHistory) {
    TwoQCache<int, std::string> cache{2, 1};

    for (int key = 1; key <= 8; ++key) {
        static_cast<void>(cache.insert({key, std::to_string(key)}));
        EXPECT_LE(cache.shadow_size(), 1);
    }

    EXPECT_EQ(cache.shadow_capacity(), 1);

    TwoQCache<int, std::string> without_shadow{2, 0};
    for (int key = 1; key <= 4; ++key) {
        static_cast<void>(without_shadow.insert({key, std::to_string(key)}));
    }
    EXPECT_EQ(without_shadow.shadow_size(), 0);
}

TEST(TwoQCache, ConstructorsExposeDefaultAndExplicitShadowCapacities) {
    const TwoQCache<int, std::string> zero{0};
    const TwoQCache<int, std::string> one{1};
    const TwoQCache<int, std::string> default_limit{8};
    const TwoQCache<int, std::string> explicit_limit{8, 11};

    EXPECT_EQ(zero.capacity(), 0);
    EXPECT_EQ(zero.shadow_capacity(), 0);
    EXPECT_EQ(one.shadow_capacity(), 1);
    EXPECT_EQ(default_limit.capacity(), 8);
    EXPECT_EQ(default_limit.shadow_capacity(), 4);
    EXPECT_EQ(explicit_limit.capacity(), 8);
    EXPECT_EQ(explicit_limit.shadow_capacity(), 11);
}

TEST(TwoQCache, FindDoesNotPromoteAnA1inEntry) {
    TwoQCache<int, std::string> cache{2};
    static_cast<void>(cache.insert({1, "one"}));
    static_cast<void>(cache.insert({2, "two"}));

    ASSERT_NE(cache.find(1), nullptr);
    const TwoQCache<int, std::string>& const_cache = cache;
    ASSERT_NE(const_cache.find(1), nullptr);
    const auto evicted = cache.insert({3, "three"});

    ASSERT_TRUE(evicted.has_value());
    EXPECT_EQ(evicted->key, 1);
}

TEST(TwoQCache, DuplicateInsertPromotesAnA1inEntry) {
    TwoQCache<int, std::string> cache{2};
    static_cast<void>(cache.insert({1, "old"}));
    static_cast<void>(cache.insert({2, "two"}));

    EXPECT_FALSE(cache.insert({1, "new"}).has_value());
    const auto evicted = cache.insert({3, "three"});

    ASSERT_TRUE(evicted.has_value());
    EXPECT_EQ(evicted->key, 2);
    ASSERT_NE(cache.find(1), nullptr);
    EXPECT_EQ(*cache.find(1), "new");
}

TEST(TwoQCache, ExtractDoesNotCreateGhostHistory) {
    TwoQCache<int, std::string> cache{2, 4};
    static_cast<void>(cache.insert({1, "one"}));

    const auto extracted = cache.extract(1);

    ASSERT_TRUE(extracted.has_value());
    EXPECT_EQ(extracted->key, 1);
    EXPECT_EQ(cache.size(), 0);
    EXPECT_EQ(cache.shadow_size(), 0);
}

TEST(TwoQCache, TouchingAGhostKeyIsANoOp) {
    TwoQCache<int, std::string> cache{2, 4};
    static_cast<void>(cache.insert({1, "one"}));
    static_cast<void>(cache.insert({2, "two"}));
    ASSERT_TRUE(cache.insert({3, "three"}).has_value());
    ASSERT_EQ(cache.shadow_size(), 1);

    cache.touch(1);

    EXPECT_EQ(cache.size(), 2);
    EXPECT_EQ(cache.shadow_size(), 1);
    EXPECT_EQ(cache.find(1), nullptr);
}

TEST(TwoQCache, ClearRemovesGhostHistory) {
    TwoQCache<int, std::string> cache{2, 4};
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
