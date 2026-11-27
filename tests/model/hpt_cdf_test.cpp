#include "lits/model/hpt.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

void expect_close(double actual, double expected, const char* description) {
    if (!std::isfinite(actual) || std::abs(actual - expected) > 1e-12) {
        throw std::runtime_error(std::string(description) + ": expected " +
                                 std::to_string(expected) + ", got " +
                                 std::to_string(actual));
    }
}

template <class Exception, class Function>
void expect_throw(Function function, const char* description) {
    try {
        function();
    } catch (const Exception&) {
        return;
    }
    throw std::runtime_error(description);
}

void prediction_cases() {
    lits::Hpt model;
    expect_throw<std::logic_error>([&] { model.cdf("cat"); }, "Untrained prediction");
    expect_throw<std::invalid_argument>([&] { model.train({}); }, "Invalid first training");
    expect_throw<std::logic_error>([&] { model.cdf("cat"); }, "Failed training is not training");
    model.train({"car", "cat", "dog"});

    // Root c=2/3, d=1/3; after "ca", r=t=1/2.
    expect_close(model.cdf("car"), 0, "First training key");
    expect_close(model.cdf("cat"), 1.0 / 3.0, "Nested CDF weighted by prefix probability");
    expect_close(model.cdf("dog"), 2.0 / 3.0, "Root contributes preceding-key mass");
    expect_close(model.cdf("cas"), 1.0 / 3.0, "Absent character between r and t");
    expect_close(model.cdf("cax"), 2.0 / 3.0, "Absent character beyond r and t");
    expect_close(model.cdf("caszz"), model.cdf("cas"), "Zero probability freezes CDF");
    expect_close(model.cdf("aaa"), 0, "Query before all training keys");
    expect_close(model.cdf("zzz"), 1, "Query after all training keys");

    const char nonterminated[] = {'c', 'a', 't'};
    expect_close(model.cdf({nonterminated, 3}), 1.0 / 3.0, "Nonterminated view");
    const auto before = model.cell(0, 'c');
    model.cdf("cat");
    expect_close(model.cell(0, 'c').prob, before.prob, "Prediction preserves table");

    model.train({"a", "b"});
    expect_close(model.cdf("b"), 0.5, "Prediction uses retrained table");
    expect_throw<std::invalid_argument>([&] { model.train({"b", "a"}); },
                                        "Invalid retraining");
    expect_close(model.cdf("b"), 0.5, "Invalid retraining preserves prediction");
}

void prefix_cases() {
    lits::Hpt model;
    model.train({"car", "cat", "dog"});
    expect_close(model.cdf("cat", 0), 1.0 / 3.0, "Zero skipped bytes");
    expect_close(model.cdf("cat", 1), 0.5, "Retain hash of skipped c");
    expect_close(model.cdf("cat", 2), 0.5, "Retain hash of skipped ca");
    expect_close(model.cdf("dog", 2), 0, "Exclude mass before skipped prefix");
    expect_close(model.cdf("cat", 3), 0, "No suffix after full key skipped");
    expect_close(model.cdf("zzx", 2), 0, "Unseen context stays zero");

    model.train({"a", "ab", "abc", "b"});
    // Without terminator counts, prefix-related keys may tie. Core collision
    // handling must not rely on strictly increasing model coordinates.
    expect_close(model.cdf("a"), 0, "Short key");
    expect_close(model.cdf("ab"), 0, "Prefix-related key may tie");
    expect_close(model.cdf("abc"), 0, "Longer prefix-related key may tie");
    expect_close(model.cdf("b"), 0.75, "Later first character remains ordered");
}

void collision_and_order_cases() {
    lits::Hpt model;
    // "az" and "ib" collide at row 541, with x=y=1/2.
    model.train({"azx", "iby"});
    expect_close(model.cdf("azy"), 0.25, "Collision row weighted by root probability");
    expect_close(model.cdf("iby"), 0.75, "Both root and collision row contribute");
    expect_close(model.cdf("iby", 2), 0.5, "Collision row with skipped prefix");

    const std::vector<std::string_view> queries = {
        "a", "az", "azw", "azx", "azy", "azz", "i", "ib", "ibx", "iby", "ibz", "z"};
    double previous = 0;
    for (const auto key : queries) {
        const double value = model.cdf(key);
        if (!std::isfinite(value) || value < previous || value < 0 || value > 1) {
            throw std::runtime_error("Sorted queries must have finite nondecreasing CDFs");
        }
        previous = value;
    }

    // Long prefixes exercise unsigned hash overflow. Collisions between the two
    // repeated-character paths also make the prefix probability underflow.
    const std::string long_key(5000, 'a');
    const std::string other_key(5000, 'b');
    model.train({long_key, other_key});
    const double result = model.cdf(long_key + "z");
    if (!std::isfinite(result) || result < 0 || result > 1) {
        throw std::runtime_error("Long-key prediction must remain bounded");
    }

    model.train({long_key});
    expect_close(model.cdf(long_key), 0, "Long observed key");
    expect_close(model.cdf(long_key + "b", 4000), 1, "Long skipped hash context");
}

void invalid_and_move_cases() {
    lits::Hpt model;
    model.train({"car", "cat", "dog"});
    expect_throw<std::invalid_argument>([&] { model.cdf(""); }, "Empty key");
    expect_throw<std::invalid_argument>([&] { model.cdf("cat", 4); }, "Prefix beyond key");
    expect_throw<std::invalid_argument>([&] {
        model.cdf("cat", std::numeric_limits<std::size_t>::max());
    }, "Huge prefix length");
    const std::string embedded_nul("c\0t", 3);
    const std::string high_byte("\x80", 1);
    expect_throw<std::invalid_argument>([&] { model.cdf(embedded_nul); }, "NUL key");
    expect_throw<std::invalid_argument>([&] { model.cdf(high_byte); }, "Non-ASCII key");
    expect_throw<std::invalid_argument>([&] { model.cdf(embedded_nul, 3); },
                                        "Skipped prefix is still validated");
    const std::string invalid_tail = std::string("zzz") + high_byte;
    expect_throw<std::invalid_argument>([&] { model.cdf(invalid_tail); },
                                        "Validate even after zero prefix probability");

    lits::Hpt moved = std::move(model);
    expect_close(moved.cdf("cat"), 1.0 / 3.0, "Moved model retains training");
    expect_throw<std::logic_error>([&] { model.cdf("cat"); }, "Moved-from model");
    model.train({"a", "b"});
    expect_close(model.cdf("b"), 0.5, "Moved-from model can be retrained");
}

} // namespace

int main() {
    try {
        prediction_cases();
        prefix_cases();
        collision_and_order_cases();
        invalid_and_move_cases();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "HPT Algorithm 1 prediction and prefix-context checks passed.\n";
    return 0;
}
