module;

#include <cstddef>
#include <optional>

export module cache.lfu;

import cache;

export class LFUCache final : public Cache {
public:
    explicit LFUCache(std::size_t capacity);

    [[nodiscard]] bool contains(Key key) const override;
    void touch(Key key) override;
    [[nodiscard]] std::optional<Key> insert(Key key) override;
    void erase(Key key) override;
    void clear() override;

    [[nodiscard]] std::size_t size() const override;
    [[nodiscard]] std::size_t capacity() const override;
};
