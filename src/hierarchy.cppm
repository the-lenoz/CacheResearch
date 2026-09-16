module;

#include <cstddef>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

export module cache.hierarchy;

import cache;

export template <typename KeyType, typename ValueType>
class CacheHierarchy {
public:
    using cache_type = Cache<KeyType, ValueType>;
    using entry_type = CacheEntry<KeyType, ValueType>;
    using key_type = KeyType;
    using value_type = ValueType;

    explicit CacheHierarchy(std::vector<std::unique_ptr<cache_type>> levels)
        : levels_(std::move(levels)) {
        if (levels_.empty()) {
            throw std::invalid_argument("cache hierarchy must contain at least one level");
        }
        for (const auto& level : levels_) {
            if (!level) {
                throw std::invalid_argument("cache hierarchy levels must not be null");
            }
        }
    }

    // Registers a cache access without filling the hierarchy on a miss.
    // A hit is promoted to L1 and returns the value already stored in the cache.
    [[nodiscard]] value_type* access(const key_type& key) {
        for (std::size_t level = 0; level < levels_.size(); ++level) {
            if (!levels_[level]->contains(key)) {
                continue;
            }

            ++hit_count_;
            if (level == 0) {
                levels_.front()->touch(key);
            } else {
                auto promoted = levels_[level]->extract(key);
                place(std::move(*promoted), 0);
            }
            return find(key);
        }

        return nullptr;
    }

    // Explicitly fills L1. Any eviction cascades through the lower levels.
    void insert(entry_type entry) {
        // Keep the hierarchy exclusive when a caller replaces an existing key.
        for (std::size_t level = 1; level < levels_.size(); ++level) {
            levels_[level]->erase(entry.key);
        }
        place(std::move(entry), 0);
    }

    [[nodiscard]] value_type* find(const key_type& key) {
        for (auto& level : levels_) {
            if (auto* value = level->find(key)) {
                return value;
            }
        }
        return nullptr;
    }

    [[nodiscard]] const value_type* find(const key_type& key) const {
        for (const auto& level : levels_) {
            if (const auto* value = std::as_const(*level).find(key)) {
                return value;
            }
        }
        return nullptr;
    }

    [[nodiscard]] std::size_t hits() const {
        return hit_count_;
    }

private:
    void place(entry_type entry, std::size_t first_level) {
        std::optional<entry_type> moving{std::move(entry)};

        for (std::size_t level = first_level; level < levels_.size() && moving; ++level) {
            moving = levels_[level]->insert(std::move(*moving));
        }
    }

    std::vector<std::unique_ptr<cache_type>> levels_;
    std::size_t hit_count_ = 0;
};
