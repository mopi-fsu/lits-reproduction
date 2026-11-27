#include "lits/model/hpt.hpp"
#include "lits/detail/unimplemented.hpp"

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
}

double Hpt::cdf(std::string_view, std::size_t) const {
    detail::unimplemented("Hpt::cdf");
}

LocalLinearModel LocalLinearModel::fit(double, double) {
    detail::unimplemented("LocalLinearModel::fit");
}

std::size_t LocalLinearModel::predict(double, std::size_t) const {
    detail::unimplemented("LocalLinearModel::predict");
}

} // namespace lits
