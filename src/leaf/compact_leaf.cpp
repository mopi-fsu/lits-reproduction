#include "lits/leaf/compact_leaf.hpp"
#include "lits/detail/unimplemented.hpp"

namespace lits {

bool CompactLeaf::insert(std::string_view, Value) {
    detail::unimplemented("CompactLeaf::insert");
}

std::optional<Value> CompactLeaf::lookup(std::string_view) const {
    detail::unimplemented("CompactLeaf::lookup");
}

std::vector<Record> CompactLeaf::scan(std::string_view, std::size_t) const {
    detail::unimplemented("CompactLeaf::scan");
}

} // namespace lits
