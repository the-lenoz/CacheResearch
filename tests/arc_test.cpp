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
