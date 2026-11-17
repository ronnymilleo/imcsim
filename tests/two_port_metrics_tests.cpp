/**
 * @file    two_port_metrics_tests.cpp
 * @brief   Tests of the stability factors and the gain bounds of a two-port, against values worked out by hand and
 *          with an independent script.
 */

#include "results/two_port_metrics.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>

namespace {

using Catch::Matchers::WithinAbs;
using Complex = std::complex<double>;

Complex Polar(const double magnitude, const double degrees) {
    return std::polar(magnitude, degrees * std::numbers::pi / 180.0);
}

/// A transistor of K a little below 1: potentially unstable, MSG 17.27 dB.
GUI::TwoPortMatrix MakePotentiallyUnstable() {
    return {.S11 = Polar(0.75, -115.0), .S12 = Polar(0.06, 60.0), .S21 = Polar(3.2, 70.0), .S22 = Polar(0.45, -40.0)};
}

/// A stable amplifier: K 2.4 and MAG 7.58 dB.
GUI::TwoPortMatrix MakeUnconditionallyStable() {
    return {.S11 = Polar(0.3, -70.0), .S12 = Polar(0.08, 60.0), .S21 = Polar(2.1, 80.0), .S22 = Polar(0.4, -40.0)};
}

} // namespace

TEST_CASE("A transistor with K below 1 is potentially unstable and has a maximum stable gain", "[two_port_metrics]") {
    const GUI::TwoPortMetrics metrics = GUI::ComputeTwoPortMetrics(MakePotentiallyUnstable());
    CHECK(metrics.IsValid);
    CHECK_THAT(metrics.K, WithinAbs(0.91726, 1e-5));
    CHECK_THAT(metrics.DeltaMagnitude, WithinAbs(0.34238, 1e-5));
    CHECK_THAT(metrics.Mu, WithinAbs(0.94219, 1e-5));
    CHECK_THAT(metrics.MuPrime, WithinAbs(0.97485, 1e-5));
    CHECK_FALSE(metrics.IsUnconditionallyStable);
    CHECK(metrics.Gain == GUI::GainKind::MSG);
    // 10 log10(3.2 / 0.06), the gain being a power ratio
    REQUIRE(metrics.GainDecibels);
    CHECK_THAT(*metrics.GainDecibels, WithinAbs(17.2700, 1e-3));
}

TEST_CASE("An unconditionally stable amplifier has a maximum available gain below its maximum stable gain",
          "[two_port_metrics]") {
    const GUI::TwoPortMetrics metrics = GUI::ComputeTwoPortMetrics(MakeUnconditionallyStable());
    CHECK_THAT(metrics.K, WithinAbs(2.40004, 1e-4));
    CHECK_THAT(metrics.DeltaMagnitude, WithinAbs(0.23752, 1e-4));
    CHECK_THAT(metrics.Mu, WithinAbs(1.75682, 1e-4));
    CHECK(metrics.IsUnconditionallyStable);
    CHECK(metrics.Gain == GUI::GainKind::MAG);
    // |S21 / S12| (K - sqrt(K^2 - 1)) in decibels
    REQUIRE(metrics.GainDecibels);
    CHECK_THAT(*metrics.GainDecibels, WithinAbs(7.5809, 1e-3));
    REQUIRE(metrics.MaximumStableGainDecibels);
    CHECK_THAT(*metrics.MaximumStableGainDecibels, WithinAbs(14.1913, 1e-3));
}

TEST_CASE("A matched resistive pad is stable and its maximum available gain is its loss", "[two_port_metrics]") {
    // A 6 dB pad between 50 ohm ports: S11 = S22 = 0 and S21 = S12 = 0.5, so Delta = -0.25 and K = 2.125
    const GUI::TwoPortMetrics metrics = GUI::ComputeTwoPortMetrics({.S11 = 0.0, .S12 = 0.5, .S21 = 0.5, .S22 = 0.0});
    CHECK_THAT(metrics.K, WithinAbs(2.125, 1e-12));
    CHECK_THAT(metrics.Mu, WithinAbs(4.0, 1e-12));
    CHECK(metrics.IsUnconditionallyStable);
    REQUIRE(metrics.GainDecibels);
    CHECK_THAT(*metrics.GainDecibels, WithinAbs(-6.0206, 1e-3));
}

TEST_CASE("A lossless reciprocal network sits on the edge of stability and counts as stable", "[two_port_metrics]") {
    // A series reactance equal to twice the port resistance: S11 = (1 + j) / 2, S21 = (1 - j) / 2, |Delta| = 1
    const Complex reflection(0.5, 0.5);
    const Complex transmission(0.5, -0.5);
    const GUI::TwoPortMetrics metrics =
        GUI::ComputeTwoPortMetrics({.S11 = reflection, .S12 = transmission, .S21 = transmission, .S22 = reflection});
    CHECK_THAT(metrics.K, WithinAbs(1.0, 1e-12));
    CHECK_THAT(metrics.DeltaMagnitude, WithinAbs(1.0, 1e-12));
    CHECK_THAT(metrics.Mu, WithinAbs(1.0, 1e-12));
    CHECK(metrics.IsUnconditionallyStable);
    CHECK(metrics.Gain == GUI::GainKind::MAG);
    REQUIRE(metrics.GainDecibels);
    CHECK_THAT(*metrics.GainDecibels, WithinAbs(0.0, 1e-9));
}

TEST_CASE("A rounding that puts mu a hair below 1 does not make a lossless network unstable", "[two_port_metrics]") {
    const Complex reflection(0.5, 0.5);
    const Complex transmission(0.5 - 1e-9, -0.5);
    const GUI::TwoPortMetrics metrics =
        GUI::ComputeTwoPortMetrics({.S11 = reflection, .S12 = transmission, .S21 = transmission, .S22 = reflection});
    CHECK(metrics.IsUnconditionallyStable);
    // K is a hair under 1 too, and the root of K^2 - 1 does not become not a number
    REQUIRE(metrics.GainDecibels);
    CHECK(std::isfinite(*metrics.GainDecibels));
}

TEST_CASE("K above 1 with |Delta| above 1 is still potentially unstable, which mu tells", "[two_port_metrics]") {
    const GUI::TwoPortMatrix matrix{.S11 = Complex(-0.7578438133993702, 0.9188459319005627),
                                    .S12 = Complex(0.007209675368719779, -0.01258839494837638),
                                    .S21 = Complex(0.20959082932642423, 0.8400291214105523),
                                    .S22 = Complex(0.9919424034140327, -0.5280307461321242)};
    const GUI::TwoPortMetrics metrics = GUI::ComputeTwoPortMetrics(matrix);
    CHECK_THAT(metrics.K, WithinAbs(4.28474, 1e-4));
    CHECK_THAT(metrics.DeltaMagnitude, WithinAbs(1.33754, 1e-4));
    CHECK(metrics.Mu < 1.0);
    CHECK_FALSE(metrics.IsUnconditionallyStable);
    CHECK(metrics.Gain == GUI::GainKind::MSG);
}

TEST_CASE("A unilateral two-port has an infinite K and the unilateral power gain when it is stable",
          "[two_port_metrics]") {
    const GUI::TwoPortMetrics metrics = GUI::ComputeTwoPortMetrics({.S11 = 0.5, .S12 = 0.0, .S21 = 3.0, .S22 = 0.4});
    CHECK(metrics.IsUnilateral);
    CHECK(std::isinf(metrics.K));
    // The limit of the formulas: 1 / |S22| and 1 / |S11|
    CHECK_THAT(metrics.Mu, WithinAbs(2.5, 1e-12));
    CHECK_THAT(metrics.MuPrime, WithinAbs(2.0, 1e-12));
    CHECK(metrics.IsUnconditionallyStable);
    CHECK(metrics.Gain == GUI::GainKind::UNILATERAL);
    // 9 / ((1 - 0.25)(1 - 0.16)) = 14.2857
    REQUIRE(metrics.GainDecibels);
    CHECK_THAT(*metrics.GainDecibels, WithinAbs(11.5490, 1e-3));
    CHECK_FALSE(metrics.MaximumStableGainDecibels);
}

TEST_CASE("A unilateral two-port that reflects more than it receives is unstable with no gain bound",
          "[two_port_metrics]") {
    const GUI::TwoPortMetrics metrics = GUI::ComputeTwoPortMetrics({.S11 = 1.2, .S12 = 0.0, .S21 = 3.0, .S22 = 0.4});
    CHECK(metrics.IsUnilateral);
    CHECK(metrics.Mu < 1.0);
    CHECK_FALSE(metrics.IsUnconditionallyStable);
    CHECK(metrics.Gain == GUI::GainKind::NONE);
    CHECK_FALSE(metrics.GainDecibels);
}

TEST_CASE("A unilateral two-port that reflects nothing has an infinite mu", "[two_port_metrics]") {
    const GUI::TwoPortMetrics metrics = GUI::ComputeTwoPortMetrics({.S11 = 0.5, .S12 = 0.0, .S21 = 3.0, .S22 = 0.0});
    CHECK(std::isinf(metrics.Mu));
    CHECK(metrics.IsUnconditionallyStable);
}

TEST_CASE("A two-port that transmits nothing forward has no gain to bound", "[two_port_metrics]") {
    const GUI::TwoPortMetrics metrics = GUI::ComputeTwoPortMetrics({.S11 = 0.5, .S12 = 0.2, .S21 = 0.0, .S22 = 0.4});
    CHECK(metrics.IsUnilateral);
    CHECK_FALSE(metrics.HasForwardTransmission);
    CHECK(metrics.Gain == GUI::GainKind::NONE);
    CHECK_FALSE(metrics.GainDecibels);
    CHECK_FALSE(metrics.MaximumStableGainDecibels);
}

TEST_CASE("A matrix with a value that is not finite is invalid", "[two_port_metrics]") {
    const GUI::TwoPortMetrics metrics = GUI::ComputeTwoPortMetrics(
        {.S11 = 0.5, .S12 = 0.1, .S21 = std::numeric_limits<double>::quiet_NaN(), .S22 = 0.4});
    CHECK_FALSE(metrics.IsValid);
    CHECK_FALSE(metrics.GainDecibels);
}

TEST_CASE("The sweep reads S12 and S21 from the right entries of the matrix", "[two_port_metrics]") {
    // Row by row: S11, S12, S21, S22
    Core::SParameters result;
    result.Frequencies = {1e6, 2e6};
    result.ReferenceOhms = {50.0, 75.0};
    const GUI::TwoPortMatrix matrix = MakePotentiallyUnstable();
    for (int index = 0; index < 2; ++index) {
        result.Values.insert(result.Values.end(), {matrix.S11, matrix.S12, matrix.S21, matrix.S22});
    }
    const GUI::TwoPortSweep sweep = GUI::ComputeTwoPortSweep(result);
    REQUIRE(sweep.Metrics.size() == 2);
    CHECK(sweep.Frequencies == result.Frequencies);
    // The factors read power waves, so ports of different reference impedances are not converted
    CHECK_THAT(sweep.Metrics[1].K, WithinAbs(GUI::ComputeTwoPortMetrics(matrix).K, 1e-12));
    CHECK_THAT(*sweep.Metrics[1].GainDecibels, WithinAbs(17.2700, 1e-3));
}

TEST_CASE("Only exactly two ports have a sweep of metrics", "[two_port_metrics]") {
    Core::SParameters one_port;
    one_port.Frequencies = {1e6};
    one_port.ReferenceOhms = {50.0};
    one_port.Values = {Complex(0.1, 0.0)};
    CHECK_FALSE(GUI::IsTwoPort(one_port));
    CHECK(GUI::ComputeTwoPortSweep(one_port).Metrics.empty());

    Core::SParameters three_ports;
    three_ports.Frequencies = {1e6};
    three_ports.ReferenceOhms = {50.0, 50.0, 50.0};
    three_ports.Values.assign(9, Complex(0.1, 0.0));
    CHECK_FALSE(GUI::IsTwoPort(three_ports));
    CHECK(GUI::ComputeTwoPortSweep(three_ports).Metrics.empty());
}
