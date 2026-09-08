module;

#include <cstddef>
#include <memory>
#include <vector>

export module cache.hierarchy;

import cache;

export class CacheHierarchy {
public:
    explicit CacheHierarchy(std::vector<std::unique_ptr<Cache>> levels);

    [[nodiscard]] bool access(Key key);
    [[nodiscard]] std::size_t hits() const;
};
