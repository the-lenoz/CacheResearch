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

TEST(LFUCache, ConstructorReportsCapacityAndNoShadowHistory) {
    const LFUCache<int, std::string> cache{7};

    EXPECT_EQ(cache.capacity(), 7);
    EXPECT_EQ(cache.size(), 0);
    EXPECT_EQ(cache.shadow_capacity(), 0);
    EXPECT_EQ(cache.shadow_size(), 0);
}

TEST(LFUCache, FindDoesNotIncreaseFrequency) {
    LFUCache<int, std::string> cache{2};
    static_cast<void>(cache.insert({1, "one"}));
    static_cast<void>(cache.insert({2, "two"}));

    ASSERT_NE(cache.find(1), nullptr);
    const LFUCache<int, std::string>& const_cache = cache;
    ASSERT_NE(const_cache.find(1), nullptr);
    const auto evicted = cache.insert({3, "three"});

    ASSERT_TRUE(evicted.has_value());
    EXPECT_EQ(evicted->key, 1);
}

TEST(LFUCache, DuplicateInsertCountsAsAccess) {
    LFUCache<int, std::string> cache{2};
    static_cast<void>(cache.insert({1, "old"}));
    static_cast<void>(cache.insert({2, "two"}));

    EXPECT_FALSE(cache.insert({1, "new"}).has_value());
    const auto evicted = cache.insert({3, "three"});

    ASSERT_TRUE(evicted.has_value());
    EXPECT_EQ(evicted->key, 2);
    ASSERT_NE(cache.find(1), nullptr);
    EXPECT_EQ(*cache.find(1), "new");
}

TEST(LFUCache, ExtractedEntryLosesItsOldFrequencyWhenReinserted) {
    LFUCache<int, std::string> cache{2};
    static_cast<void>(cache.insert({1, "one"}));
    static_cast<void>(cache.insert({2, "two"}));
    cache.touch(1);
    cache.touch(1);

    ASSERT_TRUE(cache.extract(1).has_value());
    EXPECT_FALSE(cache.insert({1, "one again"}).has_value());
    cache.touch(2);
    const auto evicted = cache.insert({3, "three"});

    ASSERT_TRUE(evicted.has_value());
    EXPECT_EQ(evicted->key, 1);
}

TEST(LFUCache, CapacityOneMaintainsValidFrequencyBuckets) {
    LFUCache<int, std::string> cache{1};
    static_cast<void>(cache.insert({1, "one"}));
    cache.touch(1);
    cache.touch(1);

    const auto evicted = cache.insert({2, "two"});

    ASSERT_TRUE(evicted.has_value());
    EXPECT_EQ(evicted->key, 1);
    EXPECT_EQ(cache.size(), 1);
    ASSERT_NE(cache.find(2), nullptr);
}
