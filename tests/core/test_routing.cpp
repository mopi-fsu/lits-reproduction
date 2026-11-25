// Tests for the core-side routing helpers (prefix confirmation, slot bounding).
#include "lits/core/routing.hpp"

#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

static int failures = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            ++failures;                                                          \
            std::cerr << __FILE__ << ":" << __LINE__ << " CHECK failed: " #cond  \
                      << "\n";                                                   \
        }                                                                        \
    } while (0)

using lits::PrefixRelation;
using lits::bound_slot;
using lits::relate_to_prefix;

static void test_prefix_relation() {
    CHECK(relate_to_prefix("", "anything") == PrefixRelation::within);
    CHECK(relate_to_prefix("ab", "ab") == PrefixRelation::within);   // equal
    CHECK(relate_to_prefix("ab", "abc") == PrefixRelation::within);  // extension
    CHECK(relate_to_prefix("ab", "aa") == PrefixRelation::below);    // same length
    CHECK(relate_to_prefix("ab", "ac") == PrefixRelation::above);
    CHECK(relate_to_prefix("ab", "a") == PrefixRelation::below);     // proper prefix of prefix
    CHECK(relate_to_prefix("ab", "b") == PrefixRelation::above);
    CHECK(relate_to_prefix("ab", "aabzz") == PrefixRelation::below); // longer but diverges early
    CHECK(relate_to_prefix("ab", "acz") == PrefixRelation::above);
    CHECK(relate_to_prefix("ab", "") == PrefixRelation::below);      // empty key sorts first
}

static void test_bound_slot() {
    CHECK(bound_slot(0, 4) == 0);
    CHECK(bound_slot(3, 4) == 3);
    CHECK(bound_slot(4, 4) == 3); // one past the end clamps
    CHECK(bound_slot(std::numeric_limits<std::size_t>::max(), 4) == 3);
    CHECK(bound_slot(0, 1) == 0);
    CHECK(bound_slot(7, 1) == 0);
    bool threw = false;
    try {
        bound_slot(0, 0);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);
}

int main() {
    test_prefix_relation();
    test_bound_slot();
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "routing tests passed\n";
    return 0;
}
