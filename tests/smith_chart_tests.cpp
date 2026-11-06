/**
 * @file    smith_chart_tests.cpp
 * @brief   Tests of the grid of the Smith chart and the map between impedance and reflection.
 */

#include "plots/smith_chart.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <complex>
#include <string>
#include <vector>

namespace {

using Catch::Matchers::WithinAbs;

const GUI::SmithCurve &FindCurve(const std::vector<GUI::SmithCurve> &grid, const std::string &label) {
    const auto curve = std::ranges::find(grid, label, &GUI::SmithCurve::Label);
    REQUIRE(curve != grid.end());
    return *curve;
}

} // namespace

TEST_CASE("The reflection of an impedance is its distance from the reference", "[smith_chart]") {
    CHECK(std::abs(GUI::ToReflection(1.0)) < 1e-12);
    CHECK_THAT(GUI::ToReflection(0.0).real(), WithinAbs(-1.0, 1e-12));
    CHECK_THAT(GUI::ToReflection(2.0).real(), WithinAbs(1.0 / 3.0, 1e-12));
    // A pure reactance reflects everything, so it lies on the outer circle
    CHECK_THAT(std::abs(GUI::ToReflection({0.0, 3.0})), WithinAbs(1.0, 1e-12));
    // z = 1 + j has the reflection j / (2 + j)
    const std::complex<double> reflection = GUI::ToReflection({1.0, 1.0});
    CHECK_THAT(reflection.real(), WithinAbs(0.2, 1e-12));
    CHECK_THAT(reflection.imag(), WithinAbs(0.4, 1e-12));
}

TEST_CASE("The grid has the outer circle, five resistance circles and ten reactance arcs", "[smith_chart]") {
    const std::vector<GUI::SmithCurve> grid = GUI::BuildSmithGrid(GUI::SMITH_CURVE_SEGMENTS);
    CHECK(grid.size() == 1 + 5 + 10);
    CHECK(std::ranges::count(grid, true, &GUI::SmithCurve::IsResistance) == 5);
    // The outer circle is first and has no label
    CHECK(grid.front().Label.empty());
    for (const std::string &label : {"0.2", "0.5", "1", "2", "5", "+j0.5", "-j0.5", "+j5", "-j0.2"}) {
        FindCurve(grid, label);
    }
}

TEST_CASE("Every curve of the grid lies inside the unit circle", "[smith_chart]") {
    for (const GUI::SmithCurve &curve : GUI::BuildSmithGrid(GUI::SMITH_CURVE_SEGMENTS)) {
        for (const GUI::SmithPoint &point : curve.Points) {
            CHECK(std::hypot(point.X, point.Y) <= 1.0 + 1e-9);
        }
    }
}

TEST_CASE("A resistance circle is the reflection of every impedance of that resistance", "[smith_chart]") {
    const std::vector<GUI::SmithCurve> grid = GUI::BuildSmithGrid(GUI::SMITH_CURVE_SEGMENTS);
    for (const double resistance : GUI::SMITH_GRID_VALUES) {
        const GUI::SmithCurve &circle = FindCurve(grid, GUI::FormatSmithValue(resistance));
        for (const GUI::SmithPoint &point : circle.Points) {
            // The point must be the reflection of resistance + j x for some x: its resistance is the circle's
            const std::complex<double> reflection(point.X, point.Y);
            if (std::abs(1.0 - reflection) < 1e-9) {
                continue;
            }
            const std::complex<double> impedance = (1.0 + reflection) / (1.0 - reflection);
            CHECK_THAT(impedance.real(), WithinAbs(resistance, 1e-6));
        }
        // It crosses the real axis at (r - 1) / (r + 1), where its label sits
        CHECK_THAT(circle.LabelPosition.X, WithinAbs((resistance - 1.0) / (resistance + 1.0), 1e-12));
        CHECK_THAT(circle.LabelPosition.Y, WithinAbs(0.0, 1e-12));
    }
}

TEST_CASE("A reactance arc runs from the outer circle to the open circuit at the same reactance", "[smith_chart]") {
    const std::vector<GUI::SmithCurve> grid = GUI::BuildSmithGrid(GUI::SMITH_CURVE_SEGMENTS);
    const GUI::SmithCurve &arc = FindCurve(grid, "+j2");
    CHECK_FALSE(arc.IsResistance);
    // It starts where the resistance is zero, on the outer circle above the axis, and ends at the open circuit
    CHECK_THAT(std::hypot(arc.Points.front().X, arc.Points.front().Y), WithinAbs(1.0, 1e-12));
    CHECK(arc.Points.front().Y > 0.0);
    CHECK_THAT(arc.Points.back().X, WithinAbs(1.0, 1e-12));
    CHECK_THAT(arc.Points.back().Y, WithinAbs(0.0, 1e-12));
    for (const GUI::SmithPoint &point : arc.Points) {
        const std::complex<double> reflection(point.X, point.Y);
        if (std::abs(1.0 - reflection) < 1e-9) {
            continue;
        }
        const std::complex<double> impedance = (1.0 + reflection) / (1.0 - reflection);
        CHECK_THAT(impedance.imag(), WithinAbs(2.0, 1e-6));
    }
    // The negative arc mirrors it below the axis
    const GUI::SmithCurve &mirror = FindCurve(grid, "-j2");
    CHECK_THAT(mirror.Points[10].Y, WithinAbs(-arc.Points[10].Y, 1e-12));
}

TEST_CASE("A grid with too few segments still has curves", "[smith_chart]") {
    const std::vector<GUI::SmithCurve> grid = GUI::BuildSmithGrid(0);
    CHECK(grid.size() == 16);
    CHECK(grid.front().Points.size() >= 5);
}
