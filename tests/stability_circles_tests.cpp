/**
 * @file    stability_circles_tests.cpp
 * @brief   Tests of the source and load stability circles: their centers and radii against the closed forms, the side
 *          that is stable against the reflection coefficient worked out directly, the degenerate shapes, and the
 *          samples and hatching that draw them.
 */

#include "results/stability_circles.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

namespace {

using Catch::Matchers::WithinAbs;
using Complex = std::complex<double>;

Complex Polar(const double magnitude, const double degrees) {
    return std::polar(magnitude, degrees * std::numbers::pi / 180.0);
}

GUI::TwoPortMatrix MakePotentiallyUnstable() {
    return {.S11 = Polar(0.75, -115.0), .S12 = Polar(0.06, 60.0), .S21 = Polar(3.2, 70.0), .S22 = Polar(0.45, -40.0)};
}

/// The reflection the other port shows for a termination, which is what makes a termination unstable above 1.
double ReflectionSeen(const GUI::TwoPortMatrix &matrix, const bool is_load, const Complex termination) {
    const Complex near_side = is_load ? matrix.S11 : matrix.S22;
    const Complex far_side = is_load ? matrix.S22 : matrix.S11;
    return std::abs(near_side + matrix.S12 * matrix.S21 * termination / (1.0 - far_side * termination));
}

/// Walks a grid of points of the chart, leaves out those that sit on the boundary, and returns the unstable ones by the
/// direct formula.
std::vector<Complex> FindGridPoints(const GUI::TwoPortMatrix &matrix, const bool is_load, const bool is_unstable) {
    std::vector<Complex> points;
    constexpr int STEPS = 40;
    for (int row = -STEPS; row <= STEPS; ++row) {
        for (int column = -STEPS; column <= STEPS; ++column) {
            const Complex point(static_cast<double>(column) / STEPS, static_cast<double>(row) / STEPS);
            if (std::abs(point) >= 1.0) {
                continue;
            }
            const double seen = ReflectionSeen(matrix, is_load, point);
            if (std::abs(seen - 1.0) < 1e-2) {
                continue;
            }
            if ((seen > 1.0) == is_unstable) {
                points.push_back(point);
            }
        }
    }
    return points;
}

/// Whether the boundary says a point is on its unstable side.
bool IsUnstableByBoundary(const GUI::StabilityBoundary &boundary, const Complex point) {
    switch (boundary.Shape) {
    case GUI::BoundaryShape::CIRCLE:
        return (std::abs(point - boundary.Center) < boundary.Radius) != boundary.IsStableInside;
    case GUI::BoundaryShape::LINE:
        return std::real(std::conj(boundary.StableNormal) * (point - boundary.Center)) < 0.0;
    case GUI::BoundaryShape::NONE:
    default:
        return boundary.Coverage == GUI::ChartCoverage::ENTIRELY_UNSTABLE;
    }
}

void CheckSidesMatchTheDirectReflection(const GUI::TwoPortMatrix &matrix) {
    const GUI::StabilityCircles circles = GUI::ComputeStabilityCircles(matrix);
    for (const bool is_load : {true, false}) {
        const GUI::StabilityBoundary &boundary = is_load ? circles.Load : circles.Source;
        for (const Complex point : FindGridPoints(matrix, is_load, true)) {
            INFO((is_load ? "load " : "source ") << point);
            CHECK(IsUnstableByBoundary(boundary, point));
        }
        for (const Complex point : FindGridPoints(matrix, is_load, false)) {
            INFO((is_load ? "load " : "source ") << point);
            CHECK_FALSE(IsUnstableByBoundary(boundary, point));
        }
    }
}

} // namespace

TEST_CASE("The circles of a transistor have the centers and radii of the closed forms", "[stability_circles]") {
    const GUI::StabilityCircles circles = GUI::ComputeStabilityCircles(MakePotentiallyUnstable());
    // Worked out with an independent script from the formulas of Gonzalez, section 3.3
    REQUIRE(circles.Source.Shape == GUI::BoundaryShape::CIRCLE);
    CHECK_THAT(circles.Source.Center.real(), WithinAbs(-0.75878, 1e-4));
    CHECK_THAT(circles.Source.Center.imag(), WithinAbs(1.18373, 1e-4));
    CHECK_THAT(circles.Source.Radius, WithinAbs(0.43120, 1e-4));
    REQUIRE(circles.Load.Shape == GUI::BoundaryShape::CIRCLE);
    CHECK_THAT(circles.Load.Center.real(), WithinAbs(1.05494, 1e-4));
    CHECK_THAT(circles.Load.Center.imag(), WithinAbs(3.01453, 1e-4));
    CHECK_THAT(circles.Load.Radius, WithinAbs(2.25160, 1e-4));
    // |S11| and |S22| are below 1, so the origin is stable, and it lies outside both circles
    CHECK_FALSE(circles.Source.IsStableInside);
    CHECK_FALSE(circles.Load.IsStableInside);
    CHECK(circles.Source.Coverage == GUI::ChartCoverage::PARTLY);
    CHECK(circles.Load.Coverage == GUI::ChartCoverage::PARTLY);
}

TEST_CASE("The stable side of each circle agrees with the reflection worked out for terminations on the chart",
          "[stability_circles]") {
    CheckSidesMatchTheDirectReflection(MakePotentiallyUnstable());
    // Stable terminations inside the circle: a source with |S22| above 1 makes the origin unstable
    CheckSidesMatchTheDirectReflection(
        {.S11 = Polar(1.4, 30.0), .S12 = Polar(0.5, 10.0), .S21 = Polar(1.0, 80.0), .S22 = Polar(0.5, -20.0)});
    CheckSidesMatchTheDirectReflection(
        {.S11 = Polar(0.5, 30.0), .S12 = Polar(0.5, 10.0), .S21 = Polar(1.0, 80.0), .S22 = Polar(1.3, -20.0)});
}

TEST_CASE("A circle that is a line has the side of the stable terminations told by a normal", "[stability_circles]") {
    // |Delta| = |0.4 - 0.9| = |S22|: the coefficient of the load circle vanishes
    const GUI::TwoPortMatrix matrix{.S11 = 0.8, .S12 = 0.9, .S21 = 1.0, .S22 = 0.5};
    const GUI::StabilityCircles circles = GUI::ComputeStabilityCircles(matrix);
    CHECK(circles.Load.Shape == GUI::BoundaryShape::LINE);
    CHECK(circles.Source.Shape == GUI::BoundaryShape::CIRCLE);
    CHECK_THAT(std::abs(circles.Load.StableNormal), WithinAbs(1.0, 1e-12));
    CheckSidesMatchTheDirectReflection(matrix);
}

TEST_CASE("A circle around the whole chart leaves it stable on one side only, whichever side that is",
          "[stability_circles]") {
    // A pad has no reflections: its circles are of radius 4 around the origin, which encloses the chart
    const GUI::TwoPortMatrix pad{.S11 = 0.0, .S12 = 0.5, .S21 = 0.5, .S22 = 0.0};
    const GUI::StabilityCircles circles = GUI::ComputeStabilityCircles(pad);
    CHECK(circles.Load.Shape == GUI::BoundaryShape::CIRCLE);
    CHECK_THAT(circles.Load.Radius, WithinAbs(4.0, 1e-9));
    CHECK(circles.Load.IsStableInside);
    CHECK(circles.Load.Coverage == GUI::ChartCoverage::ENTIRELY_STABLE);
    CHECK(circles.Source.Coverage == GUI::ChartCoverage::ENTIRELY_STABLE);
    CheckSidesMatchTheDirectReflection(pad);
}

TEST_CASE("A circle that covers the whole chart on the unstable side leaves it entirely unstable",
          "[stability_circles]") {
    // S11 above 1 with a weak feedback: whatever the load, the source sees a reflection above 1
    const GUI::TwoPortMatrix matrix{.S11 = 1.5, .S12 = 0.1, .S21 = 1.0, .S22 = 0.1};
    const GUI::StabilityCircles circles = GUI::ComputeStabilityCircles(matrix);
    CHECK(circles.Load.Coverage == GUI::ChartCoverage::ENTIRELY_UNSTABLE);
    CheckSidesMatchTheDirectReflection(matrix);
}

TEST_CASE("A circle that lies away from the chart on the unstable side leaves it entirely stable",
          "[stability_circles]") {
    // S11 well below 1 with a weak feedback: the circle of unstable loads sits around the pole, far from the chart
    const GUI::TwoPortMatrix matrix{.S11 = 0.5, .S12 = 0.1, .S21 = 1.0, .S22 = 0.1};
    const GUI::StabilityCircles circles = GUI::ComputeStabilityCircles(matrix);
    CHECK(circles.Load.Coverage == GUI::ChartCoverage::ENTIRELY_STABLE);
    CheckSidesMatchTheDirectReflection(matrix);
}

TEST_CASE("A unilateral two-port has no circle and the whole chart is stable when its reflection is below 1",
          "[stability_circles]") {
    const GUI::StabilityCircles stable = GUI::ComputeStabilityCircles({.S11 = 0.5, .S12 = 0.0, .S21 = 3.0, .S22 = 0.4});
    CHECK(stable.Load.Shape == GUI::BoundaryShape::NONE);
    CHECK(stable.Load.Coverage == GUI::ChartCoverage::ENTIRELY_STABLE);
    CHECK(stable.Source.Coverage == GUI::ChartCoverage::ENTIRELY_STABLE);
    // The load plane depends on |S11| and the source plane on |S22|
    const GUI::StabilityCircles unstable =
        GUI::ComputeStabilityCircles({.S11 = 1.2, .S12 = 0.0, .S21 = 3.0, .S22 = 0.4});
    CHECK(unstable.Load.Coverage == GUI::ChartCoverage::ENTIRELY_UNSTABLE);
    CHECK(unstable.Source.Coverage == GUI::ChartCoverage::ENTIRELY_STABLE);
    CHECK(GUI::SampleBoundary(unstable.Load).empty());
    CHECK(GUI::BuildUnstableHatch(stable.Load).empty());
}

TEST_CASE("The samples of a circle lie on it and within the view", "[stability_circles]") {
    const GUI::StabilityCircles circles = GUI::ComputeStabilityCircles(MakePotentiallyUnstable());
    for (const GUI::StabilityBoundary &boundary : {circles.Source, circles.Load}) {
        const std::vector<std::vector<Complex>> pieces = GUI::SampleBoundary(boundary);
        REQUIRE(pieces.size() == 1);
        REQUIRE(pieces.front().size() > 10);
        for (const Complex point : pieces.front()) {
            CHECK_THAT(std::abs(point - boundary.Center), WithinAbs(boundary.Radius, 1e-9));
            CHECK(std::abs(point) <= GUI::BOUNDARY_VIEW_RADIUS + 1e-9);
        }
    }
}

TEST_CASE("A circle wholly outside the view has no samples, and one that encloses it is cut at its edge",
          "[stability_circles]") {
    GUI::StabilityBoundary far_away{.Shape = GUI::BoundaryShape::CIRCLE, .Center = Complex(10.0, 0.0), .Radius = 1.0};
    CHECK(GUI::SampleBoundary(far_away).empty());
    // A circle of radius 1000 whose edge passes the origin at a distance of 1: a nearly straight arc
    GUI::StabilityBoundary huge{.Shape = GUI::BoundaryShape::CIRCLE, .Center = Complex(1001.0, 0.0), .Radius = 1000.0};
    const std::vector<std::vector<Complex>> pieces = GUI::SampleBoundary(huge);
    REQUIRE(pieces.size() == 1);
    for (const Complex point : pieces.front()) {
        CHECK_THAT(std::abs(point - huge.Center), WithinAbs(1000.0, 1e-6));
        CHECK(std::abs(point) <= GUI::BOUNDARY_VIEW_RADIUS + 1e-9);
    }
}

TEST_CASE("The samples of a line are its two ends at the edge of the view", "[stability_circles]") {
    const GUI::TwoPortMatrix matrix{.S11 = 0.8, .S12 = 0.9, .S21 = 1.0, .S22 = 0.5};
    const GUI::StabilityCircles circles = GUI::ComputeStabilityCircles(matrix);
    const std::vector<std::vector<Complex>> pieces = GUI::SampleBoundary(circles.Load);
    REQUIRE(pieces.size() == 1);
    REQUIRE(pieces.front().size() == 2);
    for (const Complex point : pieces.front()) {
        CHECK_THAT(std::abs(point), WithinAbs(GUI::BOUNDARY_VIEW_RADIUS, 1e-9));
        CHECK_THAT(std::real(std::conj(circles.Load.StableNormal) * (point - circles.Load.Center)),
                   WithinAbs(0.0, 1e-9));
    }
}

TEST_CASE("The hatching covers only unstable terminations of the chart", "[stability_circles]") {
    const std::vector<GUI::TwoPortMatrix> matrices = {
        MakePotentiallyUnstable(),
        {.S11 = Polar(1.4, 30.0), .S12 = Polar(0.5, 10.0), .S21 = Polar(1.0, 80.0), .S22 = Polar(0.5, -20.0)},
        {.S11 = 0.8, .S12 = 0.9, .S21 = 1.0, .S22 = 0.5}};
    for (const GUI::TwoPortMatrix &matrix : matrices) {
        const GUI::StabilityCircles circles = GUI::ComputeStabilityCircles(matrix);
        for (const bool is_load : {true, false}) {
            const GUI::StabilityBoundary &boundary = is_load ? circles.Load : circles.Source;
            const std::vector<GUI::HatchSegment> strokes = GUI::BuildUnstableHatch(boundary);
            INFO((is_load ? "load " : "source ")
                 << static_cast<int>(boundary.Coverage) << " shape " << static_cast<int>(boundary.Shape));
            CHECK_FALSE(strokes.empty());
            for (const GUI::HatchSegment &stroke : strokes) {
                CHECK(std::abs(stroke.From) <= 1.0 + 1e-9);
                CHECK(std::abs(stroke.To) <= 1.0 + 1e-9);
                // Three points along the stroke, leaving out the ends, which may sit on the boundary
                for (const double share : {0.25, 0.5, 0.75}) {
                    const Complex point = stroke.From + share * (stroke.To - stroke.From);
                    const double seen = ReflectionSeen(matrix, is_load, point);
                    if (std::abs(seen - 1.0) > 1e-6) {
                        CHECK(seen > 1.0);
                    }
                }
            }
        }
    }
}

TEST_CASE("A chart that is entirely stable has no hatching, and the lines are 45 degrees and spaced",
          "[stability_circles]") {
    const GUI::TwoPortMatrix pad{.S11 = 0.0, .S12 = 0.5, .S21 = 0.5, .S22 = 0.0};
    CHECK(GUI::BuildUnstableHatch(GUI::ComputeStabilityCircles(pad).Load).empty());
    const std::vector<GUI::HatchSegment> strokes =
        GUI::BuildUnstableHatch(GUI::ComputeStabilityCircles(MakePotentiallyUnstable()).Source);
    REQUIRE_FALSE(strokes.empty());
    for (const GUI::HatchSegment &stroke : strokes) {
        const Complex direction = stroke.To - stroke.From;
        CHECK_THAT(std::abs(direction.real() - direction.imag()), WithinAbs(0.0, 1e-9));
    }
}
