#include <gtest/gtest.h>

#include <string>

import cache;
import cache.lfu;

TEST(LFUCache, EvictsLeastFrequentlyUsedEntry) {
    LFUCache<int, std::string> cache{2};

    static_cast<void>(cache.insert({1, "one"}));
    static_cast<void>(cache.insert({2, "two"}));
    cache.touch(1);

    const auto evicted = cache.insert({3, "three"});

    ASSERT_TRUE(evicted.has_value());
    EXPECT_EQ(evicted->key, 2);
    EXPECT_EQ(evicted->value, "two");
    EXPECT_TRUE(cache.contains(1));
    EXPECT_TRUE(cache.contains(3));
}

TEST(LFUCache, UsesLRUToBreakFrequencyTies) {
    LFUCache<int, std::string> cache{2};

    static_cast<void>(cache.insert({1, "one"}));
    static_cast<void>(cache.insert({2, "two"}));

    const auto evicted = cache.insert({3, "three"});

    ASSERT_TRUE(evicted.has_value());
    EXPECT_EQ(evicted->key, 1);
}

TEST(LFUCache, StoresUpdatesAndExtractsValues) {
    LFUCache<int, std::string> cache{2};

    static_cast<void>(cache.insert({1, "old"}));
    EXPECT_FALSE(cache.insert({1, "new"}).has_value());
    ASSERT_NE(cache.find(1), nullptr);
    EXPECT_EQ(*cache.find(1), "new");

    const auto extracted = cache.extract(1);
    ASSERT_TRUE(extracted.has_value());
    EXPECT_EQ(extracted->value, "new");
    EXPECT_EQ(cache.size(), 0);
}

TEST(LFUCache, ImmediatelyEvictsFromZeroCapacityCache) {
    LFUCache<int, std::string> cache{0};

    const auto evicted = cache.insert({1, "one"});

    ASSERT_TRUE(evicted.has_value());
    EXPECT_EQ(evicted->key, 1);
    EXPECT_EQ(cache.size(), 0);
}
