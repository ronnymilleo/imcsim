/**
 * @file    pole_zero_view_tests.cpp
 * @brief   Tests of what the Output window shows of a pole-zero result: the table rows, the s-plane points and the
 *          frequencies that mark a Bode plot.
 */

#include "results/pole_zero_view.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <string>

namespace {

Core::PoleZeroRoot MakeReal(const bool is_pole, const double real, const double frequency_hertz) {
    return {.IsPole = is_pole,
            .Real = real,
            .Imaginary = 0.0,
            .NaturalFrequencyHertz = frequency_hertz,
            .QualityFactor = std::nullopt,
            .DampingRatio = std::nullopt,
            .IsRightHalfPlane = real > 0.0};
}

Core::PoleZeroRoot MakePair(const bool is_pole, const double real, const double imaginary, const double frequency_hertz,
                            const double quality) {
    return {.IsPole = is_pole,
            .Real = real,
            .Imaginary = imaginary,
            .NaturalFrequencyHertz = frequency_hertz,
            .QualityFactor = quality,
            .DampingRatio = 1.0 / (2.0 * quality),
            .IsRightHalfPlane = real > 0.0};
}

} // namespace

TEST_CASE("The table has one row per root, with a pair on one row", "[pole_zero_view]") {
    Core::PoleZero result;
    result.Roots = {MakePair(true, -4500.0, 5000.0, 1070.0, 0.74), MakeReal(true, -6e6, 9.5e5),
                    MakeReal(false, 0.0, 0.0)};
    const std::vector<GUI::PoleZeroRow> rows = GUI::MakePoleZeroRows(result);

    REQUIRE(rows.size() == 3);
    CHECK(rows[0].Kind == "pole");
    CHECK(rows[0].Real == "-4.5k");
    CHECK(rows[0].Imaginary == "+/-5k");
    CHECK(rows[0].Frequency == "1.07kHz");
    CHECK(rows[0].Quality == "0.74");
    CHECK(rows[0].Damping == "0.676");
    // A real root has a dash where a pair has its quality factor
    CHECK(rows[1].Imaginary == "0");
    CHECK(rows[1].Quality == "-");
    CHECK(rows[1].Damping == "-");
    CHECK(rows[2].Kind == "zero");
}

TEST_CASE("A pair on the imaginary axis reads an infinite quality factor, and a right half-plane root is flagged",
          "[pole_zero_view]") {
    Core::PoleZero result;
    Core::PoleZeroRoot oscillator = MakePair(true, 0.0, 1e3, 159.0, 1.0);
    oscillator.QualityFactor.reset();
    oscillator.DampingRatio = 0.0;
    result.Roots = {oscillator, MakeReal(true, 2e3, 318.0)};
    const std::vector<GUI::PoleZeroRow> rows = GUI::MakePoleZeroRows(result);
    CHECK(rows[0].Quality == "inf");
    CHECK_FALSE(rows[0].IsRightHalfPlane);
    CHECK(rows[1].IsRightHalfPlane);
}

TEST_CASE("The points of the s-plane hold both members of a pair, split by kind and half-plane", "[pole_zero_view]") {
    Core::PoleZero result;
    result.Roots = {MakePair(true, -4500.0, 5000.0, 1070.0, 0.74), MakeReal(true, -6e6, 9.5e5),
                    MakeReal(true, 3e3, 480.0), MakeReal(false, -1e5, 1.6e4)};

    const GUI::PoleZeroPoints poles = GUI::CollectPoleZeroPoints(result, true, false);
    REQUIRE(poles.Real.size() == 3);
    CHECK(poles.Real == std::vector<double>{-4500.0, -4500.0, -6e6});
    CHECK(poles.Imaginary == std::vector<double>{5000.0, -5000.0, 0.0});
    CHECK(GUI::CollectPoleZeroPoints(result, true, true).Real == std::vector<double>{3e3});
    CHECK(GUI::CollectPoleZeroPoints(result, false, false).Real == std::vector<double>{-1e5});
    CHECK(GUI::CollectPoleZeroPoints(result, false, true).Real.empty());
}

TEST_CASE("The Bode plot marks the natural frequency of each root, but not one at 0 Hz", "[pole_zero_view]") {
    Core::PoleZero result;
    result.Roots = {MakeReal(true, -1e4, 1591.5), MakePair(true, -4500.0, 5000.0, 1070.0, 0.74),
                    MakeReal(false, 0.0, 0.0), MakeReal(false, -1e5, 1.6e4)};
    CHECK(GUI::ListRootFrequencies(result, true, false) == std::vector<double>{1591.5, 1070.0});
    CHECK(GUI::ListRootFrequencies(result, false, false) == std::vector<double>{1.6e4});
    CHECK(GUI::ListRootFrequencies(result, true, true).empty());
}

TEST_CASE("A hovered root is described by kind, frequency and, for a pair, quality and damping", "[pole_zero_view]") {
    CHECK(GUI::DescribePoleZeroRoot(MakeReal(true, -1e4, 1591.5)) == "pole at 1.591kHz");
    CHECK(GUI::DescribePoleZeroRoot(MakePair(false, -4500.0, 5000.0, 1070.0, 0.74)) ==
          "zero pair at 1.07kHz, Q 0.74, zeta 0.676");
}

TEST_CASE("The note about cancelled pairs is empty when none cancelled", "[pole_zero_view]") {
    CHECK(GUI::DescribeCancelledPairs(0).empty());
    CHECK(GUI::DescribeCancelledPairs(1) == "1 pole-zero pair at the same place cancel and is left out");
    CHECK(GUI::DescribeCancelledPairs(3) == "3 pole-zero pairs at the same place cancel and are left out");
}

TEST_CASE("The s-plane limits keep half a decade past the outermost roots on a symmetric logarithmic axis",
          "[pole_zero_view]") {
    Core::PoleZero result;
    result.Roots = {MakePair(true, -4500.0, 5000.0, 1070.0, 0.74), MakeReal(true, -6e6, 9.5e5),
                    MakeReal(false, 2e6, 3e5)};
    const GUI::PlaneLimits limits = GUI::ComputePlaneLimits(result, true);
    CHECK_THAT(limits.RealMin, Catch::Matchers::WithinRel(-6e6 * std::sqrt(10.0), 1e-9));
    CHECK_THAT(limits.RealMax, Catch::Matchers::WithinRel(2e6 * std::sqrt(10.0), 1e-9));
    // The roots of a pair are conjugates, so the imaginary range is symmetric
    CHECK_THAT(limits.ImaginaryMax, Catch::Matchers::WithinRel(5000.0 * std::sqrt(10.0), 1e-9));
    CHECK(limits.ImaginaryMin == -limits.ImaginaryMax);
}

TEST_CASE("The s-plane limits pad a linear axis by a tenth, and keep the origin in view when every root is real",
          "[pole_zero_view]") {
    Core::PoleZero result;
    result.Roots = {MakeReal(true, -1e4, 1591.5), MakeReal(true, -3e4, 4775.0)};
    const GUI::PlaneLimits linear = GUI::ComputePlaneLimits(result, false);
    CHECK_THAT(linear.RealMin, Catch::Matchers::WithinRel(-3.3e4, 1e-9));
    // No root on the right side, yet the imaginary axis stays in view, as does the real axis with no imaginary part
    CHECK(linear.RealMax > 0.0);
    CHECK(linear.ImaginaryMin < 0.0);
    CHECK(linear.ImaginaryMax > 0.0);
    const GUI::PlaneLimits empty = GUI::ComputePlaneLimits({}, true);
    CHECK(empty.RealMin < 0.0);
    CHECK(empty.RealMax > 0.0);
}
