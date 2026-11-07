#include "lits/model/pmss.hpp"
#include "lits/detail/unimplemented.hpp"

namespace lits {

Structure Pmss::choose(std::size_t, double, WorkloadMix) const {
    detail::unimplemented("Pmss::choose");
}

} // namespace lits
