#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace lits {

using Value = std::uint64_t;

struct Record {
    std::string key;
    Value value;
};

inline constexpr std::size_t compact_leaf_capacity = 16;

} // namespace lits
