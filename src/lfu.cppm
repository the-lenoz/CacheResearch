module;

#include <cstddef>
#include <functional>
#include <iterator>
#include <list>
#include <optional>
#include <unordered_map>
#include <utility>

export module cache.lfu;

import cache;

export template <
    typename KeyType,
    typename ValueType,
    typename Hash = std::hash<KeyType>,
    typename KeyEqual = std::equal_to<KeyType>
>
class LFUCache final {
public:
    using key_type = KeyType;
    using value_type = ValueType;
    using entry_type = CacheEntry<KeyType, ValueType>;

    explicit LFUCache(std::size_t capacity)
        : capacity_(capacity) {}

    [[nodiscard]] ValueType* find(const KeyType& key) {
        const auto found = entries_.find(key);
        return found == entries_.end() ? nullptr : &found->second.value;
    }

    [[nodiscard]] const ValueType* find(const KeyType& key) const {
        const auto found = entries_.find(key);
        return found == entries_.end() ? nullptr : &found->second.value;
    }

    void touch(const KeyType& key) {
        const auto found = entries_.find(key);
        if (found == entries_.end()) {
            return;
        }

        const auto current_bucket = found->second.bucket;
        auto next_bucket = std::next(current_bucket);
        const std::size_t next_frequency = current_bucket->frequency + 1;

        if (next_bucket == buckets_.end() || next_bucket->frequency != next_frequency) {
            next_bucket = buckets_.insert(
                next_bucket,
                FrequencyBucket{next_frequency, {}}
            );
        }

        next_bucket->keys.push_front(found->first);
        current_bucket->keys.erase(found->second.position);
        found->second.bucket = next_bucket;
        found->second.position = next_bucket->keys.begin();

        if (current_bucket->keys.empty()) {
            buckets_.erase(current_bucket);
        }
    }

    [[nodiscard]] std::optional<entry_type> insert(entry_type entry) {
        const auto found = entries_.find(entry.key);
        if (found != entries_.end()) {
            found->second.value = std::move(entry.value);
            touch(found->first);
            return std::nullopt;
        }

        if (capacity_ == 0) {
            return std::optional<entry_type>{std::move(entry)};
        }

        std::optional<entry_type> evicted;
        if (entries_.size() == capacity_) {
            auto lowest_frequency = buckets_.begin();
            const KeyType& victim_key = lowest_frequency->keys.back();
            const auto victim = entries_.find(victim_key);

            evicted.emplace(entry_type{victim->first, std::move(victim->second.value)});
            lowest_frequency->keys.pop_back();
            entries_.erase(victim);

            if (lowest_frequency->keys.empty()) {
                buckets_.erase(lowest_frequency);
            }
        }

        auto frequency_one = buckets_.begin();
        if (frequency_one == buckets_.end() || frequency_one->frequency != 1) {
            frequency_one = buckets_.insert(
                frequency_one,
                FrequencyBucket{1, {}}
            );
        }

        frequency_one->keys.push_front(entry.key);
        const auto position = frequency_one->keys.begin();
        entries_.emplace(
            std::move(entry.key),
            StoredValue{std::move(entry.value), frequency_one, position}
        );
        return evicted;
    }

    [[nodiscard]] std::optional<entry_type> extract(const KeyType& key) {
        const auto found = entries_.find(key);
        if (found == entries_.end()) {
            return std::nullopt;
        }

        const auto bucket = found->second.bucket;
        entry_type extracted{found->first, std::move(found->second.value)};
        bucket->keys.erase(found->second.position);
        entries_.erase(found);

        if (bucket->keys.empty()) {
            buckets_.erase(bucket);
        }

        return std::optional<entry_type>{std::move(extracted)};
    }

    void clear() {
        entries_.clear();
        buckets_.clear();
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
    using KeyList = std::list<KeyType>;

    struct FrequencyBucket {
        std::size_t frequency;
        KeyList keys;
    };

    using BucketList = std::list<FrequencyBucket>;

    struct StoredValue {
        ValueType value;
        BucketList::iterator bucket;
        KeyList::iterator position;
    };

    std::size_t capacity_;
    BucketList buckets_;
    std::unordered_map<KeyType, StoredValue, Hash, KeyEqual> entries_;
};
