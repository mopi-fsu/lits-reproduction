#include "lits/core/validation.hpp"

#include <stdexcept>
#include <string>

namespace lits {

namespace {
// Returns nullptr when supported, else a static reason string.
const char* key_problem(std::string_view key) noexcept {
    if (key.empty()) return "key is empty";
    for (char c : key) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte == 0) return "key contains a NUL byte";
        if (byte > 127) return "key contains a byte above 127";
    }
    return nullptr;
}
} // namespace

bool is_supported_key(std::string_view key) noexcept { return key_problem(key) == nullptr; }

void validate_key(std::string_view key, const char* what) {
    if (const char* problem = key_problem(key)) {
        throw std::invalid_argument(std::string(what) + ": " + problem);
    }
}

void validate_scan_bound(std::string_view bound) {
    if (!bound.empty()) validate_key(bound, "scan lower bound");
}

void validate_bulkload(const std::vector<Record>& records) {
    for (std::size_t i = 0; i < records.size(); ++i) {
        const std::string where = "bulkload record " + std::to_string(i) + ": ";
        if (const char* problem = key_problem(records[i].key)) {
            throw std::invalid_argument(where + problem);
        }
        if (i == 0) continue;
        // std::string_view compares bytes as unsigned char: lexicographic byte order.
        const std::string_view prev = records[i - 1].key;
        const std::string_view cur = records[i].key;
        if (cur == prev) throw std::invalid_argument(where + "duplicate key");
        if (cur < prev) {
            throw std::invalid_argument(where + "keys are not sorted ascending");
        }
    }
}

} // namespace lits
