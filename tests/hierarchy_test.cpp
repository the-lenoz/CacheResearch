#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

import cache;
import cache.hierarchy;
import cache.lru;

TEST(CacheHierarchy, PromotesHitsAndCascadesEvictions) {
    using BaseCache = Cache<int, std::string>;
    std::vector<std::unique_ptr<BaseCache>> levels;
    levels.push_back(std::make_unique<LRUCache<int, std::string>>(1));
    levels.push_back(std::make_unique<LRUCache<int, std::string>>(1));
    CacheHierarchy<int, std::string> hierarchy{std::move(levels)};

    EXPECT_FALSE(hierarchy.access({1, "one"}));
    EXPECT_FALSE(hierarchy.access({2, "two"}));
    EXPECT_TRUE(hierarchy.access({1, "unused"}));
    EXPECT_TRUE(hierarchy.access({1, "unused"}));

    EXPECT_EQ(hierarchy.hits(), 2);
    ASSERT_NE(hierarchy.find(1), nullptr);
    ASSERT_NE(hierarchy.find(2), nullptr);
    EXPECT_EQ(*hierarchy.find(1), "one");
    EXPECT_EQ(*hierarchy.find(2), "two");
}
