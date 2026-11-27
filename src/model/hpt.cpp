#include "lits/model/hpt.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace lits {

Hpt::Hpt() : table_(hash_size) {}

std::uint64_t Hpt::append_character(std::uint64_t hash,
                                    unsigned char character) noexcept {
    return hash * hash_base + character;
}

const Hpt::Cell& Hpt::cell(std::size_t row, std::size_t character) const {
    return table_.at(row).at(character);
}

void Hpt::train(const std::vector<std::string_view>& sorted_keys) {
    if (sorted_keys.empty()) {
        throw std::invalid_argument("HPT training sample must be nonempty");
    }
    for (std::size_t i = 0; i < sorted_keys.size(); ++i) {
        if (sorted_keys[i].empty()) {
            throw std::invalid_argument("HPT keys must be nonempty");
        }
        for (unsigned char character : sorted_keys[i]) {
            if (character == 0 || character >= alphabet_size) {
                throw std::invalid_argument("HPT keys must contain only bytes 1..127");
            }
        }
        if (i > 0 && sorted_keys[i - 1] >= sorted_keys[i]) {
            throw std::invalid_argument("HPT keys must be sorted and unique");
        }
    }

    // Counts are temporary, so the retained cells still contain only cdf/prob.
    using CountRow = std::array<std::uint64_t, alphabet_size>;
    std::vector<CountRow> counts(hash_size);
    for (std::string_view key : sorted_keys) {
        std::uint64_t hash = 0; // Empty prefix always maps to row zero.
        for (unsigned char character : key) {
            // The row represents the prefix BEFORE the current character.
            ++counts[hash % hash_size][character];
            hash = append_character(hash, character);
        }
    }

    std::vector<Row> next_table(hash_size);
    for (std::size_t row = 0; row < hash_size; ++row) {
        std::uint64_t total = 0;
        for (std::uint64_t count : counts[row]) {
            total += count;
        }
        if (total == 0) {
            continue; // Leave unobserved rows at zero; never divide by zero.
        }

        std::uint64_t preceding = 0;
        for (std::size_t character = 0; character < alphabet_size; ++character) {
            Cell& entry = next_table[row][character];
            entry.cdf = static_cast<double>(preceding) / static_cast<double>(total);
            entry.prob = static_cast<double>(counts[row][character]) /
                         static_cast<double>(total);
            preceding += counts[row][character];
        }
    }
    table_.swap(next_table);
    trained_ = true;
}

double Hpt::cdf(std::string_view str, std::size_t prefix_length) const {
    if (!trained_ || table_.size() != hash_size) {
        throw std::logic_error("HPT must be trained before prediction");
    }
    if (str.empty()) {
        throw std::invalid_argument("HPT prediction key must be nonempty");
    }
    if (prefix_length > str.size()) {
        throw std::invalid_argument("HPT prefix length exceeds key length");
    }

    double cdf = 0, prob = 1;
    std::uint64_t hashval = 0;
    for (std::size_t i = 0; i < str.size(); ++i) {
        const auto c = static_cast<unsigned char>(str[i]);
        if (c == 0 || c >= alphabet_size) {
            throw std::invalid_argument("HPT keys must contain only bytes 1..127");
        }
        if (i >= prefix_length) {
            const Cell& entry = table_[hashval % hash_size][c];
            cdf += prob * entry.cdf;
            prob *= entry.prob;
        }
        // Even skipped characters form part of the original prefix context.
        // Keep validating the key after prob becomes zero as well.
        hashval = append_character(hashval, c);
    }
    // Protect callers from small floating-point excursions outside [0, 1].
    return std::clamp(cdf, 0.0, 1.0);
}

LocalLinearModel LocalLinearModel::fit(double min_cdf, double max_cdf) {
    if (!std::isfinite(min_cdf) || !std::isfinite(max_cdf) ||
        min_cdf < 0.0 || max_cdf > 1.0 || min_cdf > max_cdf) {
        throw std::invalid_argument("Local model needs ordered, finite CDF bounds in [0, 1]");
    }
    if (min_cdf == max_cdf) {
        throw std::domain_error("Equal CDF endpoints require a fallback structure");
    }

    const double slope = 1.0 / (max_cdf - min_cdf);
    const double intercept = -min_cdf * slope;
    if (!std::isfinite(slope) || !std::isfinite(intercept)) {
        throw std::domain_error("CDF interval is too narrow for finite model parameters");
    }
    return {slope, intercept};
}

std::size_t LocalLinearModel::predict(double cdf, std::size_t slots) const {
    if (slots == 0) {
        throw std::invalid_argument("Local model needs at least one slot");
    }
    if (!std::isfinite(cdf) || !std::isfinite(slope) || slope <= 0.0 ||
        !std::isfinite(intercept)) {
        throw std::invalid_argument("Local model needs finite input and a positive finite slope");
    }

    const double normalized = cdf * slope + intercept;
    if (!std::isfinite(normalized)) {
        throw std::domain_error("Local model prediction is not finite");
    }
    if (normalized <= 0.0) return 0;
    if (normalized >= 1.0) return slots - 1;

    const double position = normalized * static_cast<double>(slots);
    // Rounding can reach the upper bound, especially when size_t is wider than
    // a double's exact integer range. Check BEFORE converting to size_t.
    if (position >= static_cast<double>(slots)) return slots - 1;
    // Conversion of a nonnegative value truncates, equivalent to floor().
    return std::min(static_cast<std::size_t>(position), slots - 1);
}

} // namespace lits
