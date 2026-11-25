#include "lits/core/index.hpp"

#include "lits/core/validation.hpp"
#include "lits/detail/unimplemented.hpp"

#include <utility>

namespace lits {

void LitsIndex::bulkload(const std::vector<Record>& records) {
    validate_bulkload(records);
    if (records.size() > 1) {
        // Needs HPT/local prediction (Opi), GPKL/PMSS and compact leaves (Oyshi).
        detail::unimplemented("LitsIndex::bulkload of more than one record");
    }
    // Build the new state first so nothing above or below can leave a half-state.
    Slot fresh;
    if (records.size() == 1) fresh = Slot::single(records[0].key, records[0].value);
    root_ = std::move(fresh);
    size_ = records.size();
}

std::optional<Value> LitsIndex::lookup(std::string_view key) const {
    validate_key(key, "lookup key");
    switch (root_.kind()) {
    case NodeKind::empty:
        return std::nullopt;
    case NodeKind::single_record:
        if (root_.record().key == key) return root_.record().value;
        return std::nullopt;
    default:
        detail::unimplemented("LitsIndex::lookup routing (learned/leaf/HOT)");
    }
}

bool LitsIndex::insert(std::string_view key, Value value) {
    validate_key(key, "insert key");
    switch (root_.kind()) {
    case NodeKind::empty:
        root_ = Slot::single(key, value);
        size_ = 1;
        return true;
    case NodeKind::single_record:
        if (root_.record().key == key) return false;
        // Growing past one record needs a leaf or learned node.
        detail::unimplemented("LitsIndex::insert promotion from single record");
    default:
        detail::unimplemented("LitsIndex::insert routing (learned/leaf/HOT)");
    }
}

bool LitsIndex::update(std::string_view key, Value value) {
    validate_key(key, "update key");
    switch (root_.kind()) {
    case NodeKind::empty:
        return false;
    case NodeKind::single_record:
        if (root_.record().key != key) return false;
        root_.record().value = value;
        return true;
    default:
        detail::unimplemented("LitsIndex::update routing (learned/leaf/HOT)");
    }
}

bool LitsIndex::erase(std::string_view key) {
    validate_key(key, "erase key");
    switch (root_.kind()) {
    case NodeKind::empty:
        return false;
    case NodeKind::single_record:
        if (root_.record().key != key) return false;
        root_.reset();
        size_ = 0;
        return true;
    default:
        detail::unimplemented("LitsIndex::erase routing (learned/leaf/HOT)");
    }
}

std::vector<Record> LitsIndex::scan(std::string_view lower_bound, std::size_t limit) const {
    validate_scan_bound(lower_bound);
    if (limit == 0) return {};
    switch (root_.kind()) {
    case NodeKind::empty:
        return {};
    case NodeKind::single_record:
        if (std::string_view(root_.record().key) >= lower_bound) return {root_.record()};
        return {};
    default:
        detail::unimplemented("LitsIndex::scan traversal (learned/leaf/HOT)");
    }
}

} // namespace lits
