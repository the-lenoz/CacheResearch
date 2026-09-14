module;

#include <algorithm>
#include <cstddef>
#include <functional>
#include <iterator>
#include <list>
#include <optional>
#include <unordered_map>
#include <utility>

export module cache.two_q;

import cache;

export template <
    typename KeyType,
    typename ValueType,
    typename Hash = std::hash<KeyType>,
    typename KeyEqual = std::equal_to<KeyType>
>
class TwoQCache final : public Cache<KeyType, ValueType> {
public:
    using entry_type = CacheEntry<KeyType, ValueType>;

    explicit TwoQCache(std::size_t capacity)
        : TwoQCache(capacity, default_shadow_capacity(capacity)) {}

    TwoQCache(std::size_t capacity, std::size_t max_shadow_items)
        : capacity_(capacity),
          a1in_capacity_(default_a1in_capacity(capacity)),
          a1out_capacity_(max_shadow_items) {}

    [[nodiscard]] static constexpr std::size_t default_shadow_capacity(
        std::size_t capacity
    ) {
        return capacity == 0 ? 0 : std::max<std::size_t>(1, capacity / 2);
    }

    [[nodiscard]] ValueType* find(const KeyType& key) override {
        const auto found = resident_.find(key);
        return found == resident_.end() ? nullptr : &found->second.value;
    }

    [[nodiscard]] const ValueType* find(const KeyType& key) const override {
        const auto found = resident_.find(key);
        return found == resident_.end() ? nullptr : &found->second.value;
    }

    void touch(const KeyType& key) override {
        const auto found = resident_.find(key);
        if (found == resident_.end()) {
            return;
        }

        if (found->second.queue == Queue::A1in) {
            a1in_.erase(found->second.position);
            am_.push_front(found->first);
            found->second.queue = Queue::Am;
            found->second.position = am_.begin();
        } else {
            am_.splice(am_.begin(), am_, found->second.position);
        }
    }

    [[nodiscard]] std::optional<entry_type> insert(entry_type entry) override {
        const auto resident = resident_.find(entry.key);
        if (resident != resident_.end()) {
            resident->second.value = std::move(entry.value);
            touch(resident->first);
            return std::nullopt;
        }

        if (capacity_ == 0) {
            return std::optional<entry_type>{std::move(entry)};
        }

        const auto ghost = a1out_index_.find(entry.key);
        const bool ghost_hit = ghost != a1out_index_.end();
        if (ghost_hit) {
            a1out_.erase(ghost->second);
            a1out_index_.erase(ghost);
        }

        std::optional<entry_type> evicted;
        if (resident_.size() == capacity_) {
            const bool prefer_a1in = ghost_hit
                ? a1in_.size() > a1in_capacity_
                : a1in_.size() >= a1in_capacity_;
            evicted = evict_one(prefer_a1in);
        }

        auto& destination = ghost_hit ? am_ : a1in_;
        destination.push_front(entry.key);
        resident_.emplace(
            std::move(entry.key),
            StoredValue{
                std::move(entry.value),
                ghost_hit ? Queue::Am : Queue::A1in,
                destination.begin()
            }
        );
        return evicted;
    }

    [[nodiscard]] std::optional<entry_type> extract(const KeyType& key) override {
        const auto found = resident_.find(key);
        if (found == resident_.end()) {
            return std::nullopt;
        }

        auto& queue = found->second.queue == Queue::A1in ? a1in_ : am_;
        queue.erase(found->second.position);
        entry_type extracted{found->first, std::move(found->second.value)};
        resident_.erase(found);
        return std::optional<entry_type>{std::move(extracted)};
    }

    void clear() override {
        resident_.clear();
        a1out_index_.clear();
        a1in_.clear();
        am_.clear();
        a1out_.clear();
    }

    [[nodiscard]] std::size_t size() const override {
        return resident_.size();
    }

    [[nodiscard]] std::size_t capacity() const override {
        return capacity_;
    }

    [[nodiscard]] std::size_t shadow_size() const override {
        return a1out_.size();
    }

    [[nodiscard]] std::size_t shadow_capacity() const override {
        return a1out_capacity_;
    }

private:
    using KeyList = std::list<KeyType>;

    enum class Queue {
        A1in,
        Am
    };

    struct StoredValue {
        ValueType value;
        Queue queue;
        KeyList::iterator position;
    };

    // TODO(tuning): expose the A1in share through policy configuration.
    [[nodiscard]] static std::size_t default_a1in_capacity(std::size_t capacity) {
        return capacity == 0 ? 0 : std::max<std::size_t>(1, capacity / 4);
    }

    void remember_ghost(const KeyType& key) {
        if (a1out_capacity_ == 0) {
            return;
        }

        a1out_.push_front(key);
        a1out_index_.emplace(a1out_.front(), a1out_.begin());

        if (a1out_.size() > a1out_capacity_) {
            const auto oldest = std::prev(a1out_.end());
            a1out_index_.erase(*oldest);
            a1out_.erase(oldest);
        }
    }

    [[nodiscard]] std::optional<entry_type> evict_one(bool prefer_a1in) {
        const bool evict_a1in = !a1in_.empty() && (prefer_a1in || am_.empty());
        auto& queue = evict_a1in ? a1in_ : am_;
        const auto victim_position = std::prev(queue.end());
        const auto victim = resident_.find(*victim_position);

        entry_type evicted{victim->first, std::move(victim->second.value)};
        if (evict_a1in) {
            remember_ghost(victim->first);
        }

        queue.erase(victim_position);
        resident_.erase(victim);
        return std::optional<entry_type>{std::move(evicted)};
    }

    std::size_t capacity_;
    std::size_t a1in_capacity_;
    std::size_t a1out_capacity_;
    KeyList a1in_;
    KeyList am_;
    KeyList a1out_;
    std::unordered_map<KeyType, StoredValue, Hash, KeyEqual> resident_;
    std::unordered_map<
        KeyType,
        typename KeyList::iterator,
        Hash,
        KeyEqual
    > a1out_index_;
};
