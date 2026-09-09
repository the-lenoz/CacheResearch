module;

#include <algorithm>
#include <cstddef>
#include <functional>
#include <iterator>
#include <list>
#include <optional>
#include <unordered_map>
#include <utility>

export module cache.arc;

import cache;

export template <
    typename KeyType,
    typename ValueType,
    typename Hash = std::hash<KeyType>,
    typename KeyEqual = std::equal_to<KeyType>
>
class ARCCache final : public Cache<KeyType, ValueType> {
public:
    using entry_type = CacheEntry<KeyType, ValueType>;

    explicit ARCCache(std::size_t capacity)
        : capacity_(capacity) {}

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

        if (found->second.list == ResidentList::T1) {
            t1_.erase(found->second.position);
            t2_.push_front(found->first);
            found->second.list = ResidentList::T2;
            found->second.position = t2_.begin();
        } else {
            t2_.splice(t2_.begin(), t2_, found->second.position);
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

        const auto b1_hit = b1_index_.find(entry.key);
        if (b1_hit != b1_index_.end()) {
            const std::size_t delta = std::max<std::size_t>(
                1,
                b2_.size() / b1_.size()
            );
            target_t1_size_ = std::min(capacity_, target_t1_size_ + delta);

            b1_.erase(b1_hit->second);
            b1_index_.erase(b1_hit);
            auto evicted = resident_.size() == capacity_
                ? replace(false)
                : std::optional<entry_type>{};
            insert_resident(std::move(entry), ResidentList::T2);
            trim_history();
            return evicted;
        }

        const auto b2_hit = b2_index_.find(entry.key);
        if (b2_hit != b2_index_.end()) {
            const std::size_t delta = std::max<std::size_t>(
                1,
                b1_.size() / b2_.size()
            );
            target_t1_size_ = delta > target_t1_size_
                ? 0
                : target_t1_size_ - delta;

            b2_.erase(b2_hit->second);
            b2_index_.erase(b2_hit);
            auto evicted = resident_.size() == capacity_
                ? replace(true)
                : std::optional<entry_type>{};
            insert_resident(std::move(entry), ResidentList::T2);
            trim_history();
            return evicted;
        }

        std::optional<entry_type> evicted;
        if (t1_.size() + b1_.size() == capacity_) {
            if (t1_.size() < capacity_) {
                remove_oldest_ghost(b1_, b1_index_);
                if (resident_.size() == capacity_) {
                    evicted = replace(false);
                }
            } else {
                evicted = evict_resident(ResidentList::T1, false);
            }
        } else if (t1_.size() + b1_.size() < capacity_) {
            const std::size_t total_size = resident_.size() + b1_.size() + b2_.size();
            if (total_size >= capacity_) {
                if (total_size == 2 * capacity_) {
                    if (!b2_.empty()) {
                        remove_oldest_ghost(b2_, b2_index_);
                    } else {
                        remove_oldest_ghost(b1_, b1_index_);
                    }
                }
                if (resident_.size() == capacity_) {
                    evicted = replace(false);
                }
            }
        } else if (resident_.size() == capacity_) {
            evicted = replace(false);
        }

        insert_resident(std::move(entry), ResidentList::T1);
        trim_history();
        return evicted;
    }

    [[nodiscard]] std::optional<entry_type> extract(const KeyType& key) override {
        const auto found = resident_.find(key);
        if (found == resident_.end()) {
            return std::nullopt;
        }

        auto& list = found->second.list == ResidentList::T1 ? t1_ : t2_;
        list.erase(found->second.position);
        entry_type extracted{found->first, std::move(found->second.value)};
        resident_.erase(found);
        return std::optional<entry_type>{std::move(extracted)};
    }

    void clear() override {
        resident_.clear();
        b1_index_.clear();
        b2_index_.clear();
        t1_.clear();
        t2_.clear();
        b1_.clear();
        b2_.clear();
        target_t1_size_ = 0;
    }

    [[nodiscard]] std::size_t size() const override {
        return resident_.size();
    }

    [[nodiscard]] std::size_t capacity() const override {
        return capacity_;
    }

private:
    using KeyList = std::list<KeyType>;
    using GhostIndex = std::unordered_map<
        KeyType,
        typename KeyList::iterator,
        Hash,
        KeyEqual
    >;

    enum class ResidentList {
        T1,
        T2
    };

    struct StoredValue {
        ValueType value;
        ResidentList list;
        typename KeyList::iterator position;
    };

    void insert_resident(entry_type entry, ResidentList destination) {
        auto& list = destination == ResidentList::T1 ? t1_ : t2_;
        list.push_front(entry.key);
        resident_.emplace(
            std::move(entry.key),
            StoredValue{std::move(entry.value), destination, list.begin()}
        );
    }

    void remember_ghost(KeyList& list, GhostIndex& index, const KeyType& key) {
        list.push_front(key);
        index.emplace(list.front(), list.begin());
    }

    void remove_oldest_ghost(KeyList& list, GhostIndex& index) {
        if (list.empty()) {
            return;
        }
        const auto oldest = std::prev(list.end());
        index.erase(*oldest);
        list.erase(oldest);
    }

    [[nodiscard]] std::optional<entry_type> evict_resident(
        ResidentList source,
        bool keep_history
    ) {
        auto& list = source == ResidentList::T1 ? t1_ : t2_;
        const auto position = std::prev(list.end());
        const auto victim = resident_.find(*position);
        entry_type evicted{victim->first, std::move(victim->second.value)};

        if (keep_history) {
            if (source == ResidentList::T1) {
                remember_ghost(b1_, b1_index_, victim->first);
            } else {
                remember_ghost(b2_, b2_index_, victim->first);
            }
        }

        list.erase(position);
        resident_.erase(victim);
        return std::optional<entry_type>{std::move(evicted)};
    }

    [[nodiscard]] std::optional<entry_type> replace(bool requested_from_b2) {
        const bool evict_t1 = !t1_.empty()
            && (t1_.size() > target_t1_size_
                || (requested_from_b2 && t1_.size() == target_t1_size_));

        if (evict_t1 || t2_.empty()) {
            return evict_resident(ResidentList::T1, true);
        }
        return evict_resident(ResidentList::T2, true);
    }

    void trim_history() {
        while (b1_.size() + b2_.size() > capacity_) {
            if (!b2_.empty() && b2_.size() > target_t1_size_) {
                remove_oldest_ghost(b2_, b2_index_);
            } else if (!b1_.empty()) {
                remove_oldest_ghost(b1_, b1_index_);
            } else {
                remove_oldest_ghost(b2_, b2_index_);
            }
        }
    }

    std::size_t capacity_;
    std::size_t target_t1_size_ = 0;
    KeyList t1_;
    KeyList t2_;
    KeyList b1_;
    KeyList b2_;
    std::unordered_map<KeyType, StoredValue, Hash, KeyEqual> resident_;
    GhostIndex b1_index_;
    GhostIndex b2_index_;
};
