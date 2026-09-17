#include <gtest/gtest.h>

#include <cstddef>

import cache;
import cache.mock_db;

TEST(MockDatabase, CreatesDeterministicPagesOfRequestedSize) {
    const MockDatabase database{64};
    const DefaultValue first = database(42);
    const DefaultValue again = database(42);
    const DefaultValue other = database(43);

    EXPECT_EQ(first.size(), 64);
    EXPECT_EQ(first, again);
    EXPECT_NE(first, other);
}

TEST(MockDatabase, DefaultSizeAndZeroLengthAreSupported) {
    EXPECT_EQ(MockDatabase{default_page_bytes}(1).size(), 32);
    EXPECT_TRUE(MockDatabase{0}(1).empty());
}
