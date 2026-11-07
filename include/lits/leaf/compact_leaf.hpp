#pragma once

#include "lits/types.hpp"

#include <optional>
#include <string_view>
#include <vector>

namespace lits {

// Owner: Oyshi. Storage, h-pointers, and iterator design are TODO.
class CompactLeaf {
public:
    // Initial contract: insert returns false for a duplicate or a full leaf.
    bool insert(std::string_view key, Value value);
    std::optional<Value> lookup(std::string_view key) const;
    // Inclusive lower bound, ascending order, at most limit records.
    std::vector<Record> scan(std::string_view lower_bound, std::size_t limit) const;
};

} // namespace lits
