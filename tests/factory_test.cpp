#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

import cache;
import cache.factory;

TEST(CacheFactory, CreatesEveryOnlinePolicy) {
    const auto lru = make_cache<int, std::string>("LRU", 2);
    const auto lfu = make_cache<int, std::string>("LFU", 3);
    const auto two_q = make_cache<int, std::string>("2Q", 4);
    const auto arc = make_cache<int, std::string>("ARC", 5);
    const auto lirs = make_cache<int, std::string>("LIRS", 6);

    EXPECT_EQ(lru->capacity(), 2);
    EXPECT_EQ(lfu->capacity(), 3);
    EXPECT_EQ(two_q->capacity(), 4);
    EXPECT_EQ(arc->capacity(), 5);
    EXPECT_EQ(lirs->capacity(), 6);
}

TEST(CacheFactory, RejectsUnknownPolicy) {
    EXPECT_THROW(
        (static_cast<void>(make_cache<int, std::string>("UNKNOWN", 2))),
        std::invalid_argument
    );
}
