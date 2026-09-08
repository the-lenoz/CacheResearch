module;

#include <algorithm>
#include <cstddef>
#include <functional>
#include <iterator>
#include <limits>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

export module cache.belady;

import cache;

export template <
    typename KeyType,
    typename Hash = std::hash<KeyType>,
    typename KeyEqual = std::equal_to<KeyType>
>
class BeladyCache {
public:
    explicit BeladyCache(std::size_t capacity, std::vector<KeyType> trace)
        : capacity_(capacity), trace_(std::move(trace)) {}

    [[nodiscard]] std::size_t run() const {
        if (capacity_ == 0 || trace_.empty()) {
            return 0;
        }

        constexpr auto never = std::numeric_limits<std::size_t>::max();

        std::vector<std::size_t> next_occurrence(trace_.size(), never);
        std::unordered_map<KeyType, std::size_t, Hash, KeyEqual> next_seen;
        next_seen.reserve(trace_.size());

        for (std::size_t index = trace_.size(); index-- > 0;) {
            const auto found = next_seen.find(trace_[index]);
            if (found != next_seen.end()) {
                next_occurrence[index] = found->second;
            }
            next_seen.insert_or_assign(trace_[index], index);
        }

        struct FutureUse {
            std::size_t next;
            std::size_t request_index;
            KeyType key;
        };

        struct EarlierUse {
            bool operator()(const FutureUse& left, const FutureUse& right) const {
                if (left.next != right.next) {
                    return left.next < right.next;
                }
                return left.request_index < right.request_index;
            }
        };

        using Schedule = std::set<FutureUse, EarlierUse>;
        Schedule schedule;
        std::unordered_map<
            KeyType,
            typename Schedule::iterator,
            Hash,
            KeyEqual
        > resident;
        resident.reserve(std::min(capacity_, trace_.size()));

        std::size_t hit_count = 0;

        for (std::size_t index = 0; index < trace_.size(); ++index) {
            const auto found = resident.find(trace_[index]);
            if (found != resident.end()) {
                ++hit_count;
                schedule.erase(found->second);
                found->second = schedule.emplace(FutureUse{
                    next_occurrence[index],
                    index,
                    trace_[index]
                }).first;
                continue;
            }

            if (resident.size() == capacity_) {
                const auto victim = std::prev(schedule.end());
                resident.erase(victim->key);
                schedule.erase(victim);
            }

            const auto position = schedule.emplace(FutureUse{
                next_occurrence[index],
                index,
                trace_[index]
            }).first;
            resident.emplace(trace_[index], position);
        }

        return hit_count;
    }

private:
    std::size_t capacity_;
    std::vector<KeyType> trace_;
};
