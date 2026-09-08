module;

#include <cstddef>
#include <istream>
#include <string>
#include <string_view>
#include <vector>

export module cache.config;

import cache;

export struct Config {
    std::vector<std::string> policies;
};

export struct Input {
    std::size_t cache_size{};
    std::vector<DefaultKey> requests;
};

export [[nodiscard]] Config parse_config(std::string_view path);
export [[nodiscard]] Input read_input(std::istream& input);
