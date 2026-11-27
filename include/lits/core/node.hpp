#pragma once

#include "lits/leaf/compact_leaf.hpp"
#include "lits/model/hpt.hpp"
#include "lits/types.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace lits {

// What a slot currently holds. See docs/interfaces.md ("Core node model").
enum class NodeKind {
    empty,         // nothing stored
    single_record, // exactly one record, key owned inline by the slot
    learned_node,  // model + child slots; children are routed by prediction
    compact_leaf,  // up to compact_leaf_capacity records (Oyshi's CompactLeaf)
    hot_subtrie    // fallback subtrie for hard key ranges (not yet designed)
};

const char* to_string(NodeKind kind) noexcept;

struct LearnedNode;
struct HotSubtrie;

// One position in the tree. A slot exclusively owns whatever it holds, including
// the key bytes of a single record. Move-only: a moved-from slot is empty, and
// destroying/resetting a slot releases its entire subtree. Not copyable because
// a deep copy of a subtree is never needed and would hide accidental O(n) work.
class Slot {
public:
    Slot() noexcept;
    ~Slot();
    Slot(Slot&& other) noexcept;
    Slot& operator=(Slot&& other) noexcept;
    Slot(const Slot&) = delete;
    Slot& operator=(const Slot&) = delete;

    // Copies `key` into the slot; the caller's storage is not retained.
    static Slot single(std::string_view key, Value value);
    static Slot learned(std::unique_ptr<LearnedNode> node);
    static Slot leaf(std::unique_ptr<CompactLeaf> leaf);
    static Slot hot(std::unique_ptr<HotSubtrie> subtrie);

    NodeKind kind() const noexcept { return kind_; }
    bool empty() const noexcept { return kind_ == NodeKind::empty; }

    // Accessors throw std::logic_error when the slot holds a different kind.
    Record& record();
    const Record& record() const;
    LearnedNode& learned_node();
    const LearnedNode& learned_node() const;
    CompactLeaf& compact_leaf();
    const CompactLeaf& compact_leaf() const;

    void reset() noexcept;

private:
    void take(Slot& other) noexcept;

    NodeKind kind_ = NodeKind::empty;
    Record record_{};
    std::unique_ptr<LearnedNode> learned_;
    std::unique_ptr<CompactLeaf> leaf_;
    std::unique_ptr<HotSubtrie> hot_;
};

// Internal node shape (no build or routing logic yet; see docs/interfaces.md).
struct LearnedNode {
    // Bytes shared by every key below this node. Confirmed by the core before
    // the model is asked for a prediction.
    std::string prefix;
    LocalLinearModel model;
    // Child slots; slots.size() is the `slot_count` passed to model.predict.
    std::vector<Slot> slots;
};

// Placeholder: subtrie ownership is an open decision.
struct HotSubtrie {};

} // namespace lits
