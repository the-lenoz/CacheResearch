module;

#include <cstddef>
#include <optional>
#include <utility>
#include <variant>

export module cache;

export using DefaultKey = int;
export using DefaultValue = std::monostate;

export template <typename KeyType, typename ValueType>
struct CacheEntry {
    KeyType key;
    ValueType value;
};

export template <typename KeyType, typename ValueType>
class Cache {
public:
    using key_type = KeyType;
    using value_type = ValueType;
    using entry_type = CacheEntry<key_type, value_type>;

    virtual ~Cache() = default;

    [[nodiscard]] virtual value_type* find(const key_type& key) = 0;
    [[nodiscard]] virtual const value_type* find(const key_type& key) const = 0;
    virtual void touch(const key_type& key) = 0;

    [[nodiscard]] virtual std::optional<entry_type> insert(entry_type entry) = 0;
    [[nodiscard]] virtual std::optional<entry_type> extract(const key_type& key) = 0;

    virtual void clear() = 0;

    [[nodiscard]] virtual std::size_t size() const = 0;
    [[nodiscard]] virtual std::size_t capacity() const = 0;

    [[nodiscard]] bool contains(const key_type& key) const {
        return find(key) != nullptr;
    }

    void erase(const key_type& key) {
        static_cast<void>(extract(key));
    }
};
