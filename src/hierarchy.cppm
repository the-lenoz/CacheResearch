module;

#include <cstddef>
#include <functional>
#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

export module cache.hierarchy;

import cache;
import cache.variant;

export template <typename KeyType, typename ValueType, typename Loader>
class CacheHierarchy {
public:
    using cache_type = CacheVariant<KeyType, ValueType>;
    using entry_type = CacheEntry<KeyType, ValueType>;
    using key_type = KeyType;
    using value_type = ValueType;
    using result_type = CacheAccessResult<ValueType>;

    explicit CacheHierarchy(std::vector<cache_type> levels, Loader slow_get_page)
        : levels_(std::move(levels)), slow_get_page_(std::move(slow_get_page)) {
        if (levels_.empty()) {
            throw std::invalid_argument("cache hierarchy must contain at least one level");
        }
    }

    // A hit promotes the stored value; a full miss loads and inserts one page.
    [[nodiscard]] result_type access(const key_type& key) {
        for (std::size_t level = 0; level < levels_.size(); ++level) {
            if (!std::visit([&](const auto& cache) { return cache.contains(key); },
                            levels_[level])) {
                continue;
            }

            ++hit_count_;
            if (level == 0) {
                std::visit([&](auto& cache) { cache.touch(key); }, levels_.front());
            } else {
                auto promoted = std::visit([&](auto& cache) { return cache.extract(key); },
                                           levels_[level]);
                static_cast<void>(place(std::move(*promoted), 0));
            }
            return result_type{true, find(key)};
        }

        entry_type loaded{key, slow_get_page_(key)};
        auto dropped = place(std::move(loaded), 0);
        if (auto* resident = find(key)) {
            return result_type{false, resident};
        }
        return result_type{std::move(dropped->value)};
    }

    // Explicitly fills L1. Any eviction cascades through the lower levels.
    void insert(entry_type entry) {
        // Keep the hierarchy exclusive when a caller replaces an existing key.
        for (std::size_t level = 1; level < levels_.size(); ++level) {
            std::visit([&](auto& cache) { cache.erase(entry.key); }, levels_[level]);
        }
        static_cast<void>(place(std::move(entry), 0));
    }

    [[nodiscard]] value_type* find(const key_type& key) {
        for (auto& level : levels_) {
            if (auto* value = std::visit([&](auto& cache) -> value_type* {
                    return cache.find(key);
                }, level)) {
                return value;
            }
        }
        return nullptr;
    }

    [[nodiscard]] const value_type* find(const key_type& key) const {
        for (const auto& level : levels_) {
            if (const auto* value = std::visit([&](const auto& cache) -> const value_type* {
                    return cache.find(key);
                }, level)) {
                return value;
            }
        }
        return nullptr;
    }

    [[nodiscard]] std::size_t hits() const {
        return hit_count_;
    }

private:
    [[nodiscard]] std::optional<entry_type> place(entry_type entry, std::size_t first_level) {
        std::optional<entry_type> moving{std::move(entry)};

        for (std::size_t level = first_level; level < levels_.size() && moving; ++level) {
            moving = std::visit([&](auto& cache) {
                return cache.insert(std::move(*moving));
            }, levels_[level]);
        }
        return moving;
    }

    std::vector<cache_type> levels_;
    Loader slow_get_page_;
    std::size_t hit_count_ = 0;
};
