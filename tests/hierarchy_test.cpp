#include <gtest/gtest.h>

#include <functional>
#include <initializer_list>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

import cache;
import cache.factory;
import cache.hierarchy;
import cache.variant;

namespace {
using StringLoader = std::function<std::string(const int&)>;
using StringHierarchy = CacheHierarchy<int, std::string, StringLoader>;
using StringVariant = CacheVariant<int, std::string>;

std::vector<StringVariant> levels(
    std::initializer_list<std::pair<std::string_view, std::size_t>> specs
) {
    std::vector<StringVariant> result;
    for (const auto& [policy, capacity] : specs) {
        result.push_back(make_cache<int, std::string>(policy, capacity));
    }
    return result;
}

StringLoader default_loader() {
    return [](const int& key) { return std::to_string(key); };
}
}

TEST(CacheHierarchy, ConstructorRejectsEmptyLevels) {
    EXPECT_THROW((StringHierarchy{{}, default_loader()}), std::invalid_argument);
}

TEST(CacheHierarchy, MissLoadsAndLaterHitReusesTheValue) {
    int calls = 0;
    StringHierarchy hierarchy{levels({{"LRU", 2}}), [&](const int& key) {
        ++calls;
        return std::to_string(key);
    }};

    auto first = hierarchy.access(42);
    EXPECT_FALSE(first.hit());
    EXPECT_EQ(first.value(), "42");
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(hierarchy.hits(), 0);

    auto second = hierarchy.access(42);
    EXPECT_TRUE(second.hit());
    EXPECT_EQ(second.value(), "42");
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(hierarchy.hits(), 1);
}

TEST(CacheHierarchy, LowerLevelHitPromotesWithoutLoading) {
    int calls = 0;
    StringHierarchy hierarchy{levels({{"LRU", 1}, {"ARC", 1}}), [&](const int& key) {
        ++calls;
        return std::to_string(key);
    }};
    static_cast<void>(hierarchy.access(1));
    static_cast<void>(hierarchy.access(2));

    auto promoted = hierarchy.access(1);
    EXPECT_TRUE(promoted.hit());
    EXPECT_EQ(promoted.value(), "1");
    EXPECT_EQ(calls, 2);
    EXPECT_EQ(hierarchy.hits(), 1);
    ASSERT_NE(hierarchy.find(2), nullptr);
    EXPECT_EQ(*hierarchy.find(2), "2");
}

TEST(CacheHierarchy, ManualInsertReplacesLowerValueWithoutLoading) {
    int calls = 0;
    StringHierarchy hierarchy{levels({{"LRU", 1}, {"LFU", 1}}), [&](const int&) {
        ++calls;
        return std::string{"loaded"};
    }};
    hierarchy.insert({1, "old"});
    hierarchy.insert({2, "two"});
    hierarchy.insert({1, "new"});

    auto result = hierarchy.access(1);
    EXPECT_TRUE(result.hit());
    EXPECT_EQ(result.value(), "new");
    EXPECT_EQ(calls, 0);
    EXPECT_EQ(hierarchy.hits(), 1);
}

TEST(CacheHierarchy, ConstFindDoesNotLoadOrChangePolicy) {
    int calls = 0;
    StringHierarchy hierarchy{levels({{"LRU", 2}}), [&](const int&) {
        ++calls;
        return std::string{"loaded"};
    }};
    hierarchy.insert({1, "one"});
    hierarchy.insert({2, "two"});
    const auto& const_hierarchy = hierarchy;
    ASSERT_NE(const_hierarchy.find(1), nullptr);
    EXPECT_EQ(*const_hierarchy.find(1), "one");
    EXPECT_EQ(const_hierarchy.find(3), nullptr);
    hierarchy.insert({3, "three"});
    EXPECT_EQ(hierarchy.find(1), nullptr);
    EXPECT_EQ(calls, 0);
    EXPECT_EQ(hierarchy.hits(), 0);
}

TEST(CacheHierarchy, ZeroCapacityLevelPassesEntriesToNextLevel) {
    StringHierarchy hierarchy{levels({{"LRU", 0}, {"LRU", 1}}), default_loader()};
    auto first = hierarchy.access(1);
    EXPECT_FALSE(first.hit());
    EXPECT_EQ(first.value(), "1");
    auto second = hierarchy.access(2);
    EXPECT_FALSE(second.hit());
    EXPECT_EQ(second.value(), "2");
    EXPECT_TRUE(hierarchy.access(2).hit());
    EXPECT_EQ(hierarchy.hits(), 1);
}

TEST(CacheHierarchy, AllZeroCapacityLevelsReturnOwnedValue) {
    int calls = 0;
    StringHierarchy hierarchy{levels({{"LRU", 0}, {"ARC", 0}}), [&](const int& key) {
        ++calls;
        return std::to_string(key);
    }};
    auto first = hierarchy.access(7);
    EXPECT_FALSE(first.hit());
    EXPECT_EQ(first.value(), "7");
    EXPECT_EQ(hierarchy.find(7), nullptr);
    auto second = hierarchy.access(7);
    EXPECT_FALSE(second.hit());
    EXPECT_EQ(second.value(), "7");
    EXPECT_EQ(calls, 2);
    EXPECT_EQ(hierarchy.hits(), 0);
}

TEST(CacheHierarchy, EvictedKeyLoadsAgain) {
    int calls = 0;
    StringHierarchy hierarchy{levels({{"LRU", 1}}), [&](const int& key) {
        ++calls;
        return std::to_string(key);
    }};
    static_cast<void>(hierarchy.access(1));
    static_cast<void>(hierarchy.access(2));
    EXPECT_FALSE(hierarchy.access(1).hit());
    EXPECT_EQ(calls, 3);
}

TEST(CacheHierarchy, LoaderFailureDoesNotInsertOrCountAHit) {
    StringHierarchy hierarchy{levels({{"LRU", 1}}), [](const int&) -> std::string {
        throw std::runtime_error("db failed");
    }};
    EXPECT_THROW((static_cast<void>(hierarchy.access(3))), std::runtime_error);
    EXPECT_EQ(hierarchy.find(3), nullptr);
    EXPECT_EQ(hierarchy.hits(), 0);
}

TEST(CacheHierarchy, MixedAndRepeatedPoliciesPreserveValues) {
    StringHierarchy hierarchy{
        levels({{"LRU", 1}, {"ARC", 1}, {"LFU", 1}, {"ARC", 1},
                {"2Q", 1}, {"LIRS", 1}}), default_loader()
    };
    for (int key = 1; key <= 6; ++key) {
        static_cast<void>(hierarchy.access(key));
    }
    for (int key = 1; key <= 6; ++key) {
        auto result = hierarchy.access(key);
        EXPECT_TRUE(result.hit());
        EXPECT_EQ(result.value(), std::to_string(key));
    }
    EXPECT_EQ(hierarchy.hits(), 6);
}

TEST(CacheHierarchy, MoveOnlyLoaderAndPayloadWorkAcrossLevels) {
    struct Loader {
        std::unique_ptr<int> offset = std::make_unique<int>(10);
        std::unique_ptr<int> operator()(const int& key) {
            return std::make_unique<int>(key + *offset);
        }
    };
    std::vector<CacheVariant<int, std::unique_ptr<int>>> cache_levels;
    cache_levels.push_back(make_cache<int, std::unique_ptr<int>>("LFU", 1));
    cache_levels.push_back(make_cache<int, std::unique_ptr<int>>("ARC", 1));
    CacheHierarchy<int, std::unique_ptr<int>, Loader> hierarchy{
        std::move(cache_levels), Loader{}
    };
    auto first = hierarchy.access(1);
    EXPECT_FALSE(first.hit());
    EXPECT_EQ(*first.value(), 11);
    static_cast<void>(hierarchy.access(2));
    auto promoted = hierarchy.access(1);
    EXPECT_TRUE(promoted.hit());
    EXPECT_EQ(*promoted.value(), 11);
    const auto& const_hierarchy = hierarchy;
    ASSERT_NE(const_hierarchy.find(2), nullptr);
    EXPECT_EQ(**const_hierarchy.find(2), 12);
}

TEST(CacheHierarchy, ZeroCapacityResultOwnsMoveOnlyPayload) {
    struct Loader {
        std::unique_ptr<int> token = std::make_unique<int>(5);
        std::unique_ptr<int> operator()(const int& key) {
            return std::make_unique<int>(key + *token);
        }
    };
    std::vector<CacheVariant<int, std::unique_ptr<int>>> cache_levels;
    cache_levels.push_back(make_cache<int, std::unique_ptr<int>>("LRU", 0));
    CacheHierarchy<int, std::unique_ptr<int>, Loader> hierarchy{
        std::move(cache_levels), Loader{}
    };
    auto result = hierarchy.access(7);
    EXPECT_FALSE(result.hit());
    EXPECT_EQ(*result.value(), 12);
    EXPECT_EQ(hierarchy.find(7), nullptr);
}
