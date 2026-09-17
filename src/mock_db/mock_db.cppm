module;

#include <cstddef>

export module cache.mock_db;

import cache;

export inline constexpr std::size_t default_page_bytes = 32;

export class MockDatabase {
public:
    explicit MockDatabase(std::size_t page_bytes);

    [[nodiscard]] DefaultValue operator()(const DefaultKey& key) const;

private:
    std::size_t page_bytes_;
};
