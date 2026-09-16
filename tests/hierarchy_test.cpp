#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

import cache;
import cache.hierarchy;
import cache.lru;

TEST(CacheHierarchy, ConstructorRejectsEmptyAndNullLevels) {
    using BaseCache = Cache<int, std::string>;

    std::vector<std::unique_ptr<BaseCache>> empty;
    EXPECT_THROW(
        (static_cast<void>(CacheHierarchy<int, std::string>{std::move(empty)})),
        std::invalid_argument
    );

    std::vector<std::unique_ptr<BaseCache>> with_null;
    with_null.push_back(nullptr);
    EXPECT_THROW(
        (static_cast<void>(CacheHierarchy<int, std::string>{std::move(with_null)})),
        std::invalid_argument
    );
}

TEST(CacheHierarchy, PromotesHitsAndCascadesEvictions) {
    using BaseCache = Cache<int, std::string>;
    std::vector<std::unique_ptr<BaseCache>> levels;
    levels.push_back(std::make_unique<LRUCache<int, std::string>>(1));
    levels.push_back(std::make_unique<LRUCache<int, std::string>>(1));
    CacheHierarchy<int, std::string> hierarchy{std::move(levels)};

    hierarchy.insert({1, "one"});
    hierarchy.insert({2, "two"});

    const auto* promoted = hierarchy.access(1);
    ASSERT_NE(promoted, nullptr);
    EXPECT_EQ(*promoted, "one");

    EXPECT_NE(hierarchy.access(1), nullptr);

    EXPECT_EQ(hierarchy.hits(), 2);
    ASSERT_NE(hierarchy.find(1), nullptr);
    ASSERT_NE(hierarchy.find(2), nullptr);
    EXPECT_EQ(*hierarchy.find(1), "one");
    EXPECT_EQ(*hierarchy.find(2), "two");
}

TEST(CacheHierarchy, MissDoesNotInsertAnEntry) {
    using BaseCache = Cache<int, std::string>;
    std::vector<std::unique_ptr<BaseCache>> levels;
    levels.push_back(std::make_unique<LRUCache<int, std::string>>(2));
    CacheHierarchy<int, std::string> hierarchy{std::move(levels)};

    EXPECT_EQ(hierarchy.access(42), nullptr);
    EXPECT_EQ(hierarchy.find(42), nullptr);
    EXPECT_EQ(hierarchy.hits(), 0);
}

TEST(CacheHierarchy, ExplicitInsertReplacesAValueStoredInALowerLevel) {
    using BaseCache = Cache<int, std::string>;
    std::vector<std::unique_ptr<BaseCache>> levels;
    levels.push_back(std::make_unique<LRUCache<int, std::string>>(1));
    levels.push_back(std::make_unique<LRUCache<int, std::string>>(1));
    CacheHierarchy<int, std::string> hierarchy{std::move(levels)};

    hierarchy.insert({1, "old"});
    hierarchy.insert({2, "two"});
    hierarchy.insert({1, "new"});

    const auto* value = hierarchy.access(1);
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(*value, "new");
    EXPECT_EQ(hierarchy.hits(), 1);
}

TEST(CacheHierarchy, FindSupportsConstHierarchyWithoutChangingPolicy) {
    using BaseCache = Cache<int, std::string>;
    std::vector<std::unique_ptr<BaseCache>> levels;
    levels.push_back(std::make_unique<LRUCache<int, std::string>>(2));
    CacheHierarchy<int, std::string> hierarchy{std::move(levels)};
    hierarchy.insert({1, "one"});
    hierarchy.insert({2, "two"});

    hierarchy.insert({7, "seven"});

    const auto& const_hierarchy = hierarchy;
    const auto* value = const_hierarchy.find(2);

    ASSERT_NE(value, nullptr);
    EXPECT_EQ(*value, "two");

    hierarchy.insert({3, "three"});
    EXPECT_EQ(hierarchy.find(2), nullptr);
    EXPECT_NE(hierarchy.find(7), nullptr);
    EXPECT_NE(hierarchy.find(3), nullptr);
    EXPECT_EQ(hierarchy.hits(), 0);
}

TEST(CacheHierarchy, ZeroCapacityLevelPassesEntriesToTheNextLevel) {
    using BaseCache = Cache<int, std::string>;
    std::vector<std::unique_ptr<BaseCache>> levels;
    levels.push_back(std::make_unique<LRUCache<int, std::string>>(0));
    levels.push_back(std::make_unique<LRUCache<int, std::string>>(1));
    CacheHierarchy<int, std::string> hierarchy{std::move(levels)};

    hierarchy.insert({1, "one"});
    ASSERT_NE(hierarchy.find(1), nullptr);
    EXPECT_EQ(*hierarchy.find(1), "one");

    const auto* value = hierarchy.access(1);
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(*value, "one");
    EXPECT_EQ(hierarchy.hits(), 1);
}

TEST(CacheHierarchy, AllZeroCapacityLevelsDropInsertedEntries) {
    using BaseCache = Cache<int, std::string>;
    std::vector<std::unique_ptr<BaseCache>> levels;
    levels.push_back(std::make_unique<LRUCache<int, std::string>>(0));
    levels.push_back(std::make_unique<LRUCache<int, std::string>>(0));
    CacheHierarchy<int, std::string> hierarchy{std::move(levels)};

    hierarchy.insert({1, "one"});

    EXPECT_EQ(hierarchy.find(1), nullptr);
    EXPECT_EQ(hierarchy.access(1), nullptr);
    EXPECT_EQ(hierarchy.hits(), 0);
}
