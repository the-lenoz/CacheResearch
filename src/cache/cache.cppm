module;

#include <cstddef>
#include <optional>

export module cache;

export using Key = int;

export class Cache {
public:
    virtual ~Cache() = default;

    [[nodiscard]] virtual bool contains(Key key) const = 0;
    virtual void touch(Key key) = 0;
    [[nodiscard]] virtual std::optional<Key> insert(Key key) = 0;
    virtual void erase(Key key) = 0;
    virtual void clear() = 0;

    [[nodiscard]] virtual std::size_t size() const = 0;
    [[nodiscard]] virtual std::size_t capacity() const = 0;
};
