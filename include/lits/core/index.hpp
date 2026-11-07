#pragma once

#include "lits/types.hpp"

#include <optional>
#include <string_view>
#include <vector>

namespace lits {

// Owner: Bach. Single-threaded first; structure and ownership are TODO.
class LitsIndex {
public:
    // Sorted unique keys; copy keys and values into index-owned storage.
    // Invalid input should throw std::invalid_argument once implemented.
    void bulkload(const std::vector<Record>& sorted_records);
    std::optional<Value> lookup(std::string_view key) const;
    bool insert(std::string_view key, Value value); // false for an existing key
    bool update(std::string_view key, Value value); // false for a missing key
    bool erase(std::string_view key);               // false for a missing key
    // Inclusive lower bound, ascending order, at most limit records.
    std::vector<Record> scan(std::string_view lower_bound, std::size_t limit) const;
};

} // namespace lits
