#include "lits/core/index.hpp"
#include "lits/detail/unimplemented.hpp"

namespace lits {

void LitsIndex::bulkload(const std::vector<Record>&) {
    detail::unimplemented("LitsIndex::bulkload");
}

std::optional<Value> LitsIndex::lookup(std::string_view) const {
    detail::unimplemented("LitsIndex::lookup");
}

bool LitsIndex::insert(std::string_view, Value) {
    detail::unimplemented("LitsIndex::insert");
}

bool LitsIndex::update(std::string_view, Value) {
    detail::unimplemented("LitsIndex::update");
}

bool LitsIndex::erase(std::string_view) {
    detail::unimplemented("LitsIndex::erase");
}

std::vector<Record> LitsIndex::scan(std::string_view, std::size_t) const {
    detail::unimplemented("LitsIndex::scan");
}

} // namespace lits
