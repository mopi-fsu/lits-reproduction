#include "lits/model/hpt.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {

void check(bool condition, const char* description) {
    if (!condition) throw std::runtime_error(description);
}

void expect_close(double actual, double expected, const char* description) {
    check(std::isfinite(actual) && std::abs(actual - expected) <= 1e-12, description);
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

void fit_and_slot_cases() {
    const auto model = lits::LocalLinearModel::fit(0.25, 0.75);
    expect_close(model.slope, 2.0, "Fit slope from endpoints");
    expect_close(model.intercept, -0.5, "Fit intercept from lower endpoint");
    check(model.predict(0.25, 8) == 0, "Minimum maps to first slot");
    check(model.predict(0.375, 8) == 2, "Quarter of normalized interval");
    check(model.predict(0.5, 8) == 4, "Midpoint maps to middle slot");
    check(model.predict(0.625, 8) == 6, "Three quarters of normalized interval");
    check(model.predict(0.75, 8) == 7, "Maximum maps to last, not one-past-end");
    check(model.predict(0.4, 8) == 2, "Fractional slot positions round downward");
    check(model.predict(0.0, 8) == 0, "Below node interval clamps low");
    check(model.predict(1.0, 8) == 7, "Above node interval clamps high");
    check(model.predict(-100.0, 8) == 0, "Finite extrapolation clamps low");
    check(model.predict(100.0, 8) == 7, "Finite extrapolation clamps high");
    for (double coordinate : {0.0, 0.25, 0.5, 0.75, 1.0}) {
        check(model.predict(coordinate, 1) == 0, "One slot always selects zero");
    }

    const auto identity = lits::LocalLinearModel::fit(0.0, 1.0);
    check(identity.predict(0.125, 8) == 1, "Exact slot boundary");
    check(identity.predict(std::nextafter(0.125, 0.0), 8) == 0,
          "Just below slot boundary");
    check(identity.predict(std::nextafter(0.125, 1.0), 8) == 1,
          "Just above slot boundary");

    std::size_t previous = 0;
    for (int i = 0; i <= 1000; ++i) {
        const auto slot = model.predict(static_cast<double>(i) / 1000.0, 17);
        check(slot >= previous && slot < 17, "Predictions remain ordered and bounded");
        previous = slot;
    }
    const std::size_t max_slots = std::numeric_limits<std::size_t>::max();
    check(identity.predict(1.0, max_slots) == max_slots - 1, "Huge array endpoint");
    check(identity.predict(std::nextafter(1.0, 0.0), max_slots) < max_slots,
          "Huge array interior conversion stays in range");

    const double adjacent = std::nextafter(0.5, 1.0);
    const auto narrow = lits::LocalLinearModel::fit(0.5, adjacent);
    check(narrow.predict(0.5, 8) == 0 && narrow.predict(adjacent, 8) == 7,
          "Narrow representable interval endpoints");
}

void invalid_cases() {
    using Model = lits::LocalLinearModel;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    expect_throw<std::invalid_argument>([] { Model::fit(0.75, 0.25); }, "Reversed bounds");
    expect_throw<std::invalid_argument>([] { Model::fit(-0.1, 0.5); }, "Negative CDF bound");
    expect_throw<std::invalid_argument>([] { Model::fit(0.0, 1.1); }, "CDF bound above one");
    expect_throw<std::invalid_argument>([&] { Model::fit(nan, 0.5); }, "NaN minimum");
    expect_throw<std::invalid_argument>([&] { Model::fit(0.0, nan); }, "NaN maximum");
    expect_throw<std::invalid_argument>([&] { Model::fit(-inf, 0.5); }, "Infinite minimum");
    expect_throw<std::invalid_argument>([&] { Model::fit(0.0, inf); }, "Infinite maximum");
    for (double endpoint : {0.0, 0.5, 1.0}) {
        expect_throw<std::domain_error>([&] { Model::fit(endpoint, endpoint); },
                                        "Equal bounds require fallback");
    }
    expect_throw<std::domain_error>([] {
        Model::fit(0.0, std::numeric_limits<double>::denorm_min());
    }, "Reciprocal overflow requires fallback");

    const Model model;
    expect_throw<std::invalid_argument>([&] { model.predict(0.5, 0); }, "Zero slots");
    expect_throw<std::invalid_argument>([&] { model.predict(nan, 8); }, "NaN prediction");
    expect_throw<std::invalid_argument>([&] { model.predict(inf, 8); }, "Infinite prediction");
    expect_throw<std::invalid_argument>([] { Model{0.0, 0.0}.predict(0.5, 8); }, "Zero slope");
    expect_throw<std::invalid_argument>([] { Model{-1.0, 0.0}.predict(0.5, 8); }, "Negative slope");
    expect_throw<std::invalid_argument>([&] { Model{inf, 0.0}.predict(0.5, 8); }, "Infinite slope");
    expect_throw<std::invalid_argument>([&] { Model{1.0, nan}.predict(0.5, 8); }, "NaN intercept");
    expect_throw<std::domain_error>([] {
        Model{2.0, 0.0}.predict(std::numeric_limits<double>::max(), 8);
    }, "Prediction arithmetic overflow");
}

void hpt_to_slot_cases() {
    lits::Hpt hpt;
    hpt.train({"car", "cat", "dog"});
    const auto model = lits::LocalLinearModel::fit(hpt.cdf("car"), hpt.cdf("dog"));
    check(model.predict(hpt.cdf("car"), 8) == 0, "HPT first key routes low");
    check(model.predict(hpt.cdf("cat"), 8) == 4, "HPT middle key routes to midpoint");
    check(model.predict(hpt.cdf("dog"), 8) == 7, "HPT last key routes high");

    const auto subtree = lits::LocalLinearModel::fit(hpt.cdf("car", 2), hpt.cdf("cat", 2));
    check(subtree.predict(hpt.cdf("car", 2), 8) == 0, "Shared-prefix subtree minimum");
    check(subtree.predict(hpt.cdf("cat", 2), 8) == 7, "Shared-prefix subtree maximum");

    hpt.train({"a", "ab"});
    expect_throw<std::domain_error>([&] {
        lits::LocalLinearModel::fit(hpt.cdf("a"), hpt.cdf("ab"));
    }, "Tied HPT endpoints request fallback rather than divide by zero");
}

} // namespace

int main() {
    try {
        fit_and_slot_cases();
        invalid_cases();
        hpt_to_slot_cases();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "Local model normalization, slot bounds, and HPT composition checks passed.\n";
    return 0;
}
