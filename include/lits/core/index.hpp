#pragma once

#include "lits/core/node.hpp"
#include "lits/model/hpt.hpp"
#include "lits/types.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace lits {

// Owner: Bach. Single-threaded.
//
// Ownership: the index owns copies of every key; caller storage is never
// retained. Move-only (a deep copy is never needed). A moved-from index is empty.
//
// Structure (see docs/interfaces.md): empty, one record, a compact leaf (2..16
// records), or a learned node whose slots recurse. When the model cannot
// separate a group, a portable byte-radix subtrie stands in for HOT. Structure
// is chosen by fixed rules, not PMSS (which is not calibrated yet).
//
// Invariant: one global Hpt is trained per tree (at bulkload, or when a leaf
// first needs promoting) and never retrained while learned nodes exist, because
// retraining would change every node's routing.
class LitsIndex {
public:
    LitsIndex() = default;
    LitsIndex(LitsIndex&& other) noexcept
        : root_(std::move(other.root_)),
          hpt_(std::move(other.hpt_)),
          size_(std::exchange(other.size_, 0)) {}
    LitsIndex& operator=(LitsIndex&& other) noexcept {
        if (this != &other) {
            root_ = std::move(other.root_);
            hpt_ = std::move(other.hpt_);
            size_ = std::exchange(other.size_, 0);
        }
        return *this;
    }
    LitsIndex(const LitsIndex&) = delete;
    LitsIndex& operator=(const LitsIndex&) = delete;

    // Replaces the entire contents. Input must be sorted, unique, supported keys
    // (throws std::invalid_argument and leaves the index unchanged otherwise).
    // Empty input is valid and clears the index.
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
    // Number of slots of `kind` in the whole tree (walks the tree; for tests and
    // diagnostics, not for hot paths).
    std::size_t node_count(NodeKind kind) const;

private:
    bool insert_at(Slot& slot, std::string_view key, Value value);
    void grow_if_doubled(Slot& slot);
    void ensure_hpt(const std::vector<Record>& sorted_records);

    Slot root_;
    std::unique_ptr<Hpt> hpt_; // null until a learned node is first needed
    std::size_t size_ = 0;
};

} // namespace lits
