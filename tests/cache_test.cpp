#include <gtest/gtest.h>

#include <string>

import cache;

TEST(CacheEntry, StoresTypedPayload) {
    const CacheEntry<int, std::string> entry{42, "payload"};

    EXPECT_EQ(entry.key, 42);
    EXPECT_EQ(entry.value, "payload");
}
