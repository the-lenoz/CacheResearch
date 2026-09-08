#include <gtest/gtest.h>

#include <sstream>
#include <stdexcept>
#include <vector>

import cache;
import cache.config;

TEST(InputParser, ReadsCacheSizeAndTrace) {
    std::istringstream stream{"2 5\n1 2 1 3 1\n"};

    const Input input = read_input(stream);

    EXPECT_EQ(input.cache_size, 2);
    EXPECT_EQ(input.requests, (std::vector<DefaultKey>{1, 2, 1, 3, 1}));
}

TEST(InputParser, RejectsIncompleteTrace) {
    std::istringstream stream{"2 3\n1 2\n"};

    EXPECT_THROW(static_cast<void>(read_input(stream)), std::runtime_error);
}
