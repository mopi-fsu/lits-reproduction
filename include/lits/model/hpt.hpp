#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

namespace lits {

// Owner: Opi. This class is a contract; HPT storage and algorithms are TODO.
class Hpt {
public:
    // Input views are borrowed only during this call; keys must be sorted,
    // unique, nonempty, and contain only bytes 1..127 for the initial version.
    void train(const std::vector<std::string_view>& sorted_keys);

    // Skip a confirmed common prefix. The intended result is in [0, 1].
    double cdf(std::string_view key, std::size_t prefix_length = 0) const;
};

struct LocalLinearModel {
    double slope = 1.0;
    double intercept = 0.0;

    // Endpoint normalization; define the degenerate-CDF fallback before use.
    static LocalLinearModel fit(double min_cdf, double max_cdf);
    // Return a slot in [0, slot_count). Boundary slots belong to the core.
    std::size_t predict(double cdf, std::size_t slot_count) const;
};

} // namespace lits
