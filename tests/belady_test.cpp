#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

import cache.belady;

namespace {

template <typename Key>
struct ZeroLoader {
    int operator()(const Key&) const { return 0; }
};

template <typename Key, typename Hash = std::hash<Key>,
          typename Equal = std::equal_to<Key>>
using TestedBelady = BeladyCache<Key, int, ZeroLoader<Key>, Hash, Equal>;

std::size_t brute_force_optimal_hits(
    const std::vector<int>& trace,
    std::size_t capacity
) {
    std::vector<int> resident;
    const auto search = [&](const auto& self, std::size_t request) -> std::size_t {
        if (request == trace.size()) {
            return 0;
        }

        const int key = trace[request];
        if (std::find(resident.begin(), resident.end(), key) != resident.end()) {
            return 1 + self(self, request + 1);
        }
        if (capacity == 0) {
            return self(self, request + 1);
        }
        if (resident.size() < capacity) {
            resident.push_back(key);
            const std::size_t result = self(self, request + 1);
            resident.pop_back();
            return result;
        }

        std::size_t best = 0;
        for (int& victim : resident) {
            const int previous = victim;
            victim = key;
            best = std::max(best, self(self, request + 1));
            victim = previous;
        }
        return best;
    };
    return search(search, 0);
}

struct EquivalentKey {
    int value;
};

struct ModuloHash {
    std::size_t operator()(const EquivalentKey& key) const noexcept {
        return std::hash<int>{}(key.value % 10);
    }
};

struct ModuloEqual {
    bool operator()(
        const EquivalentKey& left,
        const EquivalentKey& right
    ) const noexcept {
        return left.value % 10 == right.value % 10;
    }
};

} // namespace

TEST(BeladyCache, ReturnsZeroForEmptyTrace) {
    const TestedBelady<int> cache{3, {}, {}};

    EXPECT_EQ(cache.run(), 0);
}

TEST(BeladyCache, ReturnsZeroForZeroCapacity) {
    const TestedBelady<int> cache{0, {1, 1, 1}, {}};

    EXPECT_EQ(cache.run(), 0);
}

TEST(BeladyCache, CountsRepeatedKeyHits) {
    const TestedBelady<int> cache{1, {7, 7, 7, 7}, {}};

    EXPECT_EQ(cache.run(), 3);
}

TEST(BeladyCache, EvictsTheKeyUsedFarthestInTheFuture) {
    const TestedBelady<int> cache{2, {1, 2, 3, 1, 2, 3}, {}};

    EXPECT_EQ(cache.run(), 2);
}

TEST(BeladyCache, SupportsNonIntegerKeys) {
    const TestedBelady<std::string> cache{
        2,
        {"alpha", "beta", "gamma", "alpha", "gamma"},
        {}
    };

    EXPECT_EQ(cache.run(), 2);
}

TEST(BeladyCache, ConstructorHandlesCapacityLargerThanTheWorkingSet) {
    const TestedBelady<int> cache{100, {1, 2, 1, 3, 2, 3}, {}};

    EXPECT_EQ(cache.run(), 3);
}

TEST(BeladyCache, AllUniqueRequestsProduceNoHits) {
    const TestedBelady<int> cache{3, {1, 2, 3, 4, 5, 6}, {}};

    EXPECT_EQ(cache.run(), 0);
}

TEST(BeladyCache, RunIsRepeatableAndDoesNotConsumeTheTrace) {
    const TestedBelady<int> cache{2, {1, 2, 3, 1, 2, 3}, {}};

    const std::size_t first = cache.run();
    const std::size_t second = cache.run();

    EXPECT_EQ(first, 2);
    EXPECT_EQ(second, first);
}

TEST(BeladyCache, HonorsCustomHashAndKeyEquality) {
    const TestedBelady<EquivalentKey, ModuloHash, ModuloEqual> cache{
        1,
        {{1}, {11}, {2}, {12}},
        {}
    };

    EXPECT_EQ(cache.run(), 2);
}

TEST(BeladyCache, LoadsOnlyMissesAndRepeatsTheTrace) {
    struct Loader {
        int* calls;
        std::string operator()(const int& key) {
            ++*calls;
            return std::to_string(key);
        }
    };
    int calls = 0;
    const BeladyCache<int, std::string, Loader> cache{
        2, {1, 2, 1, 3, 1}, Loader{&calls}
    };
    EXPECT_EQ(cache.run(), 2);
    EXPECT_EQ(calls, 3);
    EXPECT_EQ(cache.run(), 2);
    EXPECT_EQ(calls, 6);
}

TEST(BeladyCache, ZeroCapacityLoadsEveryRequestAndEmptyTraceLoadsNone) {
    struct Loader {
        int* calls;
        int operator()(const int& key) {
            ++*calls;
            return key;
        }
    };
    int calls = 0;
    const BeladyCache<int, int, Loader> zero{0, {1, 1, 2}, Loader{&calls}};
    EXPECT_EQ(zero.run(), 0);
    EXPECT_EQ(calls, 3);
    const BeladyCache<int, int, Loader> empty{2, {}, Loader{&calls}};
    EXPECT_EQ(empty.run(), 0);
    EXPECT_EQ(calls, 3);
}

TEST(BeladyCache, AcceptsMoveOnlyLoaderAndPayload) {
    struct Loader {
        std::unique_ptr<int> offset = std::make_unique<int>(10);
        std::unique_ptr<int> operator()(const int& key) {
            return std::make_unique<int>(key + *offset);
        }
    };
    const BeladyCache<int, std::unique_ptr<int>, Loader> cache{
        1, {1, 1, 2}, Loader{}
    };
    EXPECT_EQ(cache.run(), 1);
}

TEST(BeladyCache, PropagatesLoaderFailureAndCanRunAgain) {
    struct Loader {
        bool* fail;
        int operator()(const int& key) {
            if (*fail) {
                throw std::runtime_error("db failed");
            }
            return key;
        }
    };
    bool fail = true;
    const BeladyCache<int, int, Loader> cache{1, {1, 1}, Loader{&fail}};
    EXPECT_THROW((static_cast<void>(cache.run())), std::runtime_error);
    fail = false;
    EXPECT_EQ(cache.run(), 1);
}

TEST(BeladyCache, MatchesBruteForceOptimumForAllSmallTraces) {
    constexpr int alphabet_size = 3;
    constexpr std::size_t maximum_length = 6;

    std::size_t variants = 1;
    for (std::size_t length = 0; length <= maximum_length; ++length) {
        for (std::size_t encoded = 0; encoded < variants; ++encoded) {
            std::vector<int> trace(length);
            std::size_t remainder = encoded;
            for (std::size_t index = 0; index < length; ++index) {
                trace[index] = static_cast<int>(remainder % alphabet_size);
                remainder /= alphabet_size;
            }

            for (std::size_t capacity = 0; capacity <= 3; ++capacity) {
                const TestedBelady<int> cache{capacity, trace, {}};
                EXPECT_EQ(cache.run(), brute_force_optimal_hits(trace, capacity))
                    << "length=" << length
                    << ", encoded_trace=" << encoded
                    << ", capacity=" << capacity;
            }
        }
        variants *= alphabet_size;
    }
}
