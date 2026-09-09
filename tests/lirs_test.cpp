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
