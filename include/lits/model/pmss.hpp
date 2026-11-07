#pragma once

#include <cstddef>

namespace lits {

enum class Structure { compact_leaf, learned_node, hot_subtrie };

// Owner: Opi. Store a read fraction in [0,1]; write fraction is 1-read_fraction.
struct WorkloadMix {
    double read_fraction = 1.0;
};

class Pmss {
public:
    // TODO: calibration-table loading and bounded interpolation.
    // Until calibrated, this method throws; it does not invent latency values.
    Structure choose(std::size_t key_count, double group_gpkl,
                     WorkloadMix workload = {}) const;
};

} // namespace lits
