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

    explicit CacheHierarchy(std::vector<std::unique_ptr<cache_type>> levels)
        : levels_(std::move(levels)) {
        if (levels_.empty()) {
            throw std::invalid_argument("cache hierarchy must contain at least one level");
        }
    }

    [[nodiscard]] bool access(entry_type entry) {
        for (std::size_t level = 0; level < levels_.size(); ++level) {
            if (!levels_[level]->contains(entry.key)) {
                continue;
            }

            ++hit_count_;
            if (level == 0) {
                levels_.front()->touch(entry.key);
            } else {
                auto promoted = levels_[level]->extract(entry.key);
                place(std::move(*promoted), 0);
            }
            return true;
        }

        place(std::move(entry), 0);
        return false;
    }

    [[nodiscard]] ValueType* find(const KeyType& key) {
        for (auto& level : levels_) {
            if (auto* value = level->find(key)) {
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
