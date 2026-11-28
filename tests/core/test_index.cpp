// Tests for the single-threaded core representation and bulkload validation.
// Dependency-free: plain asserts that stay active in Release builds.
#include "lits/core/index.hpp"
#include "lits/core/node.hpp"
#include "lits/core/validation.hpp"

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

static int failures = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            ++failures;                                                          \
            std::cerr << __FILE__ << ":" << __LINE__ << " CHECK failed: " #cond  \
                      << "\n";                                                   \
        }                                                                        \
    } while (0)

template <class Ex, class F>
static bool throws(F&& f) {
    try {
        f();
    } catch (const Ex&) {
        return true;
    } catch (...) {
        return false;
    }
    return false;
}

#define CHECK_THROWS(Ex, expr) CHECK(throws<Ex>([&] { (void)(expr); }))

using lits::LitsIndex;
using lits::NodeKind;
using lits::Record;
using lits::Slot;

static_assert(!std::is_copy_constructible_v<LitsIndex>, "index is move-only");
static_assert(!std::is_copy_assignable_v<LitsIndex>, "index is move-only");
static_assert(!std::is_copy_constructible_v<Slot>, "slot is move-only");
static_assert(std::is_nothrow_move_constructible_v<LitsIndex>, "index moves must not throw");
static_assert(std::is_nothrow_move_constructible_v<Slot>, "slot moves must not throw");

static void test_key_validation() {
    CHECK(lits::is_supported_key("a"));
    CHECK(lits::is_supported_key("\x01\x7f"));
    CHECK(!lits::is_supported_key(""));
    CHECK(!lits::is_supported_key(std::string("a\0b", 3)));
    CHECK(!lits::is_supported_key("caf\xc3\xa9")); // UTF-8 is out of scope for now
    CHECK(!lits::is_supported_key("\x80"));
    CHECK_THROWS(std::invalid_argument, lits::validate_key("", "k"));
}

static void test_bulkload_validation() {
    using V = std::vector<Record>;
    lits::validate_bulkload({});                        // empty is valid
    lits::validate_bulkload(V{{"a", 1}});               // singleton
    lits::validate_bulkload(V{{"a", 1}, {"ab", 2}, {"b", 3}}); // prefix-related keys ok
    CHECK_THROWS(std::invalid_argument, lits::validate_bulkload(V{{"b", 1}, {"a", 2}}));
    CHECK_THROWS(std::invalid_argument, lits::validate_bulkload(V{{"a", 1}, {"a", 2}}));
    CHECK_THROWS(std::invalid_argument, lits::validate_bulkload(V{{"a", 1}, {"", 2}}));
    CHECK_THROWS(std::invalid_argument,
                 lits::validate_bulkload(V{{"a", 1}, {std::string("b\0", 2), 2}}));
    CHECK_THROWS(std::invalid_argument, lits::validate_bulkload(V{{"a", 1}, {"b\xff", 2}}));
    // Non-adjacent disorder is still caught.
    CHECK_THROWS(std::invalid_argument,
                 lits::validate_bulkload(V{{"a", 1}, {"c", 2}, {"b", 3}}));

    // The message identifies the offending record.
    try {
        lits::validate_bulkload(V{{"a", 1}, {"b", 2}, {"b", 3}});
        CHECK(false);
    } catch (const std::invalid_argument& e) {
        CHECK(std::string(e.what()).find("record 2") != std::string::npos);
    }
}

static void test_slot_ownership() {
    std::string source = "hello";
    Slot s = Slot::single(source, 7);
    source[0] = 'X'; // mutate and then destroy the caller's string
    source.assign(1000, 'z');
    CHECK(s.kind() == NodeKind::single_record);
    CHECK(s.record().key == "hello");
    CHECK(s.record().value == 7);

    Slot moved = std::move(s);
    CHECK(moved.record().key == "hello");
    CHECK(s.empty()); // moved-from slot is empty and reusable
    s = Slot::single("again", 1);
    CHECK(s.record().key == "again");

    Slot& alias = moved;
    moved = std::move(alias); // self-move must not lose data
    CHECK(moved.record().key == "hello");

    moved.reset();
    CHECK(moved.empty());
    CHECK_THROWS(std::logic_error, moved.record());
    CHECK_THROWS(std::logic_error, s.learned_node());

    // Owning an internal node releases children with it (checked under sanitizers).
    auto node = std::make_unique<lits::LearnedNode>();
    node->prefix = "ab";
    node->slots.resize(4);
    node->slots[2] = Slot::single("abc", 3);
    node->slots[3] = Slot::leaf(std::make_unique<lits::CompactLeaf>());
    Slot parent = Slot::learned(std::move(node));
    CHECK(parent.kind() == NodeKind::learned_node);
    CHECK(parent.learned_node().slots[2].record().key == "abc");
    CHECK(parent.learned_node().slots[3].kind() == NodeKind::compact_leaf);
    CHECK_THROWS(std::invalid_argument, Slot::learned(nullptr));
}

static void test_empty_index() {
    LitsIndex idx;
    CHECK(idx.empty() && idx.size() == 0 && idx.root_kind() == NodeKind::empty);
    CHECK(!idx.lookup("a").has_value());
    CHECK(!idx.update("a", 1));
    CHECK(!idx.erase("a"));
    CHECK(idx.scan("", 10).empty());
    CHECK_THROWS(std::invalid_argument, idx.lookup(""));
    CHECK_THROWS(std::invalid_argument, idx.insert("\x80", 1));
    CHECK_THROWS(std::invalid_argument, idx.scan("\x80", 1));
}

static void test_bulkload_behavior() {
    LitsIndex idx;
    idx.bulkload({{"k", 5}});
    CHECK(idx.size() == 1 && idx.root_kind() == NodeKind::single_record);
    CHECK(idx.lookup("k") == lits::Value{5});
    CHECK(!idx.lookup("j").has_value());
    CHECK(!idx.lookup("kk").has_value()); // longer than stored key
    CHECK_THROWS(std::invalid_argument, idx.lookup("")); // unsupported key, not a miss

    // Invalid input leaves previous contents untouched (strong guarantee).
    CHECK_THROWS(std::invalid_argument, idx.bulkload({{"b", 1}, {"a", 2}}));
    CHECK(idx.size() == 1 && idx.lookup("k") == lits::Value{5});

    // Two records become a compact leaf.
    idx.bulkload({{"a", 1}, {"b", 2}});
    CHECK(idx.size() == 2 && idx.root_kind() == NodeKind::compact_leaf);
    CHECK(idx.lookup("b") == lits::Value{2});
    idx.bulkload({{"k", 5}});

    // Repeated bulkload replaces rather than merges.
    idx.bulkload({{"other", 9}});
    CHECK(idx.size() == 1);
    CHECK(!idx.lookup("k").has_value());
    CHECK(idx.lookup("other") == lits::Value{9});

    // Empty bulkload is valid and clears.
    idx.bulkload({});
    CHECK(idx.empty() && idx.root_kind() == NodeKind::empty);
    CHECK(!idx.lookup("other").has_value());
}

static void test_single_record_operations() {
    LitsIndex idx;
    CHECK(idx.insert("m", 0)); // value zero is valid
    CHECK(idx.lookup("m") == lits::Value{0});
    CHECK(!idx.insert("m", 1)); // duplicate rejected, value unchanged
    CHECK(idx.lookup("m") == lits::Value{0});
    CHECK(idx.update("m", 42));
    CHECK(idx.lookup("m") == lits::Value{42});
    CHECK(!idx.update("n", 1));
    CHECK(!idx.erase("n"));

    // scan: inclusive lower bound, zero limit.
    CHECK(idx.scan("", 5).size() == 1);
    CHECK(idx.scan("m", 5).size() == 1); // inclusive
    CHECK(idx.scan("a", 5)[0].key == "m");
    CHECK(idx.scan("n", 5).empty());
    CHECK(idx.scan("", 0).empty());

    // A colliding key turns the single record into a two-key compact leaf.
    CHECK(idx.insert("z", 1));
    CHECK(idx.size() == 2 && idx.root_kind() == NodeKind::compact_leaf);
    CHECK(idx.lookup("m") == lits::Value{42} && idx.lookup("z") == lits::Value{1});
    CHECK(idx.erase("z")); // shrinks back to a single record
    CHECK(idx.root_kind() == NodeKind::single_record);

    CHECK(idx.erase("m"));
    CHECK(idx.empty() && !idx.lookup("m").has_value());
    CHECK(idx.insert("m", 3)); // reusable after erase
}

static void test_index_move() {
    LitsIndex a;
    a.insert("k", 1);
    LitsIndex b = std::move(a);
    CHECK(b.lookup("k") == lits::Value{1});
    CHECK(a.empty() && a.root_kind() == NodeKind::empty); // moved-from is empty
    a = std::move(b);
    CHECK(a.lookup("k") == lits::Value{1} && b.empty());
}

int main() {
    test_key_validation();
    test_bulkload_validation();
    test_slot_ownership();
    test_empty_index();
    test_bulkload_behavior();
    test_single_record_operations();
    test_index_move();
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "core tests passed\n";
    return 0;
}
