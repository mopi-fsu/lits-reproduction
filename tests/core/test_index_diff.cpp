// Differential tests: LitsIndex against std::map used purely as a REFERENCE.
// The index never delegates to a map; the map only says what the answer should be.
// Covers bulkload at every structural size, promotion, collisions, prefix-related
// keys, the subtrie fallback, update/erase, and bounded scans.
#include "lits/core/index.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <map>
#include <random>
#include <string>
#include <vector>

static int failures = 0;
// Optional argv[1]: added to every seed, to sweep different random sequences.
static std::uint64_t g_seed_offset = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            ++failures;                                                          \
            std::cerr << __FILE__ << ":" << __LINE__ << " CHECK failed: " #cond  \
                      << "\n";                                                   \
        }                                                                        \
    } while (0)

using lits::LitsIndex;
using lits::NodeKind;
using lits::Record;
using lits::Value;
using Ref = std::map<std::string, Value>;

static std::string random_key(std::mt19937_64& rng, std::size_t alphabet, std::size_t max_len) {
    const std::size_t len = 1 + rng() % max_len;
    std::string key;
    for (std::size_t i = 0; i < len; ++i) key.push_back(static_cast<char>('a' + rng() % alphabet));
    return key;
}

static std::vector<Record> to_records(const Ref& ref) {
    std::vector<Record> out;
    for (const auto& [k, v] : ref) out.push_back({k, v});
    return out;
}

// Prints why a comparison failed, so a failing seed can be diagnosed from the log.
static bool mismatch(const std::string& why) {
    std::cerr << "  mismatch: " << why << '\n';
    return false;
}

// Full comparison: size, every key, a miss probe, and full + bounded scans.
static bool matches(const LitsIndex& idx, const Ref& ref, std::mt19937_64& rng,
                    std::size_t alphabet, std::size_t max_len) {
    if (idx.size() != ref.size()) {
        return mismatch("size " + std::to_string(idx.size()) + " != " + std::to_string(ref.size()));
    }
    const auto all = idx.scan("", ref.size() + 5);
    if (all.size() != ref.size()) {
        return mismatch("full scan returned " + std::to_string(all.size()) + " of " +
                        std::to_string(ref.size()));
    }
    std::size_t i = 0;
    for (const auto& [k, v] : ref) {
        if (all[i].key != k || all[i].value != v) {
            return mismatch("full scan position " + std::to_string(i) + " is '" + all[i].key +
                            "', expected '" + k + "'");
        }
        if (idx.lookup(k) != v) return mismatch("lookup of existing key '" + k + "' failed");
        ++i;
    }
    for (int probe = 0; probe < 20; ++probe) {
        const std::string key = random_key(rng, alphabet, max_len);
        const auto it = ref.find(key);
        const auto got = idx.lookup(key);
        if (it == ref.end() ? got.has_value() : got != it->second) {
            return mismatch("lookup of '" + key + "' gave the wrong answer");
        }

        const std::size_t limit = rng() % 12;
        const auto part = idx.scan(key, limit);
        std::size_t n = 0;
        for (auto lb = ref.lower_bound(key); lb != ref.end() && n < limit; ++lb, ++n) {
            if (n >= part.size() || part[n].key != lb->first || part[n].value != lb->second) {
                return mismatch("scan('" + key + "', " + std::to_string(limit) + ") item " +
                                std::to_string(n) + " is '" +
                                (n < part.size() ? part[n].key : std::string("<missing>")) +
                                "', expected '" + lb->first + "'");
            }
        }
        if (part.size() != n) {
            return mismatch("scan('" + key + "', " + std::to_string(limit) + ") returned " +
                            std::to_string(part.size()) + " items, expected " + std::to_string(n));
        }
    }
    return true;
}

// Random keys over a small alphabet: lots of shared prefixes and prefix-related keys.
static void test_bulkload_sizes() {
    std::mt19937_64 rng((1) + g_seed_offset);
    for (std::size_t n : {0u, 1u, 2u, 15u, 16u, 17u, 18u, 40u, 200u, 1000u, 4000u}) {
        for (std::size_t alphabet : {2u, 4u, 26u}) {
            if (alphabet == 2 && n > 1000) continue; // only 2046 distinct short keys exist
            Ref ref;
            while (ref.size() < n) ref.emplace(random_key(rng, alphabet, 10), rng());
            LitsIndex idx;
            idx.bulkload(to_records(ref));
            CHECK(matches(idx, ref, rng, alphabet, 10));
        }
    }
}

static void test_structure_kinds() {
    std::mt19937_64 rng((2) + g_seed_offset);
    Ref ref;
    while (ref.size() < 2000) ref.emplace(random_key(rng, 26, 12), rng());
    LitsIndex idx;
    idx.bulkload(to_records(ref));
    CHECK(idx.root_kind() == NodeKind::learned_node);
    CHECK(idx.node_count(NodeKind::learned_node) >= 1);
    CHECK(idx.node_count(NodeKind::compact_leaf) >= 1);

    LitsIndex small;
    small.bulkload({{"a", 1}, {"b", 2}, {"c", 3}});
    CHECK(small.root_kind() == NodeKind::compact_leaf);
    CHECK(small.node_count(NodeKind::learned_node) == 0);
}

// Keys a, aa, aaa, ... have equal or nearly equal CDFs: the model cannot separate
// them, so the subtrie fallback must still produce a correct index.
static void test_degenerate_fallback() {
    Ref ref;
    for (std::size_t len = 1; len <= 300; ++len) ref.emplace(std::string(len, 'a'), len);
    std::mt19937_64 rng((3) + g_seed_offset);
    LitsIndex idx;
    idx.bulkload(to_records(ref));
    CHECK(matches(idx, ref, rng, 1, 300));
    CHECK(idx.node_count(NodeKind::hot_subtrie) >= 1);

    // And keys that differ only in the last byte, many sharing a long prefix.
    Ref same_prefix;
    for (char c = 'a'; c <= 'z'; ++c)
        for (char d = 'a'; d <= 'z'; ++d)
            same_prefix.emplace(std::string(40, 'x') + c + d, static_cast<Value>(c) * 256 + d);
    LitsIndex idx2;
    idx2.bulkload(to_records(same_prefix));
    CHECK(matches(idx2, same_prefix, rng, 26, 3));
}

// Start from every initial size and grow by inserts only (promotion paths).
static void test_insert_growth() {
    for (std::size_t start : {0u, 1u, 2u, 16u, 17u, 300u}) {
        for (std::size_t alphabet : {3u, 26u}) {
            std::mt19937_64 rng((10 + start + alphabet) + g_seed_offset);
            Ref ref;
            while (ref.size() < start) ref.emplace(random_key(rng, alphabet, 8), rng());
            LitsIndex idx;
            idx.bulkload(to_records(ref));
            for (int step = 0; step < 1500; ++step) {
                const std::string key = random_key(rng, alphabet, 8);
                const Value v = rng();
                const bool expected = ref.emplace(key, v).second;
                CHECK(idx.insert(key, v) == expected);
                if (step % 250 == 0) CHECK(matches(idx, ref, rng, alphabet, 8));
            }
            CHECK(matches(idx, ref, rng, alphabet, 8));
        }
    }
}

// Mixed insert/update/erase/lookup against the reference.
static void test_random_operations() {
    for (std::size_t alphabet : {2u, 3u, 8u, 26u}) {
        for (std::size_t start : {0u, 20u, 500u}) {
            std::mt19937_64 rng((100 + alphabet * 7 + start) + g_seed_offset);
            Ref ref;
            while (ref.size() < start) ref.emplace(random_key(rng, alphabet, 9), rng());
            LitsIndex idx;
            idx.bulkload(to_records(ref));
            for (int step = 0; step < 4000; ++step) {
                const std::string key = random_key(rng, alphabet, 9);
                const Value v = rng();
                switch (rng() % 5) {
                case 0:
                case 1: {
                    const bool expected = ref.emplace(key, v).second;
                    CHECK(idx.insert(key, v) == expected);
                    break;
                }
                case 2: {
                    const auto it = ref.find(key);
                    CHECK(idx.update(key, v) == (it != ref.end()));
                    if (it != ref.end()) it->second = v;
                    break;
                }
                case 3:
                    CHECK(idx.erase(key) == (ref.erase(key) == 1));
                    break;
                default: {
                    const auto it = ref.find(key);
                    const auto got = idx.lookup(key);
                    CHECK(it == ref.end() ? !got.has_value() : got == it->second);
                }
                }
                if (idx.size() != ref.size()) {
                    CHECK(false);
                    return;
                }
                if (step % 500 == 0) CHECK(matches(idx, ref, rng, alphabet, 9));
            }
            CHECK(matches(idx, ref, rng, alphabet, 9));
            // Drain completely: erase everything, index returns to empty and is reusable.
            for (const auto& rec : to_records(ref)) CHECK(idx.erase(rec.key));
            CHECK(idx.empty() && idx.lookup("a") == std::nullopt && idx.scan("", 5).empty());
            CHECK(idx.insert("again", 1) && idx.lookup("again") == Value{1});
        }
    }
}

// Re-bulkload replaces a large tree; a smaller bulkload drops the trained model.
static void test_rebulkload() {
    std::mt19937_64 rng((5) + g_seed_offset);
    Ref big, small;
    while (big.size() < 1000) big.emplace(random_key(rng, 26, 10), rng());
    while (small.size() < 30) small.emplace(random_key(rng, 26, 10), rng());
    LitsIndex idx;
    idx.bulkload(to_records(big));
    idx.bulkload(to_records(small));
    CHECK(matches(idx, small, rng, 26, 10));
    idx.bulkload(to_records(big));
    CHECK(matches(idx, big, rng, 26, 10));
    // A rejected bulkload must not disturb the existing tree.
    try {
        idx.bulkload({{"b", 1}, {"a", 2}});
        CHECK(false);
    } catch (const std::invalid_argument&) {
    }
    CHECK(matches(idx, big, rng, 26, 10));
}

// Resizing: erasing collapses nodes back into leaves; heavy growth rebuilds nodes.
static void test_resizing() {
    std::mt19937_64 rng((7) + g_seed_offset);
    Ref ref;
    while (ref.size() < 3000) ref.emplace(random_key(rng, 26, 12), rng());
    LitsIndex idx;
    idx.bulkload(to_records(ref));
    CHECK(idx.root_kind() == NodeKind::learned_node);

    // Erase in random order down to a handful of keys: the tree must shrink.
    std::vector<std::string> order;
    for (const auto& kv : ref) order.push_back(kv.first);
    std::shuffle(order.begin(), order.end(), rng);
    std::size_t step = 0;
    while (ref.size() > 12) {
        CHECK(idx.erase(order.back()));
        ref.erase(order.back());
        order.pop_back();
        if (++step % 400 == 0) CHECK(matches(idx, ref, rng, 26, 12));
    }
    CHECK(matches(idx, ref, rng, 26, 12));
    CHECK(idx.root_kind() == NodeKind::compact_leaf);
    CHECK(idx.node_count(NodeKind::learned_node) == 0);
    CHECK(idx.node_count(NodeKind::hot_subtrie) == 0);

    while (ref.size() > 1) {
        CHECK(idx.erase(order.back()));
        ref.erase(order.back());
        order.pop_back();
    }
    CHECK(idx.root_kind() == NodeKind::single_record);
    CHECK(matches(idx, ref, rng, 26, 12));

    // Growth far beyond the build size (sorted inserts are the worst case for a
    // model trained on a small prefix of the key space) stays correct.
    LitsIndex grow;
    Ref gref;
    for (int i = 0; i < 40; ++i) {
        gref.emplace("k" + std::to_string(1000 + i), i);
    }
    grow.bulkload(to_records(gref));
    for (int i = 40; i < 6000; ++i) {
        const std::string key = "k" + std::to_string(1000 + i);
        CHECK(grow.insert(key, i));
        gref.emplace(key, i);
        if (i % 1000 == 0) CHECK(matches(grow, gref, rng, 10, 6));
    }
    CHECK(matches(grow, gref, rng, 10, 6));
    CHECK(grow.node_count(NodeKind::learned_node) >= 1);

    // Subtrie collapse: degenerate keys, then erase most of them.
    Ref chain;
    for (std::size_t len = 1; len <= 200; ++len) chain.emplace(std::string(len, 'a'), len);
    LitsIndex cidx;
    cidx.bulkload(to_records(chain));
    for (std::size_t len = 200; len > 10; --len) {
        CHECK(cidx.erase(std::string(len, 'a')));
        chain.erase(std::string(len, 'a'));
    }
    CHECK(matches(cidx, chain, rng, 1, 12));
    CHECK(cidx.node_count(NodeKind::hot_subtrie) == 0);
}

int main(int argc, char** argv) {
    if (argc > 1) g_seed_offset = std::stoull(argv[1]);
    test_resizing();
    test_bulkload_sizes();
    test_structure_kinds();
    test_degenerate_fallback();
    test_insert_growth();
    test_random_operations();
    test_rebulkload();
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "differential tests passed\n";
    return 0;
}
