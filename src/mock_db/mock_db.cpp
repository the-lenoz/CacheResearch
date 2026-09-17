module;

#include <cstddef>
#include <cstdint>

module cache.mock_db;

import cache;

MockDatabase::MockDatabase(std::size_t page_bytes)
    : page_bytes_(page_bytes) {}

DefaultValue MockDatabase::operator()(const DefaultKey& key) const {
    DefaultValue page(page_bytes_);
    std::uint64_t state = static_cast<std::uint64_t>(static_cast<std::int64_t>(key));
    for (std::size_t index = 0; index < page.size(); ++index) {
        state += 0x9e3779b97f4a7c15ULL;
        std::uint64_t mixed = state;
        mixed = (mixed ^ (mixed >> 30)) * 0xbf58476d1ce4e5b9ULL;
        mixed = (mixed ^ (mixed >> 27)) * 0x94d049bb133111ebULL;
        page[index] = static_cast<std::byte>((mixed ^ (mixed >> 31)) & 0xff);
    }
    return page;
}
