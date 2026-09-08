#include <gtest/gtest.h>

#include <string>

import cache;
import cache.lru;

TEST(LRUCache, EvictsLeastRecentlyUsedEntry) {
    LRUCache<int, std::string> cache{2};

    EXPECT_FALSE(cache.insert({1, "one"}).has_value());
    EXPECT_FALSE(cache.insert({2, "two"}).has_value());
    cache.touch(1);

    const auto evicted = cache.insert({3, "three"});

    ASSERT_TRUE(evicted.has_value());
    EXPECT_EQ(evicted->key, 2);
    EXPECT_EQ(evicted->value, "two");
    EXPECT_TRUE(cache.contains(1));
    EXPECT_TRUE(cache.contains(3));
    EXPECT_FALSE(cache.contains(2));
}

TEST(LRUCache, StoresAndUpdatesValues) {
    LRUCache<int, std::string> cache{1};

    static_cast<void>(cache.insert({1, "old"}));
    EXPECT_FALSE(cache.insert({1, "new"}).has_value());

    ASSERT_NE(cache.find(1), nullptr);
    EXPECT_EQ(*cache.find(1), "new");
    EXPECT_EQ(cache.size(), 1);
}

TEST(LRUCache, ExtractsAnEntry) {
    LRUCache<int, std::string> cache{2};
    static_cast<void>(cache.insert({1, "one"}));

    const auto extracted = cache.extract(1);

    ASSERT_TRUE(extracted.has_value());
    EXPECT_EQ(extracted->value, "one");
    EXPECT_EQ(cache.size(), 0);
}

TEST(LRUCache, ImmediatelyEvictsFromZeroCapacityCache) {
    LRUCache<int, std::string> cache{0};

    const auto evicted = cache.insert({1, "one"});

    ASSERT_TRUE(evicted.has_value());
    EXPECT_EQ(evicted->key, 1);
    EXPECT_EQ(cache.size(), 0);
}
