module;

#include <cstddef>
#include <memory>
#include <string_view>

export module cache.factory;

import cache;

export [[nodiscard]] std::unique_ptr<Cache> make_cache(
    std::string_view policy,
    std::size_t capacity
);
