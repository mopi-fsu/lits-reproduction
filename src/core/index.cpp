#include "lits/core/index.hpp"

#include "lits/core/routing.hpp"
#include "lits/core/validation.hpp"
#include "lits/model/gpkl.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace lits {
namespace {

using Records = std::vector<Record>;

// A contiguous, sorted, unique run of records (a view; owns nothing).
struct Range {
    const Record* first;
    std::size_t count;
    const Record& operator[](std::size_t i) const { return first[i]; }
    Range sub(std::size_t begin, std::size_t end) const { return {first + begin, end - begin}; }
};

[[noreturn]] void internal_error(const char* what) {
    throw std::logic_error(std::string("LitsIndex internal error: ") + what);
}

// ---------------------------------------------------------------- building --

Slot build_group(Range r, const Hpt* hpt);

Slot make_leaf(Range r) {
    auto leaf = std::make_unique<CompactLeaf>();
    for (std::size_t i = 0; i < r.count; ++i) {
        if (leaf->insert(r[i].key, r[i].value) != LeafInsert::inserted) {
            internal_error("compact leaf build rejected a record");
        }
    }
    return Slot::leaf(std::move(leaf));
}

std::size_t route_slot(const LearnedNode& node, std::string_view key, const Hpt* hpt) {
    if (hpt == nullptr) internal_error("learned node without a trained HPT");
    const double cdf = hpt->cdf(key, node.prefix.size());
    return bound_slot(node.model.predict(cdf, node.slots.size()), node.slots.size());
}

// Returns nullopt when the model cannot be used for this group; the caller then
// uses the subtrie. Every nullopt path leaves nothing half-built behind.
std::optional<Slot> try_build_learned(Range r, const Hpt& hpt) {
    const std::size_t n = r.count;
    const std::size_t prefix_len = common_prefix_length(r[0].key, r[n - 1].key);

    std::vector<double> cdfs(n);
    for (std::size_t i = 0; i < n; ++i) cdfs[i] = hpt.cdf(r[i].key, prefix_len);
    // The ordered scan relies on slot order matching key order, so insist on it.
    for (std::size_t i = 1; i < n; ++i) {
        if (cdfs[i] < cdfs[i - 1]) return std::nullopt;
    }

    LocalLinearModel model;
    try {
        model = LocalLinearModel::fit(cdfs.front(), cdfs.back());
    } catch (const std::domain_error&) {
        return std::nullopt; // equal or unrepresentably close endpoints
    }

    auto node = std::make_unique<LearnedNode>();
    node->prefix = r[0].key.substr(0, prefix_len);
    node->model = model;
    node->slots.resize(n);
    node->count = node->built_count = n;

    std::size_t begin = 0;
    bool have_last = false;
    std::size_t last_slot = 0;
    while (begin < n) {
        const std::size_t slot = bound_slot(model.predict(cdfs[begin], n), n);
        std::size_t end = begin + 1;
        while (end < n && bound_slot(model.predict(cdfs[end], n), n) == slot) ++end;
        if (end - begin == n) return std::nullopt;       // no progress: all in one slot
        if (have_last && slot <= last_slot) return std::nullopt; // not monotone
        node->slots[slot] = build_group(r.sub(begin, end), &hpt);
        have_last = true;
        last_slot = slot;
        begin = end;
    }
    return Slot::learned(std::move(node));
}

// Always makes progress: the group's common prefix is maximal, so either one key
// equals the prefix or at least two distinct next bytes exist; every child is
// strictly smaller than the group.
Slot build_subtrie(Range r, const Hpt* hpt) {
    const std::size_t n = r.count;
    auto node = std::make_unique<HotSubtrie>();
    const std::size_t p = common_prefix_length(r[0].key, r[n - 1].key);
    node->prefix = r[0].key.substr(0, p);
    node->count = node->built_count = n;

    std::size_t i = 0;
    if (r[0].key.size() == p) {
        node->terminal = r[0];
        i = 1;
    }
    while (i < n) {
        const auto byte = static_cast<unsigned char>(r[i].key[p]);
        std::size_t j = i + 1;
        while (j < n && static_cast<unsigned char>(r[j].key[p]) == byte) ++j;
        node->children.emplace_back(byte, build_group(r.sub(i, j), hpt));
        i = j;
    }
    return Slot::hot(std::move(node));
}

Slot build_group(Range r, const Hpt* hpt) {
    if (r.count == 0) return Slot();
    if (r.count == 1) return Slot::single(r[0].key, r[0].value);
    if (r.count <= compact_leaf_capacity) return make_leaf(r);
    if (hpt == nullptr) internal_error("group needs a model but no HPT is trained");
    if (auto learned = try_build_learned(r, *hpt)) return std::move(*learned);
    return build_subtrie(r, hpt);
}

std::unique_ptr<Hpt> train_hpt(const Records& sorted) {
    std::vector<std::string_view> views;
    views.reserve(sorted.size());
    for (const Record& rec : sorted) views.emplace_back(rec.key);
    auto hpt = std::make_unique<Hpt>();
    hpt->train(views); // views are not retained
    return hpt;
}

// ----------------------------------------------------------------- routing --

// The child slot of an internal node that could hold `key`, or nullptr when the
// key cannot be below it (outside the prefix, or no child for its next byte).
// A subtrie's terminal key is handled by the caller.
template <class SlotT>
SlotT* child_for(SlotT& slot, std::string_view key, const Hpt* hpt) {
    if (slot.kind() == NodeKind::learned_node) {
        auto& node = slot.learned_node();
        if (relate_to_prefix(node.prefix, key) != PrefixRelation::within) return nullptr;
        return &node.slots[route_slot(node, key, hpt)];
    }
    auto& node = slot.hot_subtrie();
    const std::size_t p = node.prefix.size();
    if (key.size() <= p || key.compare(0, p, node.prefix) != 0) return nullptr;
    const auto byte = static_cast<unsigned char>(key[p]);
    auto it = std::lower_bound(node.children.begin(), node.children.end(), byte,
                               [](const auto& entry, unsigned char b) { return entry.first < b; });
    if (it == node.children.end() || it->first != byte) return nullptr;
    return &it->second;
}

template <class SlotT>
struct Target {
    SlotT* slot = nullptr;  // a single_record, a compact_leaf, or (terminal) a subtrie
    bool terminal = false;  // the key is the subtrie's own terminal key
};

// Walks down to the slot that would hold `key`. A null slot means a certain miss.
template <class SlotT>
Target<SlotT> locate(SlotT& root, std::string_view key, const Hpt* hpt) {
    SlotT* cur = &root;
    while (cur != nullptr) {
        switch (cur->kind()) {
        case NodeKind::empty:
            return {};
        case NodeKind::single_record:
        case NodeKind::compact_leaf:
            return {cur, false};
        case NodeKind::hot_subtrie:
            if (key == std::string_view(cur->hot_subtrie().prefix)) return {cur, true};
            cur = child_for(*cur, key, hpt);
            break;
        case NodeKind::learned_node:
            cur = child_for(*cur, key, hpt);
            break;
        }
    }
    return {};
}

// -------------------------------------------------------------- leaf upkeep --
// CompactLeaf has no update/erase yet, so rebuild it from scan() (<= 16 records).

Records leaf_records(const Slot& leaf_slot) {
    return leaf_slot.compact_leaf().scan("", compact_leaf_capacity);
}

// Replace a leaf slot with the right smaller structure for `records` (sorted).
Slot shrink_to(const Records& records) {
    return build_group(Range{records.data(), records.size()}, nullptr);
}

// ------------------------------------------------------------------- scan ----

void collect(const Slot& slot, std::string_view lower, std::size_t limit, Records& out,
             const Hpt* hpt) {
    if (out.size() >= limit) return;
    switch (slot.kind()) {
    case NodeKind::empty:
        return;
    case NodeKind::single_record:
        if (lower.empty() || std::string_view(slot.record().key) >= lower) {
            out.push_back(slot.record());
        }
        return;
    case NodeKind::compact_leaf: {
        Records part = slot.compact_leaf().scan(lower, limit - out.size());
        for (Record& rec : part) out.push_back(std::move(rec));
        return;
    }
    case NodeKind::learned_node: {
        const LearnedNode& node = slot.learned_node();
        std::size_t first = 0;
        if (!lower.empty()) {
            switch (relate_to_prefix(node.prefix, lower)) {
            case PrefixRelation::above: return; // every key here is < lower
            case PrefixRelation::below: lower = {}; break;
            case PrefixRelation::within: first = route_slot(node, lower, hpt); break;
            }
        }
        // Only the predicted slot can mix keys below and above `lower`.
        for (std::size_t s = first; s < node.slots.size() && out.size() < limit; ++s) {
            collect(node.slots[s], s == first ? lower : std::string_view{}, limit, out, hpt);
        }
        return;
    }
    case NodeKind::hot_subtrie: {
        const HotSubtrie& node = slot.hot_subtrie();
        bool bounded = false;
        unsigned char bound_byte = 0;
        if (!lower.empty()) {
            switch (relate_to_prefix(node.prefix, lower)) {
            case PrefixRelation::above: return;
            case PrefixRelation::below: break;
            case PrefixRelation::within:
                if (lower.size() > node.prefix.size()) {
                    bounded = true;
                    bound_byte = static_cast<unsigned char>(lower[node.prefix.size()]);
                }
                break;
            }
        }
        if (!bounded && node.terminal) out.push_back(*node.terminal); // key == prefix sorts first
        for (const auto& [byte, child] : node.children) {
            if (out.size() >= limit) break;
            if (bounded && byte < bound_byte) continue;
            collect(child, (bounded && byte == bound_byte) ? lower : std::string_view{}, limit,
                    out, hpt);
        }
        return;
    }
    }
}

std::size_t count_kind(const Slot& slot, NodeKind kind) {
    std::size_t total = slot.kind() == kind ? 1 : 0;
    if (slot.kind() == NodeKind::learned_node) {
        for (const Slot& child : slot.learned_node().slots) total += count_kind(child, kind);
    } else if (slot.kind() == NodeKind::hot_subtrie) {
        for (const auto& entry : slot.hot_subtrie().children) {
            total += count_kind(entry.second, kind);
        }
    }
    return total;
}

// Records below an internal slot (0 for other kinds); used for resizing decisions.
std::size_t subtree_count(const Slot& slot) {
    if (slot.kind() == NodeKind::learned_node) return slot.learned_node().count;
    if (slot.kind() == NodeKind::hot_subtrie) return slot.hot_subtrie().count;
    return 0;
}

// Replace an internal slot with a freshly built structure of its current records.
void rebuild(Slot& slot, const Hpt* hpt) {
    Records all;
    collect(slot, {}, std::numeric_limits<std::size_t>::max(), all, hpt);
    Slot fresh = build_group(Range{all.data(), all.size()}, hpt);
    slot = std::move(fresh);
}

// After an erase: an internal node that now fits a compact leaf becomes one (or a
// single record, or empty). Rebuilds from the actual records, so a wrong count can
// only cost time, never correctness.
void collapse_if_small(Slot& slot, const Hpt* hpt) {
    if (subtree_count(slot) <= compact_leaf_capacity) rebuild(slot, hpt);
}

bool erase_at(Slot& slot, std::string_view key, const Hpt* hpt) {
    switch (slot.kind()) {
    case NodeKind::empty:
        return false;
    case NodeKind::single_record:
        if (slot.record().key != key) return false;
        slot.reset();
        return true;
    case NodeKind::compact_leaf: {
        Records records = leaf_records(slot);
        auto it = std::find_if(records.begin(), records.end(),
                               [&](const Record& rec) { return rec.key == key; });
        if (it == records.end()) return false;
        records.erase(it);
        slot = shrink_to(records); // 1 record -> single, otherwise a smaller leaf
        return true;
    }
    case NodeKind::learned_node:
    case NodeKind::hot_subtrie: {
        if (slot.kind() == NodeKind::hot_subtrie &&
            key == std::string_view(slot.hot_subtrie().prefix)) {
            HotSubtrie& node = slot.hot_subtrie();
            if (!node.terminal) return false;
            node.terminal.reset();
            --node.count;
        } else {
            Slot* child = child_for(slot, key, hpt);
            if (child == nullptr || !erase_at(*child, key, hpt)) return false;
            if (slot.kind() == NodeKind::learned_node) {
                --slot.learned_node().count;
            } else {
                --slot.hot_subtrie().count;
            }
        }
        collapse_if_small(slot, hpt);
        return true;
    }
    }
    internal_error("unknown NodeKind");
}

Records insert_sorted(Records records, std::string_view key, Value value) {
    auto pos = std::lower_bound(records.begin(), records.end(), key,
                                [](const Record& rec, std::string_view k) {
                                    return std::string_view(rec.key) < k;
                                });
    records.insert(pos, Record{std::string(key), value});
    return records;
}

} // namespace

// ------------------------------------------------------------ public API ----

// Resizing: once a node holds twice the records it was built for, rebuild it so
// its slot count and model fit the data again. Doubling keeps the cost amortized.
void LitsIndex::grow_if_doubled(Slot& slot) {
    std::size_t count = 0, built = 0;
    if (slot.kind() == NodeKind::learned_node) {
        count = slot.learned_node().count;
        built = slot.learned_node().built_count;
    } else if (slot.kind() == NodeKind::hot_subtrie) {
        count = slot.hot_subtrie().count;
        built = slot.hot_subtrie().built_count;
    }
    if (built > 0 && count >= 2 * built) rebuild(slot, hpt_.get());
}

void LitsIndex::ensure_hpt(const Records& sorted_records) {
    if (!hpt_) hpt_ = train_hpt(sorted_records);
}

void LitsIndex::bulkload(const Records& records) {
    validate_bulkload(records);
    // Build everything first; commit with non-throwing moves so a failure (e.g.
    // bad_alloc) leaves the previous contents intact.
    std::unique_ptr<Hpt> hpt;
    if (records.size() > compact_leaf_capacity) hpt = train_hpt(records);
    Slot fresh = build_group(Range{records.data(), records.size()}, hpt.get());
    root_ = std::move(fresh);
    hpt_ = std::move(hpt);
    size_ = records.size();
}

std::optional<Value> LitsIndex::lookup(std::string_view key) const {
    validate_key(key, "lookup key");
    const auto target = locate(root_, key, hpt_.get());
    if (target.slot == nullptr) return std::nullopt;
    if (target.terminal) {
        const auto& terminal = target.slot->hot_subtrie().terminal;
        if (terminal) return terminal->value;
        return std::nullopt;
    }
    if (target.slot->kind() == NodeKind::single_record) {
        if (target.slot->record().key == key) return target.slot->record().value;
        return std::nullopt;
    }
    return target.slot->compact_leaf().lookup(key);
}

bool LitsIndex::insert(std::string_view key, Value value) {
    validate_key(key, "insert key");
    if (!insert_at(root_, key, value)) return false;
    ++size_;
    return true;
}

bool LitsIndex::insert_at(Slot& slot, std::string_view key, Value value) {
    switch (slot.kind()) {
    case NodeKind::empty:
        slot = Slot::single(key, value);
        return true;

    case NodeKind::single_record: {
        if (slot.record().key == key) return false;
        // A collision creates a two-key compact leaf (paper, Section 4.1).
        Records two = insert_sorted(Records{slot.record()}, key, value);
        slot = shrink_to(two);
        return true;
    }

    case NodeKind::compact_leaf:
        switch (slot.compact_leaf().insert(key, value)) {
        case LeafInsert::inserted: return true;
        case LeafInsert::duplicate: return false;
        case LeafInsert::full: {
            // Promote: drain the leaf (leaf unchanged on `full`), add the key, rebuild.
            Records all = insert_sorted(leaf_records(slot), key, value);
            ensure_hpt(all);
            slot = build_group(Range{all.data(), all.size()}, hpt_.get());
            return true;
        }
        }
        internal_error("unknown LeafInsert");

    case NodeKind::learned_node: {
        LearnedNode& node = slot.learned_node();
        if (relate_to_prefix(node.prefix, key) != PrefixRelation::within) {
            // The key lies outside every key this node was built for, so it
            // cannot be a duplicate. Rebuild the subtree to include it.
            Records all;
            collect(slot, {}, std::numeric_limits<std::size_t>::max(), all, hpt_.get());
            all = insert_sorted(std::move(all), key, value);
            slot = build_group(Range{all.data(), all.size()}, hpt_.get());
            return true;
        }
        if (!insert_at(node.slots[route_slot(node, key, hpt_.get())], key, value)) return false;
        ++node.count;
        grow_if_doubled(slot);
        return true;
    }

    case NodeKind::hot_subtrie: {
        HotSubtrie& node = slot.hot_subtrie();
        const std::size_t p = node.prefix.size();
        const std::size_t lcp = common_prefix_length(node.prefix, key);
        if (lcp < p) {
            // Split: a new parent holds the shorter shared prefix. Allocate first,
            // then move, so a failure cannot lose the existing subtree.
            auto parent = std::make_unique<HotSubtrie>();
            parent->prefix = node.prefix.substr(0, lcp);
            parent->count = parent->built_count = node.count + 1;
            const auto old_byte = static_cast<unsigned char>(node.prefix[lcp]);
            Slot fresh;
            std::optional<Record> terminal;
            if (key.size() == lcp) {
                terminal = Record{std::string(key), value};
            } else {
                fresh = Slot::single(key, value);
            }
            parent->children.reserve(2);
            parent->terminal = std::move(terminal);
            const bool new_first = key.size() > lcp && static_cast<unsigned char>(key[lcp]) < old_byte;
            if (key.size() > lcp && new_first) {
                parent->children.emplace_back(static_cast<unsigned char>(key[lcp]), std::move(fresh));
            }
            parent->children.emplace_back(old_byte, std::move(slot));
            if (key.size() > lcp && !new_first) {
                parent->children.emplace_back(static_cast<unsigned char>(key[lcp]), std::move(fresh));
            }
            slot = Slot::hot(std::move(parent));
            return true;
        }
        if (key.size() == p) {
            if (node.terminal) return false;
            node.terminal = Record{std::string(key), value};
            ++node.count;
            grow_if_doubled(slot);
            return true;
        }
        const auto byte = static_cast<unsigned char>(key[p]);
        auto it = std::lower_bound(node.children.begin(), node.children.end(), byte,
                                   [](const auto& entry, unsigned char b) { return entry.first < b; });
        if (it == node.children.end() || it->first != byte) {
            it = node.children.emplace(it, byte, Slot());
        }
        if (!insert_at(it->second, key, value)) return false;
        ++node.count;
        grow_if_doubled(slot);
        return true;
    }
    }
    internal_error("unknown NodeKind");
}

bool LitsIndex::update(std::string_view key, Value value) {
    validate_key(key, "update key");
    const auto target = locate(root_, key, hpt_.get());
    if (target.slot == nullptr) return false;
    if (target.terminal) {
        auto& terminal = target.slot->hot_subtrie().terminal;
        if (!terminal) return false;
        terminal->value = value;
        return true;
    }
    if (target.slot->kind() == NodeKind::single_record) {
        if (target.slot->record().key != key) return false;
        target.slot->record().value = value;
        return true;
    }
    Records records = leaf_records(*target.slot);
    for (Record& rec : records) {
        if (rec.key == key) {
            rec.value = value;
            *target.slot = shrink_to(records);
            return true;
        }
    }
    return false;
}

bool LitsIndex::erase(std::string_view key) {
    validate_key(key, "erase key");
    if (!erase_at(root_, key, hpt_.get())) return false;
    --size_;
    return true;
}

std::vector<Record> LitsIndex::scan(std::string_view lower_bound, std::size_t limit) const {
    validate_scan_bound(lower_bound);
    Records out;
    if (limit == 0) return out;
    collect(root_, lower_bound, limit, out, hpt_.get());
    return out;
}

std::size_t LitsIndex::node_count(NodeKind kind) const { return count_kind(root_, kind); }

} // namespace lits
