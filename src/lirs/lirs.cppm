module;

#include <algorithm>
#include <cstddef>
#include <functional>
#include <iterator>
#include <list>
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
class LIRSCache final : public Cache<KeyType, ValueType> {
public:
    using entry_type = CacheEntry<KeyType, ValueType>;

    explicit LIRSCache(std::size_t capacity)
        : capacity_(capacity),
          lir_capacity_(capacity == 0
              ? 0
              : capacity - std::max<std::size_t>(1, capacity / 100)) {}

    [[nodiscard]] ValueType* find(const KeyType& key) override {
        const auto found = entries_.find(key);
        return found == entries_.end() || !found->second.value
            ? nullptr
            : &*found->second.value;
    }

    [[nodiscard]] const ValueType* find(const KeyType& key) const override {
        const auto found = entries_.find(key);
        return found == entries_.end() || !found->second.value
            ? nullptr
            : &*found->second.value;
    }

    void touch(const KeyType& key) override {
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

    [[nodiscard]] std::optional<entry_type> insert(entry_type entry) override {
        const auto resident = entries_.find(entry.key);
        if (resident != entries_.end() && resident->second.value) {
            resident->second.value = std::move(entry.value);
            touch(resident->first);
            return std::nullopt;
        }

        if (capacity_ == 0) {
            return std::optional<entry_type>{std::move(entry)};
        }

        std::optional<entry_type> evicted;
        if (resident_count_ == capacity_) {
            evicted = evict_resident_hir();
        }

        const auto history = entries_.find(entry.key);
        if (history != entries_.end()) {
            history->second.value.emplace(std::move(entry.value));
            ++resident_count_;
            touch(history->first);
            return evicted;
        }

        const bool make_lir = lir_count_ < lir_capacity_;
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

    [[nodiscard]] std::optional<entry_type> extract(const KeyType& key) override {
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

    void clear() override {
        entries_.clear();
        stack_.clear();
        queue_.clear();
        resident_count_ = 0;
        lir_count_ = 0;
    }

    [[nodiscard]] std::size_t size() const override {
        return resident_count_;
    }

    [[nodiscard]] std::size_t capacity() const override {
        return capacity_;
    }

private:
    using KeyList = std::list<KeyType>;

    struct Node {
        std::optional<ValueType> value;
        bool is_lir = false;
        bool in_stack = false;
        typename KeyList::iterator stack_position{};
        bool in_queue = false;
        typename KeyList::iterator queue_position{};
    };

    void move_to_stack_front(const KeyType& key, Node& node) {
        if (node.in_stack) {
            stack_.erase(node.stack_position);
        }
        stack_.push_front(key);
        node.in_stack = true;
        node.stack_position = stack_.begin();
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
            if (!found->second.value && !found->second.in_queue) {
                entries_.erase(found);
            }
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
    std::size_t resident_count_ = 0;
    std::size_t lir_count_ = 0;
    KeyList stack_;
    KeyList queue_;
    std::unordered_map<KeyType, Node, Hash, KeyEqual> entries_;
    KeyEqual key_equal_{};
};
