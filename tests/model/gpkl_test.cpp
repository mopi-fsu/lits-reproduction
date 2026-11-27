#include "lits/model/gpkl.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void expect_close(double actual, double expected, const char* description) {
    if (!std::isfinite(actual) || std::abs(actual - expected) > 1e-12) {
        throw std::runtime_error(std::string(description) + ": expected " +
                                 std::to_string(expected) + ", got " +
                                 std::to_string(actual));
    }
}

template <class Function>
void expect_invalid(Function function, const char* description) {
    try {
        function();
    } catch (const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error(std::string(description) + ": expected invalid_argument");
}

void prefix_cases() {
    expect_close(lits::common_prefix_length("car", "cat"), 2, "shared prefix");
    expect_close(lits::common_prefix_length("car", "dog"), 0, "different starts");
    expect_close(lits::common_prefix_length("", "car"), 0, "empty left");
    expect_close(lits::common_prefix_length("car", ""), 0, "empty right");
    expect_close(lits::common_prefix_length("", ""), 0, "both empty");
    expect_close(lits::common_prefix_length("car", "car"), 3, "identical keys");
    expect_close(lits::common_prefix_length("a", "abc"), 1, "one key prefixes another");

    const char left[] = {'c', 'a', 'r'};
    const char right[] = {'c', 'a', 't'};
    expect_close(lits::common_prefix_length({left, 3}, {right, 3}), 2,
                 "views without terminators");
    const std::string with_nul("a\0b", 3);
    expect_close(lits::common_prefix_length(with_nul, with_nul), 3,
                 "prefix utility counts embedded zero bytes");
}

void gpkl_cases() {
    expect_close(lits::gpkl({}), 0, "empty group");
    expect_close(lits::gpkl({"car"}), 0, "singleton group");
    expect_close(lits::gpkl({"car", "cat"}), 1, "shared group prefix removed");
    expect_close(lits::gpkl({"car", "cat", "dog"}), 7.0 / 3.0,
                 "fractional average and one-neighbor endpoints");
    expect_close(lits::gpkl({"car", "cat", "dog", "dot"}), 3,
                 "two neighboring clusters");
    expect_close(lits::gpkl({"a", "ab", "abc"}), 5.0 / 3.0,
                 "conceptual terminator for prefix-related keys");
    expect_close(lits::gpkl({"https://car", "https://cat", "https://dog"}),
                 lits::gpkl({"car", "cat", "dog"}), "common-prefix invariance");
    const std::string prefix(1024, 'x');
    const std::vector<std::string> long_keys = {prefix + "car", prefix + "cat",
                                                prefix + "dog"};
    expect_close(lits::gpkl({long_keys[0], long_keys[1], long_keys[2]}),
                 7.0 / 3.0, "long common prefix");
}

void local_cases() {
    const std::vector<std::string_view> keys = {"car", "cat", "dog", "dot"};
    expect_close(lits::local_gpkl(keys, 2), 1, "independent local prefixes");
    expect_close(lits::local_gpkl(keys, 1), 0, "all singleton groups");
    expect_close(lits::local_gpkl(keys, 4), lits::gpkl(keys), "one full group");
    expect_close(lits::local_gpkl(keys, 10), lits::gpkl(keys), "one short group");
    expect_close(lits::local_gpkl(keys, std::numeric_limits<std::size_t>::max()),
                 lits::gpkl(keys), "huge group size does not overflow");
    expect_close(lits::local_gpkl({"car", "cat", "dog"}, 2), 0.5,
                 "final singleton contributes zero");
    // First group: (3+3+1)/3 = 7/3. Final pair (dot,fox): (1+1)/2 = 1.
    expect_close(lits::local_gpkl({"car", "cat", "dog", "dot", "fox"}, 3),
                 5.0 / 3.0, "unweighted average includes final short pair");
    expect_close(lits::local_gpkl({}), 0, "empty local input");
    expect_close(lits::local_gpkl({"car"}), 0, "singleton local input");

    // Default size 32: a 32-key group followed by a singleton, rather than one
    // whole-input group. 'aA' through 'a`' differ after their shared 'a'.
    std::vector<std::string> owned;
    for (int i = 0; i < 32; ++i) {
        owned.push_back(std::string("a") + static_cast<char>('A' + i));
    }
    owned.push_back("z");
    std::vector<std::string_view> views;
    for (const auto& key : owned) {
        views.push_back(key);
    }
    expect_close(lits::local_gpkl(views), 0.5, "default group size is 32");
}

void invalid_cases() {
    const std::vector<std::vector<std::string_view>> invalid = {
        {"cat", "car"}, {"car", "car"}, {""},
        {std::string_view("a\0b", 3)}, {std::string_view("\x80", 1)}};
    for (const auto& keys : invalid) {
        expect_invalid([&] { lits::gpkl(keys); }, "invalid global keys");
        expect_invalid([&] { lits::local_gpkl(keys, 1); }, "invalid local keys");
    }
    // Invalid order/duplicates must be caught across group boundaries too.
    expect_invalid([] { lits::local_gpkl({"a", "b", "a", "c"}, 2); },
                   "unsorted group boundary");
    expect_invalid([] { lits::local_gpkl({"a", "b", "b", "c"}, 2); },
                   "duplicate group boundary");
    expect_invalid([] { lits::local_gpkl({"car"}, 0); }, "zero group size");
    expect_invalid([] { lits::local_gpkl({}, 0); }, "zero group size on empty input");
}

} // namespace

int main() {
    try {
        prefix_cases();
        gpkl_cases();
        local_cases();
        invalid_cases();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "GPKL calculation, grouping, and validation checks passed.\n";
    return 0;
}
