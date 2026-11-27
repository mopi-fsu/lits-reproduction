#include "lits/leaf/compact_leaf.hpp"

#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>

namespace lits {
namespace {

bool has_only_supported_bytes(std::string_view text) {
    return std::all_of(text.begin(), text.end(), [](char c) {
        const auto byte = static_cast<unsigned char>(c);
        return byte >= 1 && byte <= 127;
    });
}

void require_supported_key(std::string_view key) {
    if (key.empty() || !has_only_supported_bytes(key)) {
        throw std::invalid_argument(
            "CompactLeaf: keys must be nonempty and contain only bytes 1..127");
    }
}

auto first_not_less(const std::vector<Record>& records, std::string_view key) {
    return std::lower_bound(records.begin(), records.end(), key,
                            [](const Record& record, std::string_view target) {
                                return std::string_view(record.key) < target;
                            });
}

} // namespace

LeafInsert CompactLeaf::insert(std::string_view key, Value value) {
    require_supported_key(key);

    // Check for a duplicate first, so a full leaf still reports one: the core
    // must not promote a leaf just because an existing key was inserted again.
    const auto position = first_not_less(records_, key);
    if (position != records_.end() && position->key == key) {
        return LeafInsert::duplicate;
    }
    if (records_.size() >= compact_leaf_capacity) {
        return LeafInsert::full;
    }

    // Like the paper's default compact node, keep exactly one slot per record:
    // build a new array with one more slot instead of reserving empty slots.
    // Everything that can throw happens before the leaf is modified.
    std::vector<Record> grown;
    grown.reserve(records_.size() + 1);
    Record record{std::string(key), value};

    auto source = std::make_move_iterator(records_.begin());
    const auto split = std::distance(records_.cbegin(), position);
    grown.insert(grown.end(), source, source + split);
    grown.push_back(std::move(record));
    grown.insert(grown.end(), source + split, std::make_move_iterator(records_.end()));
    records_.swap(grown);
    return LeafInsert::inserted;
}

std::optional<Value> CompactLeaf::lookup(std::string_view key) const {
    require_supported_key(key);

    const auto position = first_not_less(records_, key);
    if (position == records_.end() || position->key != key) {
        return std::nullopt;
    }
    return position->value;
}

std::vector<Record> CompactLeaf::scan(std::string_view lower_bound, std::size_t limit) const {
    if (!lower_bound.empty()) {
        require_supported_key(lower_bound);
    }

    const auto first = first_not_less(records_, lower_bound);
    const auto available = static_cast<std::size_t>(std::distance(first, records_.end()));
    const auto count = std::min(limit, available);
    return std::vector<Record>(first, first + static_cast<std::ptrdiff_t>(count));
}

} // namespace lits
