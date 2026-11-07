#include "lits/model/gpkl.hpp"
#include "lits/detail/unimplemented.hpp"

namespace lits {

std::size_t common_prefix_length(std::string_view, std::string_view) {
    detail::unimplemented("common_prefix_length");
}

double gpkl(const std::vector<std::string_view>&) {
    detail::unimplemented("gpkl");
}

double local_gpkl(const std::vector<std::string_view>&, std::size_t) {
    detail::unimplemented("local_gpkl");
}

} // namespace lits
