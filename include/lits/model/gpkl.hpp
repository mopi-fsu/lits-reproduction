#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

namespace lits {

// Owner: Opi. Count bytes, not Unicode code points. Accepts arbitrary byte views,
// including empty views; does not require a null terminator.
std::size_t common_prefix_length(std::string_view left, std::string_view right);

// Sorted unique, nonempty keys containing only bytes 1..127. Invalid keys or
// ordering throw std::invalid_argument. Empty/singleton groups return 0.
// A conceptual terminator distinguishes a key that prefixes another key.
double gpkl(const std::vector<std::string_view>& sorted_keys);

// Unweighted average of consecutive group GPKLs, including the final short group.
// Empty input returns 0; a singleton group contributes 0. Same key requirements
// as gpkl(). group_size == 0 throws std::invalid_argument, even for empty input.
double local_gpkl(const std::vector<std::string_view>& sorted_keys,
                  std::size_t group_size = 32);

} // namespace lits
