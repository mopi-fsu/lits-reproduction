#pragma once

#include <cstddef>
#include <vector>

namespace lits {

enum class Structure { compact_leaf, learned_node, hot_subtrie };

// Owner: Opi. Store a read fraction in [0,1]; write fraction is 1-read_fraction.
struct WorkloadMix {
    double read_fraction = 1.0;
};

using Table = std::vector<std::vector<double>>;

// Every latency table uses [gpkl_index][size_index], with consistent units
// (e.g. nanoseconds). Size coordinates are log2(key_count), not raw counts.
struct Calibration {
    std::vector<double> gpkl_axis;
    std::vector<double> log2_size_axis;

    Table lit_read;
    Table lit_insert;
    Table hot_read;
    Table hot_insert;
};

class Pmss {
public:
    // Own the supplied calibration. Axes must be nonempty, finite, nonnegative,
    // and strictly increasing. Tables must match both axes and contain finite,
    // positive latencies in consistent units. Invalid data throws invalid_argument.
    explicit Pmss(Calibration calibration);

    // Validate positive key_count, finite/nonnegative GPKL, and read fraction in
    // [0,1]. Small groups use compact leaves. Otherwise apply Equation 5 to the
    // interpolated LIT/HOT latencies; ties prefer learned_node. Invalid input
    // throws invalid_argument; a moved-from object throws logic_error.
    Structure choose(std::size_t key_count, double group_gpkl,
                     WorkloadMix workload = {}) const;

private:
    Calibration calibration_;

    // Bilinear interpolation; clamp to axis boundaries rather than extrapolate.
    // One-entry axes are supported and reduce to one-dimensional/constant lookup.
    double interpolate(const Table& table, double gpkl, double log2_size) const;
};

} // namespace lits
