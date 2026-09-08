#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

import cache;
import cache.belady;
import cache.config;
import cache.factory;
import cache.hierarchy;

int main(int argc, char** argv) {
    try {
        if (argc != 2) {
            throw std::runtime_error("usage: cache_sim <config-file>");
        }

        const Config config = parse_config(argv[1]);
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
                input.cache_size
            ));
        }

        CacheHierarchy<DefaultKey, DefaultValue> hierarchy{std::move(levels)};
        for (const DefaultKey key : input.requests) {
            static_cast<void>(hierarchy.access({key, {}}));
        }

        std::cout << hierarchy.hits() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
