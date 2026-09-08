module;

#include <cstddef>
#include <functional>
#include <optional>

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

    explicit TwoQCache(std::size_t capacity);

    [[nodiscard]] ValueType* find(const KeyType& key) override;
    [[nodiscard]] const ValueType* find(const KeyType& key) const override;
    void touch(const KeyType& key) override;
    [[nodiscard]] std::optional<entry_type> insert(entry_type entry) override;
    [[nodiscard]] std::optional<entry_type> extract(const KeyType& key) override;
    void clear() override;

    [[nodiscard]] std::size_t size() const override;
    [[nodiscard]] std::size_t capacity() const override;
};
