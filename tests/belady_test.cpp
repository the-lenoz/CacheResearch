#include <gtest/gtest.h>

#include <string>
#include <vector>

import cache.belady;

TEST(BeladyCache, ReturnsZeroForEmptyTrace) {
    const BeladyCache<int> cache{3, {}};

    EXPECT_EQ(cache.run(), 0);
}

TEST(BeladyCache, ReturnsZeroForZeroCapacity) {
    const BeladyCache<int> cache{0, {1, 1, 1}};

    EXPECT_EQ(cache.run(), 0);
}

TEST(BeladyCache, CountsRepeatedKeyHits) {
    const BeladyCache<int> cache{1, {7, 7, 7, 7}};

    EXPECT_EQ(cache.run(), 3);
}

TEST(BeladyCache, EvictsTheKeyUsedFarthestInTheFuture) {
    const BeladyCache<int> cache{2, {1, 2, 3, 1, 2, 3}};

    EXPECT_EQ(cache.run(), 2);
}

TEST(BeladyCache, SupportsNonIntegerKeys) {
    const BeladyCache<std::string> cache{
        2,
        {"alpha", "beta", "gamma", "alpha", "gamma"}
    };

    EXPECT_EQ(cache.run(), 2);
}
