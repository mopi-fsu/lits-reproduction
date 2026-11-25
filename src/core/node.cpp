#include "lits/core/node.hpp"

#include <stdexcept>
#include <string>
#include <utility>

namespace lits {

const char* to_string(NodeKind kind) noexcept {
    switch (kind) {
    case NodeKind::empty: return "empty";
    case NodeKind::single_record: return "single_record";
    case NodeKind::learned_node: return "learned_node";
    case NodeKind::compact_leaf: return "compact_leaf";
    case NodeKind::hot_subtrie: return "hot_subtrie";
    }
    return "unknown";
}

Slot::Slot() noexcept = default;
Slot::~Slot() = default;

Slot::Slot(Slot&& other) noexcept { take(other); }

Slot& Slot::operator=(Slot&& other) noexcept {
    if (this != &other) {
        take(other);
    }
    return *this;
}

void Slot::take(Slot& other) noexcept {
    kind_ = other.kind_;
    record_ = std::move(other.record_);
    learned_ = std::move(other.learned_);
    leaf_ = std::move(other.leaf_);
    hot_ = std::move(other.hot_);
    other.reset();
}

void Slot::reset() noexcept {
    kind_ = NodeKind::empty;
    record_.key.clear();
    record_.value = 0;
    learned_.reset();
    leaf_.reset();
    hot_.reset();
}

Slot Slot::single(std::string_view key, Value value) {
    Slot slot;
    slot.record_.key.assign(key.data(), key.size());
    slot.record_.value = value;
    slot.kind_ = NodeKind::single_record;
    return slot;
}

Slot Slot::learned(std::unique_ptr<LearnedNode> node) {
    if (!node) throw std::invalid_argument("Slot::learned: null node");
    Slot slot;
    slot.learned_ = std::move(node);
    slot.kind_ = NodeKind::learned_node;
    return slot;
}

Slot Slot::leaf(std::unique_ptr<CompactLeaf> leaf) {
    if (!leaf) throw std::invalid_argument("Slot::leaf: null leaf");
    Slot slot;
    slot.leaf_ = std::move(leaf);
    slot.kind_ = NodeKind::compact_leaf;
    return slot;
}

Slot Slot::hot(std::unique_ptr<HotSubtrie> subtrie) {
    if (!subtrie) throw std::invalid_argument("Slot::hot: null subtrie");
    Slot slot;
    slot.hot_ = std::move(subtrie);
    slot.kind_ = NodeKind::hot_subtrie;
    return slot;
}

namespace {
[[noreturn]] void wrong_kind(const char* wanted, NodeKind actual) {
    throw std::logic_error(std::string("Slot holds ") + to_string(actual) + ", not " +
                           wanted);
}
} // namespace

Record& Slot::record() {
    if (kind_ != NodeKind::single_record) wrong_kind("single_record", kind_);
    return record_;
}
const Record& Slot::record() const {
    if (kind_ != NodeKind::single_record) wrong_kind("single_record", kind_);
    return record_;
}
LearnedNode& Slot::learned_node() {
    if (kind_ != NodeKind::learned_node) wrong_kind("learned_node", kind_);
    return *learned_;
}
const LearnedNode& Slot::learned_node() const {
    if (kind_ != NodeKind::learned_node) wrong_kind("learned_node", kind_);
    return *learned_;
}
CompactLeaf& Slot::compact_leaf() {
    if (kind_ != NodeKind::compact_leaf) wrong_kind("compact_leaf", kind_);
    return *leaf_;
}
const CompactLeaf& Slot::compact_leaf() const {
    if (kind_ != NodeKind::compact_leaf) wrong_kind("compact_leaf", kind_);
    return *leaf_;
}

} // namespace lits
