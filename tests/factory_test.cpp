#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

import cache;
import cache.factory;

TEST(CacheFactory, CreatesLRUAndLFU) {
    const auto lru = make_cache<int, std::string>("LRU", 2);
    const auto lfu = make_cache<int, std::string>("LFU", 3);

    EXPECT_EQ(lru->capacity(), 2);
    EXPECT_EQ(lfu->capacity(), 3);
}

TEST(CacheFactory, RejectsUnknownPolicy) {
    EXPECT_THROW(
        (static_cast<void>(make_cache<int, std::string>("UNKNOWN", 2))),
        std::invalid_argument
    );
}
