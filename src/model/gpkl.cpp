#include "lits/model/gpkl.hpp"

#include <algorithm>
#include <stdexcept>

namespace lits {

std::size_t common_prefix_length(std::string_view string_a, std::string_view string_b) {
    const std::size_t limit = std::min(string_a.size(), string_b.size());
    std::size_t length = 0;
    while (length < limit && string_a[length] == string_b[length]) {
        ++length;
    }
    return length;
}

namespace {

void validate_sorted_keys(const std::vector<std::string_view>& string_list) {
    for (std::size_t i = 0; i < string_list.size(); ++i) {
        if (string_list[i].empty()) {
            throw std::invalid_argument("GPKL keys must be nonempty");
        }
        for (unsigned char byte : string_list[i]) {
            if (byte == 0 || byte > 127) {
                throw std::invalid_argument("GPKL keys must contain only bytes 1..127");
            }
        }
        if (i > 0 && string_list[i - 1] >= string_list[i]) {
            throw std::invalid_argument("GPKL keys must be sorted and unique");
        }
    }
}

// Work on [begin, end) without copying each local group into another vector.
double gpkl_range(const std::vector<std::string_view>& string_list,
                  std::size_t begin, std::size_t end) {
    const std::size_t count = end - begin;
    if (count < 2) {
        return 0.0; // No neighbors to distinguish.
    }

    // For sorted keys, the first and last keys determine the group's prefix.
    const std::size_t common_whole_group =
        common_prefix_length(string_list[begin], string_list[end - 1]);
    double partial_length_sum = 0.0;
    for (std::size_t i = begin; i < end; ++i) {
        std::size_t length = 0;
        if (i > begin) {
            length = common_prefix_length(string_list[i], string_list[i - 1]);
        }
        if (i + 1 < end) {
            length = std::max(length,
                             common_prefix_length(string_list[i], string_list[i + 1]));
        }
        // The +1 counts a conceptual terminator if a key prefixes its neighbor.
        // No terminating byte is read through the string_view.
        partial_length_sum += static_cast<double>(length - common_whole_group) + 1.0;
    }
    return partial_length_sum / static_cast<double>(count);
}

} // namespace

double gpkl(const std::vector<std::string_view>& string_list) {
    validate_sorted_keys(string_list);
    return gpkl_range(string_list, 0, string_list.size());
}

double local_gpkl(const std::vector<std::string_view>& list, std::size_t g_size) {
    if (g_size == 0) {
        throw std::invalid_argument("GPKL group size must be positive");
    }
    validate_sorted_keys(list);
    if (list.empty()) {
        return 0.0;
    }

    std::size_t groups = 0;
    double sum = 0;
    for (std::size_t begin = 0; begin < list.size();) {
        const std::size_t count = std::min(g_size, list.size() - begin);
        const std::size_t end = begin + count;
        sum += gpkl_range(list, begin, end);
        ++groups;
        begin = end;
    }
    return sum / static_cast<double>(groups);
}

} // namespace lits
