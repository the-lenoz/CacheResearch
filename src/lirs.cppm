module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <list>
#include <limits>
#include <map>
#include <optional>
#include <unordered_map>
#include <utility>

export module cache.lirs;

import cache;

export template <
    typename KeyType,
    typename ValueType,
    typename Hash = std::hash<KeyType>,
    typename KeyEqual = std::equal_to<KeyType>
>
class LIRSCache final {
public:
    using key_type = KeyType;
    using value_type = ValueType;
    using entry_type = CacheEntry<KeyType, ValueType>;

    explicit LIRSCache(std::size_t capacity)
        : LIRSCache(capacity, default_shadow_capacity(capacity)) {}

    LIRSCache(std::size_t capacity, std::size_t max_shadow_items)
        : capacity_(capacity),
          lir_capacity_(default_lir_capacity(capacity)),
          shadow_capacity_(max_shadow_items) {}

    [[nodiscard]] static constexpr std::size_t default_shadow_capacity(
        std::size_t capacity
    ) {
        return capacity;
    }

    [[nodiscard]] ValueType* find(const KeyType& key) {
        const auto found = entries_.find(key);
        return found == entries_.end() || !found->second.value
            ? nullptr
            : &*found->second.value;
    }

    [[nodiscard]] const ValueType* find(const KeyType& key) const {
        const auto found = entries_.find(key);
        return found == entries_.end() || !found->second.value
            ? nullptr
            : &*found->second.value;
    }

    void touch(const KeyType& key) {
        const auto found = entries_.find(key);
        if (found == entries_.end() || !found->second.value) {
            return;
        }

        Node& node = found->second;
        if (node.is_lir) {
            move_to_stack_front(found->first, node);
            prune_stack();
            return;
        }

        const bool was_in_stack = node.in_stack;
        move_to_stack_front(found->first, node);
        remove_from_queue(node);

        if (was_in_stack && lir_capacity_ > 0) {
            node.is_lir = true;
            ++lir_count_;
            if (lir_count_ > lir_capacity_) {
                demote_bottom_lir(found->first);
            }
        } else {
            move_to_queue_front(found->first, node);
        }

        prune_stack();
    }

    [[nodiscard]] std::optional<entry_type> insert(entry_type entry) {
        const auto resident = entries_.find(entry.key);
        if (resident != entries_.end() && resident->second.value) {
            resident->second.value = std::move(entry.value);
            touch(resident->first);
            return std::nullopt;
        }

        if (capacity_ == 0) {
            return std::optional<entry_type>{std::move(entry)};
        }

        const auto history = entries_.find(entry.key);
        if (history != entries_.end()) {
            history->second.value.emplace(std::move(entry.value));
            shadow_index_.erase(history->second.stack_epoch);
            ++resident_count_;
            std::optional<entry_type> evicted;
            if (resident_count_ > capacity_) {
                evicted = evict_resident_hir();
            }
            touch(history->first);
            return evicted;
        }

        std::optional<entry_type> evicted;
        if (resident_count_ == capacity_) {
            evicted = evict_resident_hir();
        }

        const bool make_lir = lir_count_ < lir_capacity_;
        const std::uint64_t stack_epoch = next_stack_epoch();
        stack_.push_front(entry.key);
        typename KeyList::iterator queue_position{};
        bool in_queue = false;

        if (!make_lir) {
            queue_.push_front(entry.key);
            queue_position = queue_.begin();
            in_queue = true;
        }

        entries_.emplace(
            std::move(entry.key),
            Node{
                std::optional<ValueType>{std::move(entry.value)},
                make_lir,
                true,
                stack_.begin(),
                stack_epoch,
                in_queue,
                queue_position
            }
        );
        ++resident_count_;
        if (make_lir) {
            ++lir_count_;
        }
        prune_stack();
        return evicted;
    }

    [[nodiscard]] std::optional<entry_type> extract(const KeyType& key) {
        const auto found = entries_.find(key);
        if (found == entries_.end() || !found->second.value) {
            return std::nullopt;
        }

        Node& node = found->second;
        entry_type extracted{found->first, std::move(*node.value)};
        if (node.in_stack) {
            stack_.erase(node.stack_position);
        }
        remove_from_queue(node);
        if (node.is_lir) {
            --lir_count_;
        }
        --resident_count_;
        entries_.erase(found);

        fill_lir_quota();
        prune_stack();
        return std::optional<entry_type>{std::move(extracted)};
    }

    void clear() {
        shadow_index_.clear();
        entries_.clear();
        stack_.clear();
        queue_.clear();
        resident_count_ = 0;
        lir_count_ = 0;
        next_stack_epoch_ = 0;
    }

    [[nodiscard]] std::size_t size() const {
        return resident_count_;
    }

    [[nodiscard]] std::size_t capacity() const {
        return capacity_;
    }

    [[nodiscard]] std::size_t shadow_size() const {
        return shadow_index_.size();
    }

    [[nodiscard]] std::size_t shadow_capacity() const {
        return shadow_capacity_;
    }

    [[nodiscard]] bool contains(const KeyType& key) const {
        return find(key) != nullptr;
    }

    void erase(const KeyType& key) {
        static_cast<void>(extract(key));
    }

private:
    using KeyList = std::list<KeyType>;

    struct Node {
        std::optional<ValueType> value;
        bool is_lir = false;
        bool in_stack = false;
        KeyList::iterator stack_position{};
        std::uint64_t stack_epoch = 0;
        bool in_queue = false;
        KeyList::iterator queue_position{};
    };

    // TODO(tuning): expose the resident HIR share (currently 1%, at least one)
    // through policy configuration.
    [[nodiscard]] static std::size_t default_lir_capacity(std::size_t capacity) {
        return capacity == 0
            ? 0
            : capacity - std::max<std::size_t>(1, capacity / 100);
    }

    void move_to_stack_front(const KeyType& key, Node& node) {
        const std::uint64_t stack_epoch = next_stack_epoch();
        if (node.in_stack) {
            stack_.erase(node.stack_position);
        }
        stack_.push_front(key);
        node.in_stack = true;
        node.stack_position = stack_.begin();
        node.stack_epoch = stack_epoch;
    }

    [[nodiscard]] std::uint64_t next_stack_epoch() {
        if (next_stack_epoch_ == std::numeric_limits<std::uint64_t>::max()) {
            // Rebase only after 2^64 stack insertions/moves; S order is unchanged.
            shadow_index_.clear();
            std::uint64_t epoch = 0;
            for (auto position = stack_.rbegin(); position != stack_.rend(); ++position) {
                const auto found = entries_.find(*position);
                if (found == entries_.end()) {
                    continue;
                }
                found->second.stack_epoch = ++epoch;
                if (!found->second.value) {
                    shadow_index_.emplace(epoch, found->first);
                }
            }
            next_stack_epoch_ = epoch;
        }
        return ++next_stack_epoch_;
    }

    void remove_from_queue(Node& node) {
        if (node.in_queue) {
            queue_.erase(node.queue_position);
            node.in_queue = false;
        }
    }

    void move_to_queue_front(const KeyType& key, Node& node) {
        remove_from_queue(node);
        queue_.push_front(key);
        node.in_queue = true;
        node.queue_position = queue_.begin();
    }

    void demote_bottom_lir(const KeyType& promoted_key) {
        for (auto position = stack_.rbegin(); position != stack_.rend(); ++position) {
            const auto candidate = entries_.find(*position);
            if (candidate != entries_.end()
                && candidate->second.is_lir
                && !key_equal_(candidate->first, promoted_key)) {
                candidate->second.is_lir = false;
                --lir_count_;
                move_to_queue_front(candidate->first, candidate->second);
                return;
            }
        }
    }

    void fill_lir_quota() {
        while (lir_count_ < lir_capacity_ && !queue_.empty()) {
            const KeyType key = queue_.front();
            const auto candidate = entries_.find(key);
            remove_from_queue(candidate->second);
            candidate->second.is_lir = true;
            ++lir_count_;
            move_to_stack_front(candidate->first, candidate->second);
        }
    }

    void prune_stack() {
        while (!stack_.empty()) {
            const auto found = entries_.find(stack_.back());
            if (found != entries_.end() && found->second.is_lir) {
                break;
            }

            if (found == entries_.end()) {
                stack_.pop_back();
                continue;
            }

            found->second.in_stack = false;
            stack_.pop_back();
            if (!found->second.value) {
                shadow_index_.erase(found->second.stack_epoch);
                if (!found->second.in_queue) {
                    entries_.erase(found);
                }
            }
        }
    }

    void trim_shadow_history() {
        while (shadow_index_.size() > shadow_capacity_) {
            const auto oldest = shadow_index_.begin();
            const auto shadow = entries_.find(oldest->second);
            shadow->second.in_stack = false;
            stack_.erase(shadow->second.stack_position);
            shadow_index_.erase(oldest);
            entries_.erase(shadow);
        }
    }

    [[nodiscard]] std::optional<entry_type> evict_resident_hir() {
        if (queue_.empty()) {
            return evict_bottom_lir();
        }

        const auto found = entries_.find(queue_.back());
        entry_type evicted{found->first, std::move(*found->second.value)};
        queue_.pop_back();
        found->second.in_queue = false;
        found->second.value.reset();
        --resident_count_;

        if (!found->second.in_stack) {
            entries_.erase(found);
        } else if (shadow_capacity_ == 0) {
            stack_.erase(found->second.stack_position);
            entries_.erase(found);
        } else {
            shadow_index_.emplace(found->second.stack_epoch, found->first);
            trim_shadow_history();
        }
        return std::optional<entry_type>{std::move(evicted)};
    }

    [[nodiscard]] std::optional<entry_type> evict_bottom_lir() {
        for (auto position = stack_.rbegin(); position != stack_.rend(); ++position) {
            const auto found = entries_.find(*position);
            if (found != entries_.end() && found->second.value) {
                entry_type evicted{found->first, std::move(*found->second.value)};
                const auto stack_position = found->second.stack_position;
                if (found->second.is_lir) {
                    --lir_count_;
                }
                stack_.erase(stack_position);
                entries_.erase(found);
                --resident_count_;
                return std::optional<entry_type>{std::move(evicted)};
            }
        }
        return std::nullopt;
    }

    std::size_t capacity_;
    std::size_t lir_capacity_;
    std::size_t shadow_capacity_;
    std::size_t resident_count_ = 0;
    std::size_t lir_count_ = 0;
    std::uint64_t next_stack_epoch_ = 0;
    KeyList stack_;
    KeyList queue_;
    std::map<std::uint64_t, KeyType> shadow_index_;
    std::unordered_map<KeyType, Node, Hash, KeyEqual> entries_;
    KeyEqual key_equal_{};
};
