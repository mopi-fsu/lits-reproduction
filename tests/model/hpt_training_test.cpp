#include "lits/model/hpt.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void expect_close(double actual, double expected, const char* description) {
    if (!std::isfinite(actual) || std::abs(actual - expected) > 1e-12) {
        throw std::runtime_error(description);
    }
}

template <class Exception, class Function>
void expect_throw(Function function) {
    try {
        function();
    } catch (const Exception&) {
        return;
    }
    throw std::runtime_error("Expected exception was not thrown");
}

void check_rows(const lits::Hpt& model) {
    for (std::size_t row = 0; row < lits::Hpt::hash_size; ++row) {
        double sum = 0;
        for (std::size_t character = 0; character < lits::Hpt::alphabet_size;
             ++character) {
            const auto& entry = model.cell(row, character);
            expect_close(entry.cdf, sum, "CDF must exclude the current character");
            if (entry.prob < 0 || entry.prob > 1) {
                throw std::runtime_error("Probability out of bounds");
            }
            sum += entry.prob;
        }
        if (sum != 0) {
            expect_close(sum, 1, "Observed row probabilities must sum to one");
        }
    }
}

void training_cases() {
    lits::Hpt model;
    check_rows(model);
    model.train({"car", "cat", "dog"});

    // Empty prefix: two 'c' observations and one 'd' observation.
    expect_close(model.cell(0, 'c').prob, 2.0 / 3.0, "Root counts must include every key");
    expect_close(model.cell(0, 'c').cdf, 0, "Nothing precedes root 'c'");
    expect_close(model.cell(0, 'd').prob, 1.0 / 3.0, "Root 'd' probability");
    expect_close(model.cell(0, 'd').cdf, 2.0 / 3.0, "Root 'd' CDF");
    expect_close(model.cell(0, 'e').cdf, 1, "An absent column can still have a CDF");

    // hash("c") = 99; hash("ca") % 1024 = 778 for base 131.
    expect_close(model.cell(99, 'a').prob, 1, "Row must represent preceding prefix");
    expect_close(model.cell(778, 'r').prob, 0.5, "Repeated prefix counts 'r'");
    expect_close(model.cell(778, 't').prob, 0.5, "Repeated prefix counts 't'");
    expect_close(model.cell(778, 'r').cdf, 0, "CDF excludes 'r' itself");
    expect_close(model.cell(778, 't').cdf, 0.5, "CDF includes preceding 'r'");
    expect_close(model.cell(1023, 'z').prob, 0, "Unseen row probability stays zero");
    expect_close(model.cell(1023, 'z').cdf, 0, "Unseen row CDF stays zero");
    check_rows(model);

    // Invalid training must preserve a previously trained table.
    const std::vector<std::vector<std::string_view>> invalid = {
        {}, {""}, {"cat", "car"}, {"cat", "cat"},
        {std::string_view("a\0b", 3)}, {std::string_view("\x80", 1)}};
    for (const auto& sample : invalid) {
        expect_throw<std::invalid_argument>([&] { model.train(sample); });
        expect_close(model.cell(0, 'c').prob, 2.0 / 3.0, "Invalid input changed model");
    }
    expect_throw<std::out_of_range>([&] { model.cell(1024, 'a'); });
    expect_throw<std::out_of_range>([&] { model.cell(0, 128); });

    model.train({"z"});
    expect_close(model.cell(0, 'z').prob, 1, "Retraining replaces old statistics");
    expect_close(model.cell(0, 'c').prob, 0, "Retraining clears previous root counts");
    expect_close(model.cell(99, 'a').prob, 0, "Retraining clears previous prefix rows");
    check_rows(model);
}

void collision_case() {
    lits::Hpt model;
    // Prefixes "az" and "ib" both map to row 541; their next characters pool.
    model.train({"azx", "iby"});
    expect_close(model.cell(541, 'x').prob, 0.5, "Hash collision must pool counts");
    expect_close(model.cell(541, 'y').prob, 0.5, "Hash collision must retain both contexts");
    expect_close(model.cell(541, 'y').cdf, 0.5, "Collision row CDF");
    check_rows(model);
}

void long_key_case() {
    // A 10,000-byte view exercises repeated prefixes and unsigned hash overflow.
    // Every observed next character is 'a', so each populated row has prob(a)=1.
    lits::Hpt model;
    model.train({std::string(10000, 'a')});
    for (std::size_t row = 0; row < lits::Hpt::hash_size; ++row) {
        const double probability = model.cell(row, 'a').prob;
        if (probability != 0 && probability != 1) {
            throw std::runtime_error("Long-key counts normalized incorrectly");
        }
    }
    expect_close(model.cell(0, 'a').prob, 1, "Long key has a first character");
    check_rows(model);
}

} // namespace

int main() {
    try {
        training_cases();
        collision_case();
        long_key_case();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "HPT counting, normalization, collisions, and retraining checks passed.\n";
    return 0;
}
