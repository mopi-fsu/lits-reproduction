#pragma once

#include "lits/core/node.hpp"
#include "lits/types.hpp"

#include <cstddef>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace lits {

// Owner: Bach. Single-threaded first.
//
// Ownership: the index owns copies of every key; caller storage is never
// retained. Move-only (a deep copy is never needed). A moved-from index is empty.
//
// Implemented today: input validation, empty/single-record states, and
// operations on them. Anything that needs a learned node, compact leaf or HOT
// subtrie throws std::logic_error (never a silent fallback); see
// docs/interfaces.md for the exact table.
class LitsIndex {
public:
    LitsIndex() = default;
    LitsIndex(LitsIndex&& other) noexcept
        : root_(std::move(other.root_)), size_(std::exchange(other.size_, 0)) {}
    LitsIndex& operator=(LitsIndex&& other) noexcept {
        if (this != &other) {
            root_ = std::move(other.root_);
            size_ = std::exchange(other.size_, 0);
        }
        return *this;
    }
    LitsIndex(const LitsIndex&) = delete;
    LitsIndex& operator=(const LitsIndex&) = delete;

    // Replaces the entire contents. Input must be sorted, unique, supported keys
    // (throws std::invalid_argument and leaves the index unchanged otherwise).
    // Empty input is valid and clears the index. Currently only 0 or 1 records
    // can be built; more throws std::logic_error and leaves the index unchanged.
    void bulkload(const std::vector<Record>& sorted_records);
    std::optional<Value> lookup(std::string_view key) const;
    bool insert(std::string_view key, Value value); // false for an existing key
    bool update(std::string_view key, Value value); // false for a missing key
    bool erase(std::string_view key);               // false for a missing key
    // Inclusive lower bound (empty = from the start), ascending, at most limit.
    std::vector<Record> scan(std::string_view lower_bound, std::size_t limit) const;

    std::size_t size() const noexcept { return size_; }
    bool empty() const noexcept { return size_ == 0; }
    NodeKind root_kind() const noexcept { return root_.kind(); }

private:
    Slot root_;
    std::size_t size_ = 0;
};

} // namespace lits
