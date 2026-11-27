#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace lits {

// Owner: Opi. Fixed-size prefix statistics; string CDF prediction is still TODO.
class Hpt {
public:
    static constexpr std::size_t hash_size = 1024;
    static constexpr std::size_t alphabet_size = 128;
    static constexpr std::uint64_t hash_base = 131;

    struct Cell {
        double cdf = 0.0;  // Probability of a character smaller than this column.
        double prob = 0.0; // Probability of this column's character.
    };

    Hpt();

    // The caller supplies an already selected sample. It must be nonempty,
    // sorted and unique, with nonempty keys containing only bytes 1..127.
    // Invalid input throws std::invalid_argument without changing the table.
    // Each valid call replaces previous training; input views are not retained.
    void train(const std::vector<std::string_view>& sorted_keys);

    // Read-only inspection of the trained table. Invalid indices throw
    // std::out_of_range. Unobserved rows (and the initial table) contain zeros.
    const Cell& cell(std::size_t row, std::size_t character) const;

    // Skip a confirmed common prefix. The intended result is in [0, 1].
    double cdf(std::string_view key, std::size_t prefix_length = 0) const;

private:
    using Row = std::array<Cell, alphabet_size>;

    // Heap storage: 1024 rows * 128 cells * two doubles = 2 MiB on our platform.
    std::vector<Row> table_;

    // Unsigned overflow intentionally implements arithmetic modulo 2^64.
    static std::uint64_t append_character(std::uint64_t hash,
                                          unsigned char character) noexcept;
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
