#include <charconv>
#include <cstddef>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

import cache;
import cache.belady;
import cache.config;
import cache.factory;
import cache.hierarchy;

int main(int argc, char** argv) {
    try {
        bool capacity_includes_shadow = false;
        std::optional<std::size_t> logical_value_bytes;
        std::string_view config_path;
        for (int index = 1; index < argc; ++index) {
            const std::string_view argument = argv[index];
            if (argument == "--capacity-includes-shadow") {
                capacity_includes_shadow = true;
            } else if (argument == "--value-bytes") {
                if (++index >= argc) {
                    throw std::runtime_error("--value-bytes requires an integer");
                }
                const std::string_view value = argv[index];
                std::size_t parsed = 0;
                const auto [end, error] = std::from_chars(
                    value.data(),
                    value.data() + value.size(),
                    parsed
                );
                if (error != std::errc{} || end != value.data() + value.size()
                    || parsed == 0) {
                    throw std::runtime_error(
                        "--value-bytes must be a positive integer"
                    );
                }
                logical_value_bytes = parsed;
            } else if (argument.starts_with('-')) {
                throw std::runtime_error("unknown option: " + std::string{argument});
            } else if (config_path.empty()) {
                config_path = argument;
            } else {
                throw std::runtime_error("only one config file may be specified");
            }
        }
        if (config_path.empty()) {
            throw std::runtime_error(
                "usage: cache_sim [--capacity-includes-shadow] "
                "[--value-bytes <positive integer>] <config-file>"
            );
        }

        const Config config = parse_config(config_path);
        Input input = read_input(std::cin);

        if (config.policies.size() == 1 && config.policies.front() == "BELADY") {
            const BeladyCache<DefaultKey> cache{
                input.cache_size,
                std::move(input.requests)
            };
            std::cout << cache.run() << '\n';
            return 0;
        }

        using OnlineCache = Cache<DefaultKey, DefaultValue>;
        std::vector<std::unique_ptr<OnlineCache>> levels;
        levels.reserve(config.policies.size());

        for (const auto& policy : config.policies) {
            levels.push_back(make_cache<DefaultKey, DefaultValue>(
                policy,
                input.cache_size,
                capacity_includes_shadow
                    ? CapacityAccounting::resident_and_shadow
                    : CapacityAccounting::resident_only,
                logical_value_bytes.value_or(sizeof(DefaultValue))
            ));
        }

        CacheHierarchy<DefaultKey, DefaultValue> hierarchy{std::move(levels)};
        for (const DefaultKey key : input.requests) {
            if (hierarchy.access(key) == nullptr) {
                hierarchy.insert({key, {}});
            }
        }

        std::cout << hierarchy.hits() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
