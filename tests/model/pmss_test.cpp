#include "lits/model/pmss.hpp"
#include "lits/types.hpp"

#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

using lits::Calibration;
using lits::Pmss;
using lits::Structure;
using lits::Table;

// All latency values below are artificial test fixtures, NOT measurements.
Table constant_table(std::size_t rows, std::size_t columns, double value) {
    return Table(rows, std::vector<double>(columns, value));
}

Calibration fixture() {
    return {{3, 5}, {5, 7}, {{100, 100}, {100, 100}},
            {{300, 300}, {300, 300}}, {{200, 200}, {200, 200}},
            {{100, 100}, {100, 100}}};
}

void check(bool condition, const char* description) {
    if (!condition) throw std::runtime_error(description);
}

template <class Exception, class Function>
void expect_throw(Function function, const char* description) {
    try {
        function();
    } catch (const Exception&) {
        return;
    }
    throw std::runtime_error(description);
}

// Keep interpolation private. Compare its result against constants just below
// and above a hand-computed value; both structure choices must be correct.
void expect_interpolation(const std::vector<double>& gpkl_axis,
                          const std::vector<double>& size_axis, const Table& grid,
                          std::size_t key_count, double gpkl, double expected) {
    for (double offset : {-0.25, 0.25}) {
        const auto hot = constant_table(gpkl_axis.size(), size_axis.size(), expected + offset);
        const Pmss model({gpkl_axis, size_axis, grid, grid, hot, hot});
        const Structure wanted = offset < 0 ? Structure::hot_subtrie : Structure::learned_node;
        check(model.choose(key_count, gpkl, {1}) == wanted, "Read-grid interpolation");
        check(model.choose(key_count, gpkl, {0}) == wanted, "Insert-grid interpolation");
    }
}

void interpolation_cases() {
    const std::vector<double> gpkl = {3, 5};
    const std::vector<double> sizes = {5, 7};
    const Table grid = {{10, 30}, {50, 90}};
    expect_interpolation(gpkl, sizes, grid, 32, 3, 10);
    expect_interpolation(gpkl, sizes, grid, 128, 3, 30);
    expect_interpolation(gpkl, sizes, grid, 32, 5, 50);
    expect_interpolation(gpkl, sizes, grid, 128, 5, 90);
    expect_interpolation(gpkl, sizes, grid, 64, 3, 20);
    expect_interpolation(gpkl, sizes, grid, 64, 5, 70);
    expect_interpolation(gpkl, sizes, grid, 32, 4, 30);
    expect_interpolation(gpkl, sizes, grid, 128, 4, 60);
    // log2(64)=6: midpoint in size, midpoint in GPKL => (10+30+50+90)/4=45.
    expect_interpolation(gpkl, sizes, grid, 64, 4, 45);

    expect_interpolation(gpkl, sizes, grid, 17, 0, 10); // Below both axes.
    expect_interpolation(gpkl, sizes, grid, 1024, 100, 90); // Above both axes.
    expect_interpolation(gpkl, sizes, grid, 64, 0, 20);
    expect_interpolation(gpkl, sizes, grid, 64, 100, 70);
    expect_interpolation(gpkl, sizes, grid, std::numeric_limits<std::size_t>::max(), 3, 30);

    expect_interpolation({1, 3, 9}, {5, 6, 10},
                         {{10, 20, 100}, {30, 40, 120}, {90, 100, 180}},
                         256, 6, 110); // Nonuniform axes: both fractions are 1/2.
    expect_interpolation({3}, sizes, {{10, 30}}, 64, 100, 20);
    expect_interpolation(gpkl, {6}, {{10}, {50}}, 64, 4, 30);
    expect_interpolation({3}, {5}, {{17}}, 1000, 100, 17);
    expect_interpolation(gpkl, {5, 6, 7}, {{10, 20, 30}, {50, 70, 90}}, 64, 4, 45);
}

void decision_cases() {
    const Pmss model(fixture());
    // LIT read/insert = 100/300, HOT = 200/100.
    check(model.choose(64, 4) == Structure::learned_node, "Default read-only workload");
    check(model.choose(64, 4, {0}) == Structure::hot_subtrie, "Insert-only workload");
    check(model.choose(64, 4, {0.25}) == Structure::hot_subtrie, "250 vs 125 latency");
    check(model.choose(64, 4, {0.9}) == Structure::learned_node, "120 vs 190 latency");
    check(model.choose(1, 0) == Structure::compact_leaf, "Single-key group");
    check(model.choose(lits::compact_leaf_capacity, 4, {0}) == Structure::compact_leaf,
          "Capacity boundary uses compact leaf");
    check(model.choose(lits::compact_leaf_capacity + 1, 4, {0}) == Structure::hot_subtrie,
          "Above capacity uses performance model");

    auto tied = fixture();
    tied.lit_insert = tied.lit_read = tied.hot_insert = tied.hot_read = constant_table(2, 2, 100);
    check(Pmss(tied).choose(64, 4, {0.5}) == Structure::learned_node, "Ties prefer LIT");

    const double maximum = std::numeric_limits<double>::max();
    tied.lit_insert = tied.lit_read = tied.hot_insert = tied.hot_read =
        constant_table(2, 2, maximum);
    check(Pmss(tied).choose(64, 4, {0.1}) == Structure::learned_node,
          "Large finite latencies preserve tied decision");

    auto source = fixture();
    Pmss owned(source);
    source.lit_read = constant_table(2, 2, 10000);
    check(owned.choose(64, 4) == Structure::learned_node, "Calibration is owned");
    Pmss moved(std::move(owned));
    check(moved.choose(64, 4) == Structure::learned_node, "Move preserves calibration");
    expect_throw<std::logic_error>([&] { owned.choose(64, 4); }, "Moved-from model");
}

void invalid_calibration_cases() {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    const std::vector<std::vector<double>> bad_axes = {
        {}, {3, 3}, {5, 3}, {-1, 3}, {3, nan}, {3, inf}};
    for (const auto& axis : bad_axes) {
        auto sample = fixture();
        sample.gpkl_axis = axis;
        expect_throw<std::invalid_argument>([&] { Pmss model(sample); }, "Invalid GPKL axis");
        sample = fixture();
        sample.log2_size_axis = axis;
        expect_throw<std::invalid_argument>([&] { Pmss model(sample); }, "Invalid size axis");
    }

    for (int which = 0; which < 4; ++which) {
        const std::vector<Table> bad_tables = {
            {}, {{1, 2}}, {{1, 2}, {3}}, {{1, 2, 3}, {4, 5, 6}},
            {{0, 1}, {1, 1}}, {{-1, 1}, {1, 1}}, {{nan, 1}, {1, 1}}, {{inf, 1}, {1, 1}}};
        for (const auto& bad : bad_tables) {
            auto sample = fixture();
            Table* tables[] = {&sample.lit_read, &sample.lit_insert,
                               &sample.hot_read, &sample.hot_insert};
            *tables[which] = bad;
            expect_throw<std::invalid_argument>([&] { Pmss model(sample); },
                                                "Invalid latency table");
        }
    }
}

void invalid_query_cases() {
    const Pmss model(fixture());
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    expect_throw<std::invalid_argument>([&] { model.choose(0, 4); }, "Zero keys");
    for (double gpkl : {-1.0, nan, inf}) {
        expect_throw<std::invalid_argument>([&] { model.choose(64, gpkl); }, "Invalid GPKL");
        expect_throw<std::invalid_argument>([&] { model.choose(1, gpkl); },
                                            "Small groups still validate GPKL");
    }
    for (double fraction : {-0.1, 1.1, nan, inf}) {
        expect_throw<std::invalid_argument>([&] { model.choose(64, 4, {fraction}); },
                                            "Invalid workload fraction");
        expect_throw<std::invalid_argument>([&] { model.choose(1, 4, {fraction}); },
                                            "Small groups still validate workload");
    }
}

} // namespace

int main() {
    try {
        interpolation_cases();
        decision_cases();
        invalid_calibration_cases();
        invalid_query_cases();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "PMSS interpolation, Equation 5 decisions, and validation checks passed.\n";
    return 0;
}
