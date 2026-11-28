#include "lits/model/pmss.hpp"
#include "lits/types.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

namespace lits {
namespace {

void validate_axis(const std::vector<double>& axis, const char* name) {
    if (axis.empty()) {
        throw std::invalid_argument(std::string(name) + " axis must be nonempty");
    }
    for (std::size_t i = 0; i < axis.size(); ++i) {
        if (!std::isfinite(axis[i]) || axis[i] < 0 ||
            (i > 0 && axis[i] <= axis[i - 1])) {
            throw std::invalid_argument(std::string(name) +
                " axis must be finite, nonnegative, and strictly increasing");
        }
    }
}

void validate_table(const Table& table, std::size_t rows, std::size_t columns,
                    const char* name) {
    if (table.size() != rows) {
        throw std::invalid_argument(std::string(name) + " row count must match GPKL axis");
    }
    for (const auto& row : table) {
        if (row.size() != columns) {
            throw std::invalid_argument(std::string(name) +
                                        " column count must match log2(size) axis");
        }
        for (double latency : row) {
            if (!std::isfinite(latency) || latency <= 0) {
                throw std::invalid_argument(std::string(name) +
                                            " latencies must be finite and positive");
            }
        }
    }
}

struct Bracket {
    std::size_t low;
    std::size_t high;
    double fraction;
};

Bracket bracket(const std::vector<double>& axis, double value) {
    if (value <= axis.front()) return {0, 0, 0};
    if (value >= axis.back()) return {axis.size() - 1, axis.size() - 1, 0};

    const auto upper = std::upper_bound(axis.begin(), axis.end(), value);
    const std::size_t high = static_cast<std::size_t>(upper - axis.begin());
    const std::size_t low = high - 1;
    const double fraction = (value - axis[low]) / (axis[high] - axis[low]);
    return {low, high, fraction};
}

// A convex combination. Clamp roundoff to the input range, including possible
// overflow near the largest finite double. All inputs here are finite/positive.
double blend(double low, double high, double fraction) {
    if (fraction <= 0) return low;
    if (fraction >= 1) return high;
    return std::clamp((1.0 - fraction) * low + fraction * high,
                      std::min(low, high), std::max(low, high));
}

} // namespace

Pmss::Pmss(Calibration calibration) {
    validate_axis(calibration.gpkl_axis, "GPKL");
    validate_axis(calibration.log2_size_axis, "log2(size)");
    const auto rows = calibration.gpkl_axis.size();
    const auto columns = calibration.log2_size_axis.size();
    validate_table(calibration.lit_read, rows, columns, "LIT read");
    validate_table(calibration.lit_insert, rows, columns, "LIT insert");
    validate_table(calibration.hot_read, rows, columns, "HOT read");
    validate_table(calibration.hot_insert, rows, columns, "HOT insert");
    calibration_ = std::move(calibration);
}

double Pmss::interpolate(const Table& table, double gpkl, double log2_size) const {
    const auto rows = bracket(calibration_.gpkl_axis, gpkl);
    const auto columns = bracket(calibration_.log2_size_axis, log2_size);

    // Interpolate across size in each GPKL row, then between those rows.
    const double low = blend(table[rows.low][columns.low],
                             table[rows.low][columns.high], columns.fraction);
    const double high = blend(table[rows.high][columns.low],
                              table[rows.high][columns.high], columns.fraction);
    return blend(low, high, rows.fraction);
}

Structure Pmss::choose(std::size_t key_count, double group_gpkl,
                       WorkloadMix workload) const {
    if (key_count == 0 || !std::isfinite(group_gpkl) || group_gpkl < 0 ||
        !std::isfinite(workload.read_fraction) || workload.read_fraction < 0 ||
        workload.read_fraction > 1) {
        throw std::invalid_argument("PMSS needs positive key count, nonnegative GPKL, "
                                    "and a read fraction in [0,1]");
    }
    if (calibration_.gpkl_axis.empty() || calibration_.log2_size_axis.empty()) {
        throw std::logic_error("PMSS calibration is unavailable on a moved-from object");
    }
    if (key_count <= compact_leaf_capacity) return Structure::compact_leaf;

    const double size = std::log2(static_cast<double>(key_count));
    const double lit_read = interpolate(calibration_.lit_read, group_gpkl, size);
    const double lit_insert = interpolate(calibration_.lit_insert, group_gpkl, size);
    const double hot_read = interpolate(calibration_.hot_read, group_gpkl, size);
    const double hot_insert = interpolate(calibration_.hot_insert, group_gpkl, size);

    // Equation 5: fr*read_latency + (1-fr)*insert_latency.
    const double lit_latency = blend(lit_insert, lit_read, workload.read_fraction);
    const double hot_latency = blend(hot_insert, hot_read, workload.read_fraction);
    return lit_latency <= hot_latency ? Structure::learned_node : Structure::hot_subtrie;
}

} // namespace lits
