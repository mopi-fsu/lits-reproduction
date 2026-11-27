#pragma once

#include "lits/types.hpp"

#include <optional>
#include <string_view>
#include <vector>

namespace lits {

// Result of CompactLeaf::insert, agreed with Bach (core). The core promotes the
// leaf only on `full`; `duplicate` means the key is already stored.
enum class LeafInsert { inserted, duplicate, full };

// Owner: Oyshi. Simple owned storage for now: an exactly sized sorted array of
// records searched by binary search. The paper's h-pointer layout, hash
// filtering, and scan iterator are planned in docs/interfaces.md. This is not
// the paper's memory layout.
class CompactLeaf {
public:
    // Returns `inserted`, `duplicate` (key already stored, even if the leaf is
    // full), or `full`. Only `inserted` changes the leaf, and it copies the key.
    // Throws std::invalid_argument for an unsupported key. [[nodiscard]] makes the
    // compiler warn if a caller ignores the result, since ignoring `full` would
    // silently drop the key.
    [[nodiscard]] LeafInsert insert(std::string_view key, Value value);
    // Throws std::invalid_argument for an unsupported key.
    [[nodiscard]] std::optional<Value> lookup(std::string_view key) const;
    // Inclusive lower bound, ascending order, at most limit records. An empty
    // lower bound starts at the first record; any other unsupported lower bound
    // throws std::invalid_argument, even when limit is zero.
    [[nodiscard]] std::vector<Record> scan(std::string_view lower_bound,
                                           std::size_t limit) const;

private:
    std::vector<Record> records_; // Sorted by key, unique, at most capacity.
};

} // namespace lits
