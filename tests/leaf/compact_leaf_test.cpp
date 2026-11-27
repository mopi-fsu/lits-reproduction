// Standalone correctness tests for CompactLeaf. Uses no HPT or core-tree code.
// Checks are always active, including in Release builds.

#include "lits/leaf/compact_leaf.hpp"

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using lits::CompactLeaf;
using lits::LeafInsert;
using lits::Record;
using lits::Value;
using lits::compact_leaf_capacity;

int failures = 0;

void report_failure(const char* file, int line, const char* expression) {
    ++failures;
    std::cerr << file << ':' << line << ": check failed: " << expression << '\n';
}

#define CHECK(condition)                                     \
    do {                                                     \
        if (!(condition)) {                                  \
            report_failure(__FILE__, __LINE__, #condition);  \
        }                                                    \
    } while (false)

#define CHECK_INVALID(expression)                                                  \
    do {                                                                           \
        bool rejected = false;                                                     \
        try {                                                                      \
            static_cast<void>(expression);                                         \
        } catch (const std::invalid_argument&) {                                   \
            rejected = true;                                                       \
        }                                                                          \
        if (!rejected) {                                                           \
            report_failure(__FILE__, __LINE__, "invalid_argument from " #expression); \
        }                                                                          \
    } while (false)

using Expected = std::vector<std::pair<std::string, Value>>;

bool matches(const std::vector<Record>& actual, const Expected& expected) {
    if (actual.size() != expected.size()) {
        return false;
    }
    for (std::size_t i = 0; i < actual.size(); ++i) {
        if (actual[i].key != expected[i].first || actual[i].value != expected[i].second) {
            return false;
        }
    }
    return true;
}

// Distinct supported keys in ascending order ("key000", "key001", ...). Zero
// padding keeps byte order equal to numeric order for any capacity below 1000,
// so these tests also work if the leaf limit changes (e.g. the 8/16/32 ablation).
std::string numbered_key(std::size_t i) {
    std::string digits = std::to_string(i);
    return "key" + std::string(3 - std::min<std::size_t>(3, digits.size()), '0') + digits;
}

CompactLeaf full_leaf() {
    CompactLeaf leaf;
    for (std::size_t i = 0; i < compact_leaf_capacity; ++i) {
        CHECK(leaf.insert(numbered_key(i), i) == LeafInsert::inserted);
    }
    return leaf;
}

void test_empty_leaf() {
    const CompactLeaf leaf;
    CHECK(!leaf.lookup("missing").has_value());
    CHECK(leaf.scan("a", 10).empty());
    CHECK(leaf.scan("", 10).empty());
}

void test_zero_is_a_stored_value() {
    CompactLeaf leaf;
    CHECK(leaf.insert("zero", 0) == LeafInsert::inserted);
    CHECK(leaf.lookup("zero").has_value());
    CHECK(leaf.lookup("zero") == std::optional<Value>(0));
    CHECK(!leaf.lookup("other").has_value());
}

void test_maximum_value_round_trips() {
    const Value maximum = std::numeric_limits<Value>::max();
    CompactLeaf leaf;
    CHECK(leaf.insert("max", maximum) == LeafInsert::inserted);
    CHECK(leaf.lookup("max") == std::optional<Value>(maximum));
}

void test_duplicate_keeps_original_value() {
    CompactLeaf leaf;
    CHECK(leaf.insert("key", 1) == LeafInsert::inserted);
    CHECK(leaf.insert("key", 2) == LeafInsert::duplicate);
    CHECK(leaf.lookup("key") == std::optional<Value>(1));
    CHECK(matches(leaf.scan("", 10), {{"key", 1}}));
}

void test_full_leaf_rejects_without_change() {
    CompactLeaf leaf = full_leaf();
    Expected expected;
    for (std::size_t i = 0; i < compact_leaf_capacity; ++i) {
        expected.emplace_back(numbered_key(i), i);
    }

    CHECK(leaf.insert("zzz", 99) == LeafInsert::full);             // Would sort last.
    CHECK(leaf.insert("aaa", 99) == LeafInsert::full);             // Would sort first.
    CHECK(leaf.insert(numbered_key(3), 99) == LeafInsert::duplicate); // Already stored.

    CHECK(!leaf.lookup("zzz").has_value());
    CHECK(!leaf.lookup("aaa").has_value());
    CHECK(leaf.lookup(numbered_key(3)) == std::optional<Value>(3));
    CHECK(matches(leaf.scan("", compact_leaf_capacity + 1), expected));
}

// The core promotes a leaf only on `full`, so a duplicate must never look full,
// even when the leaf is full and the key would sort first or last.
void test_insert_reports_duplicate_before_full() {
    CompactLeaf leaf = full_leaf();
    const std::string first = numbered_key(0);
    const std::string last = numbered_key(compact_leaf_capacity - 1);
    CHECK(leaf.insert(first, 7) == LeafInsert::duplicate);
    CHECK(leaf.insert(last, 7) == LeafInsert::duplicate);
    CHECK(leaf.lookup(first) == std::optional<Value>(0));
    CHECK(leaf.insert("overflow", 7) == LeafInsert::full);
    CHECK(!leaf.lookup("overflow").has_value());

    CompactLeaf one;
    CHECK(one.insert("only", 1) == LeafInsert::inserted);
    CHECK(one.insert("only", 2) == LeafInsert::duplicate);
    CHECK(one.insert("other", 3) == LeafInsert::inserted);
}

void test_any_insertion_order_scans_sorted() {
    CompactLeaf leaf;
    for (std::size_t i = compact_leaf_capacity; i > 0; --i) {
        CHECK(leaf.insert(numbered_key(i - 1), i - 1) == LeafInsert::inserted);
    }
    Expected expected;
    for (std::size_t i = 0; i < compact_leaf_capacity; ++i) {
        expected.emplace_back(numbered_key(i), i);
    }
    CHECK(matches(leaf.scan("", compact_leaf_capacity), expected));
}

void test_prefix_related_keys() {
    CompactLeaf leaf;
    CHECK(leaf.insert("abc", 3) == LeafInsert::inserted);
    CHECK(leaf.insert("a", 1) == LeafInsert::inserted);
    CHECK(leaf.insert("b", 4) == LeafInsert::inserted);
    CHECK(leaf.insert("ab", 2) == LeafInsert::inserted);

    CHECK(leaf.lookup("a") == std::optional<Value>(1));
    CHECK(leaf.lookup("ab") == std::optional<Value>(2));
    CHECK(leaf.lookup("abc") == std::optional<Value>(3));
    CHECK(!leaf.lookup("abcd").has_value());
    CHECK(!leaf.lookup("aa").has_value());

    CHECK(matches(leaf.scan("", 10), {{"a", 1}, {"ab", 2}, {"abc", 3}, {"b", 4}}));
    CHECK(matches(leaf.scan("ab", 10), {{"ab", 2}, {"abc", 3}, {"b", 4}}));
    CHECK(matches(leaf.scan("aa", 10), {{"ab", 2}, {"abc", 3}, {"b", 4}}));
    CHECK(matches(leaf.scan("abd", 10), {{"b", 4}}));
}

void test_scan_bounds_and_limits() {
    CompactLeaf leaf;
    CHECK(leaf.insert("b", 2) == LeafInsert::inserted);
    CHECK(leaf.insert("d", 4) == LeafInsert::inserted);
    CHECK(leaf.insert("f", 6) == LeafInsert::inserted);

    CHECK(matches(leaf.scan("a", 10), {{"b", 2}, {"d", 4}, {"f", 6}})); // Before first.
    CHECK(matches(leaf.scan("d", 10), {{"d", 4}, {"f", 6}}));           // Inclusive.
    CHECK(matches(leaf.scan("c", 10), {{"d", 4}, {"f", 6}}));           // Between keys.
    CHECK(matches(leaf.scan("f", 10), {{"f", 6}}));                     // Last key.
    CHECK(leaf.scan("g", 10).empty());                                  // Past last.

    CHECK(leaf.scan("a", 0).empty());
    CHECK(matches(leaf.scan("a", 1), {{"b", 2}}));
    CHECK(matches(leaf.scan("a", 2), {{"b", 2}, {"d", 4}}));
    CHECK(matches(leaf.scan("a", 3), {{"b", 2}, {"d", 4}, {"f", 6}}));
    CHECK(matches(leaf.scan("d", std::numeric_limits<std::size_t>::max()),
                  {{"d", 4}, {"f", 6}}));
}

void test_supported_byte_boundaries() {
    const std::string lowest(1, '\x01');
    const std::string highest(1, '\x7f');
    CompactLeaf leaf;
    CHECK(leaf.insert(highest, 127) == LeafInsert::inserted);
    CHECK(leaf.insert("A", 65) == LeafInsert::inserted);
    CHECK(leaf.insert(lowest, 1) == LeafInsert::inserted);

    CHECK(leaf.lookup(lowest) == std::optional<Value>(1));
    CHECK(leaf.lookup(highest) == std::optional<Value>(127));
    CHECK(matches(leaf.scan("", 10), {{lowest, 1}, {"A", 65}, {highest, 127}}));
}

void test_unsupported_keys_are_rejected() {
    const std::string_view empty;
    const std::string_view embedded_nul("a\0b", 3);
    const std::string_view leading_nul("\0a", 2);
    const std::string_view high_byte("a\x80", 2);
    const std::string_view top_byte("\xff", 1);

    CompactLeaf leaf;
    CHECK(leaf.insert("kept", 1) == LeafInsert::inserted);

    for (const std::string_view key : {empty, embedded_nul, leading_nul, high_byte, top_byte}) {
        CHECK_INVALID(leaf.insert(key, 2));
        CHECK_INVALID(leaf.lookup(key));
    }
    for (const std::string_view key : {embedded_nul, leading_nul, high_byte, top_byte}) {
        CHECK_INVALID(leaf.scan(key, 10));
        CHECK_INVALID(leaf.scan(key, 0)); // Validation does not depend on the limit.
    }

    CHECK(matches(leaf.scan("", 10), {{"kept", 1}}));
}

void test_unsupported_key_is_rejected_when_full() {
    CompactLeaf leaf = full_leaf();
    CHECK_INVALID(leaf.insert("", 1));
}

void test_leaf_owns_inserted_keys() {
    CompactLeaf leaf;
    {
        auto buffer = std::make_unique<std::string>("borrowed-key");
        CHECK(leaf.insert(std::string_view(*buffer), 5) == LeafInsert::inserted);
        buffer->assign(buffer->size(), 'X');
    }
    CHECK(leaf.lookup("borrowed-key") == std::optional<Value>(5));
    CHECK(matches(leaf.scan("", 1), {{"borrowed-key", 5}}));
}

void test_scan_records_outlive_leaf() {
    std::vector<Record> records;
    {
        CompactLeaf leaf;
        CHECK(leaf.insert("survivor", 8) == LeafInsert::inserted);
        records = leaf.scan("", 1);
    }
    CHECK(matches(records, {{"survivor", 8}}));
}

void test_copy_is_independent() {
    CompactLeaf original;
    CHECK(original.insert("shared", 1) == LeafInsert::inserted);
    CompactLeaf copy = original;
    CHECK(copy.insert("only-copy", 2) == LeafInsert::inserted);
    CHECK(!original.lookup("only-copy").has_value());
    CHECK(copy.lookup("shared") == std::optional<Value>(1));
}

// Applies the same random operations to a leaf and a capacity-limited std::map.
void test_differential_against_map() {
    const std::string_view alphabet("aAbz\x01\x7f");
    std::mt19937 engine(0x4c495453u); // Fixed seed: failures must be reproducible.
    const auto next = [&engine](std::size_t bound) {
        return static_cast<std::size_t>(engine() % bound);
    };
    const auto random_key = [&] {
        std::string key(1 + next(3), ' '); // Short keys force duplicates and prefixes.
        for (char& c : key) {
            c = alphabet[next(alphabet.size())];
        }
        return key;
    };

    for (int round = 0; round < 2000 && failures == 0; ++round) {
        CompactLeaf leaf;
        std::map<std::string, Value> reference;

        for (int step = 0; step < 64 && failures == 0; ++step) {
            const std::string key = random_key();
            switch (next(3)) {
            case 0: {
                const Value value = engine();
                const LeafInsert expected = reference.count(key) != 0
                    ? LeafInsert::duplicate
                    : reference.size() >= compact_leaf_capacity ? LeafInsert::full
                                                                : LeafInsert::inserted;
                CHECK(leaf.insert(key, value) == expected);
                if (expected == LeafInsert::inserted) {
                    reference.emplace(key, value);
                }
                break;
            }
            case 1: {
                const auto found = reference.find(key);
                const std::optional<Value> expected = found == reference.end()
                    ? std::nullopt
                    : std::optional<Value>(found->second);
                CHECK(leaf.lookup(key) == expected);
                break;
            }
            default: {
                const std::size_t limit = next(compact_leaf_capacity + 3);
                Expected expected;
                for (auto it = reference.lower_bound(key);
                     it != reference.end() && expected.size() < limit; ++it) {
                    expected.emplace_back(it->first, it->second);
                }
                CHECK(matches(leaf.scan(key, limit), expected));
                break;
            }
            }
        }

        Expected everything(reference.begin(), reference.end());
        CHECK(matches(leaf.scan("", compact_leaf_capacity), everything));
    }
}

} // namespace

int main() {
    test_empty_leaf();
    test_zero_is_a_stored_value();
    test_maximum_value_round_trips();
    test_duplicate_keeps_original_value();
    test_full_leaf_rejects_without_change();
    test_insert_reports_duplicate_before_full();
    test_any_insertion_order_scans_sorted();
    test_prefix_related_keys();
    test_scan_bounds_and_limits();
    test_supported_byte_boundaries();
    test_unsupported_keys_are_rejected();
    test_unsupported_key_is_rejected_when_full();
    test_leaf_owns_inserted_keys();
    test_scan_records_outlive_leaf();
    test_copy_is_independent();
    test_differential_against_map();

    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "compact leaf tests passed\n";
    return 0;
}
