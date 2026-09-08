module;

#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>

module cache.config;

Config parse_config(std::string_view path) {
    std::ifstream input{std::string{path}};
    if (!input) {
        throw std::runtime_error("cannot open config: " + std::string{path});
    }

    std::size_t level_count = 0;
    if (!(input >> level_count) || level_count == 0) {
        throw std::runtime_error("config must start with a positive level count");
    }

    Config config;
    config.policies.reserve(level_count);

    for (std::size_t level = 0; level < level_count; ++level) {
        std::string policy;
        if (!(input >> policy)) {
            throw std::runtime_error("config contains fewer policies than declared");
        }
        config.policies.push_back(std::move(policy));
    }

    return config;
}

Input read_input(std::istream& input) {
    std::size_t request_count = 0;
    Input result;

    if (!(input >> result.cache_size >> request_count)) {
        throw std::runtime_error("input must start with cache size and request count");
    }

    result.requests.reserve(request_count);
    for (std::size_t index = 0; index < request_count; ++index) {
        DefaultKey key{};
        if (!(input >> key)) {
            throw std::runtime_error("input contains fewer requests than declared");
        }
        result.requests.push_back(key);
    }

    return result;
}
