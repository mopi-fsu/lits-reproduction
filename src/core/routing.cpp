#include "lits/core/routing.hpp"

#include <stdexcept>

namespace lits {

PrefixRelation relate_to_prefix(std::string_view prefix, std::string_view key) noexcept {
    if (key.size() >= prefix.size() && key.substr(0, prefix.size()) == prefix) {
        return PrefixRelation::within;
    }
    // string_view ordering is lexicographic byte order, matching the index order.
    return key < prefix ? PrefixRelation::below : PrefixRelation::above;
}

std::size_t bound_slot(std::size_t predicted, std::size_t slot_count) {
    if (slot_count == 0) {
        throw std::invalid_argument("bound_slot: slot_count must be positive");
    }
    return predicted < slot_count ? predicted : slot_count - 1;
}

} // namespace lits
