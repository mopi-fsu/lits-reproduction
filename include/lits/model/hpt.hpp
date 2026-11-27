#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace lits {

// Owner: Opi. Fixed-size prefix statistics and Algorithm 1 string CDF prediction.
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

    // Algorithm 1: return a model coordinate in [0, 1]. For a confirmed prefix,
    // hash its bytes but start cdf=0, prob=1 at prefix_length, retaining the full
    // prefix context. Skipping the entire key returns zero. Unseen/zero-probability
    // contexts contribute no further mass. No end-of-key symbol is counted, so
    // different strings can receive the same coordinate.
    // Throws std::logic_error before training (or on a moved-from model).
    // Empty/unsupported keys or prefix_length > key.size() throw invalid_argument.
    double cdf(std::string_view key, std::size_t prefix_length = 0) const;

private:
    using Row = std::array<Cell, alphabet_size>;

    // Heap storage: 1024 rows * 128 cells * two doubles = 2 MiB on our platform.
    std::vector<Row> table_;
    bool trained_ = false;

    // Unsigned overflow intentionally implements arithmetic modulo 2^64.
    static std::uint64_t append_character(std::uint64_t hash,
                                          unsigned char character) noexcept;
};

struct LocalLinearModel {
    double slope = 1.0;
    double intercept = 0.0;

    // Normalize [min_cdf, max_cdf] to [0, 1]. Bounds must be finite, ordered,
    // and within [0,1], or invalid_argument is thrown. Equal bounds or an
    // unrepresentably narrow interval throw domain_error; the core must fallback.
    static LocalLinearModel fit(double min_cdf, double max_cdf);
    // floor((cdf*slope + intercept)*slot_count), clamped to [0, slot_count-1].
    // Finite inputs outside the fitted interval clamp to endpoint slots.
    // Zero slots, non-finite input/coefficients, or a nonpositive slope throw
    // invalid_argument. Non-finite arithmetic throws domain_error. The core
    // separately handles reserved boundary slots and fallback structures.
    std::size_t predict(double cdf, std::size_t slot_count) const;
};

} // namespace lits
