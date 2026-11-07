#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

namespace lits {

// Owner: Opi. Count bytes, not Unicode code points.
std::size_t common_prefix_length(std::string_view left, std::string_view right);

// Sorted unique keys; define empty/singleton behavior in the first implementation.
double gpkl(const std::vector<std::string_view>& sorted_keys);

// Average the GPKLs of consecutive groups. Specify handling of the final group.
double local_gpkl(const std::vector<std::string_view>& sorted_keys,
                  std::size_t group_size = 32);

} // namespace lits
