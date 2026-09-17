module;

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

export module cache;

export using DefaultKey = int;
export using DefaultValue = std::vector<std::byte>;

export template <typename KeyType, typename ValueType>
struct CacheEntry {
    KeyType key;
    ValueType value;
};

export template <typename ValueType>
class CacheAccessResult {
public:
    CacheAccessResult(bool hit, ValueType* cached)
        : hit_(hit), cached_(cached) {}

    explicit CacheAccessResult(ValueType uncached)
        : hit_(false), uncached_(std::move(uncached)) {}

    [[nodiscard]] bool hit() const { return hit_; }

    [[nodiscard]] ValueType& value() {
        return uncached_ ? *uncached_ : *cached_;
    }

    [[nodiscard]] const ValueType& value() const {
        return uncached_ ? *uncached_ : *cached_;
    }

private:
    bool hit_;
    ValueType* cached_ = nullptr;
    std::optional<ValueType> uncached_;
};
