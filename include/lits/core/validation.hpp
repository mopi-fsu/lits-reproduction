#pragma once

#include "lits/types.hpp"

#include <string_view>
#include <vector>

namespace lits {

// Supported key: nonempty, every byte in 1..127 (so no embedded NUL, no high bytes).
bool is_supported_key(std::string_view key) noexcept;

// Throws std::invalid_argument naming `what` and the reason.
void validate_key(std::string_view key, const char* what);

// A scan lower bound may be empty (start of index) but must otherwise be supported.
void validate_scan_bound(std::string_view bound);

// Records must be strictly ascending in byte order (sorted + unique) with
// supported keys. Empty input is valid. The error names the record index.
void validate_bulkload(const std::vector<Record>& sorted_records);

} // namespace lits
