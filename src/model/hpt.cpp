#include "lits/model/hpt.hpp"
#include "lits/detail/unimplemented.hpp"

namespace lits {

void Hpt::train(const std::vector<std::string_view>&) {
    detail::unimplemented("Hpt::train");
}

double Hpt::cdf(std::string_view, std::size_t) const {
    detail::unimplemented("Hpt::cdf");
}

LocalLinearModel LocalLinearModel::fit(double, double) {
    detail::unimplemented("LocalLinearModel::fit");
}

std::size_t LocalLinearModel::predict(double, std::size_t) const {
    detail::unimplemented("LocalLinearModel::predict");
}

} // namespace lits
