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
