#pragma once

#include <cstddef>
#include <string_view>

namespace lits {

// Where a key falls relative to a learned node's confirmed common prefix.
enum class PrefixRelation {
    within, // key starts with prefix: the model may be asked for a prediction
    below,  // key sorts before every key that shares the prefix
    above   // key sorts after every key that shares the prefix
};

// Step 1 of routing (docs/interfaces.md). Pure byte comparison; the model is
// never consulted for keys that are not `within`. An empty prefix always
// matches. A key that is a proper prefix of `prefix` is `below`.
PrefixRelation relate_to_prefix(std::string_view prefix, std::string_view key) noexcept;

// Step 4: never trust a prediction to be in range. Clamps to
// [0, slot_count - 1]; throws std::invalid_argument when slot_count is zero
// (a learned node must have at least one slot).
std::size_t bound_slot(std::size_t predicted, std::size_t slot_count);

} // namespace lits
