module;

#include <cstddef>
#include <functional>
#include <iterator>
#include <list>
#include <optional>
#include <unordered_map>
#include <utility>

export module cache.lru;

import cache;

export template <
    typename KeyType,
    typename ValueType,
    typename Hash = std::hash<KeyType>,
    typename KeyEqual = std::equal_to<KeyType>
>
class LRUCache final {
public:
    using key_type = KeyType;
    using value_type = ValueType;
    using entry_type = CacheEntry<KeyType, ValueType>;

    explicit LRUCache(std::size_t capacity)
        : capacity_(capacity) {}

    [[nodiscard]] ValueType* find(const KeyType& key) {
        const auto found = index_.find(key);
        return found == index_.end() ? nullptr : &found->second->value;
    }

    [[nodiscard]] const ValueType* find(const KeyType& key) const {
        const auto found = index_.find(key);
        return found == index_.end() ? nullptr : &found->second->value;
    }

    void touch(const KeyType& key) {
        const auto found = index_.find(key);
        if (found != index_.end()) {
            entries_.splice(entries_.begin(), entries_, found->second);
        }
    }

    [[nodiscard]] std::optional<entry_type> insert(entry_type entry) {
        const auto found = index_.find(entry.key);
        if (found != index_.end()) {
            found->second->value = std::move(entry.value);
            entries_.splice(entries_.begin(), entries_, found->second);
            return std::nullopt;
        }

        if (capacity_ == 0) {
            return std::optional<entry_type>{std::move(entry)};
        }

        std::optional<entry_type> evicted;
        if (entries_.size() == capacity_) {
            const auto victim = std::prev(entries_.end());
            index_.erase(victim->key);
            evicted.emplace(std::move(*victim));
            entries_.erase(victim);
        }

        entries_.push_front(std::move(entry));
        index_.emplace(entries_.front().key, entries_.begin());
        return evicted;
    }

    [[nodiscard]] std::optional<entry_type> extract(const KeyType& key) {
        const auto found = index_.find(key);
        if (found == index_.end()) {
            return std::nullopt;
        }

        const auto position = found->second;
        entry_type extracted = std::move(*position);
        entries_.erase(position);
        index_.erase(found);
        return std::optional<entry_type>{std::move(extracted)};
    }

    void clear() {
        index_.clear();
        entries_.clear();
    }

    [[nodiscard]] std::size_t size() const {
        return entries_.size();
    }

    [[nodiscard]] std::size_t capacity() const {
        return capacity_;
    }

    [[nodiscard]] bool contains(const KeyType& key) const {
        return find(key) != nullptr;
    }

    void erase(const KeyType& key) {
        static_cast<void>(extract(key));
    }

    [[nodiscard]] std::size_t shadow_size() const { return 0; }
    [[nodiscard]] std::size_t shadow_capacity() const { return 0; }

private:
    using EntryList = std::list<entry_type>;

    std::size_t capacity_;
    EntryList entries_;
    std::unordered_map<
        KeyType,
        typename EntryList::iterator,
        Hash,
        KeyEqual
    > index_;
};
