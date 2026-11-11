/**
 * @file    transient_analysis_tests.cpp
 * @brief   Tests of the time step control of the transient analysis: the corners of the source waveforms.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "devices/waveforms.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <nodalys/nodalys.h>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

/**
 * @brief   Highest voltage a node reaches over the output points of a transient.
 */
double FindPeak(const Nodalys::TransientResult &result, const int node) {
    const std::vector<double> &voltages = result.NodeVoltages[static_cast<std::size_t>(node)];
    return *std::ranges::max_element(voltages);
}

} // namespace

TEST_CASE("A pulse narrower than the time step still drives the circuit", "[nodalys][transient]") {
    // 2 us at 5 V between output points 10 us apart, into an RC of 1 us
    const Nodalys::PulseParameters pulse{
        .Low = 0.0, .High = 5.0, .Delay = 11e-6, .RiseTime = 1e-9, .FallTime = 1e-9, .Width = 2e-6, .Period = 100e-6};
    Nodalys::CircuitBuilder builder;
    builder.AddVpulse("V1", 1, 0, pulse);
    builder.AddResistor("R1", 1, 2, 1e3);
    builder.AddCapacitor("C1", 2, 0, 1e-9);
    auto circuit = builder.Build();
    REQUIRE(circuit.has_value());

    const auto result = Nodalys::ComputeTransient(*circuit, {.StopTime = 100e-6, .TimeStep = 10e-6});

    REQUIRE(result.has_value());
    // The source reaches its high level on the output points, which keep the corners
    CHECK_THAT(FindPeak(*result, 1), WithinAbs(5.0, 1e-9));
    // The capacitor charges for the width of the pulse: 5 (1 - e^-2) = 4.32 V, plus the share of the edges
    CHECK_THAT(FindPeak(*result, 2), WithinRel(5.0 * (1.0 - std::exp(-2.0)), 0.01));
}

TEST_CASE("A clock edge lands on an output point even between two of them", "[nodalys][transient]") {
    // A 30 kHz clock has edges at 0, 16.7 us, 33.3 us..., none on the 10 us grid
    Nodalys::CircuitBuilder builder;
    builder.AddClock("CLK", 1, 0, {.Frequency = 30e3, .DutyCycle = 0.5, .LogicLevel = 5.0, .StartDelay = 0.0});
    builder.AddResistor("R1", 1, 0, 1e3);
    auto circuit = builder.Build();
    REQUIRE(circuit.has_value());

    const auto result = Nodalys::ComputeTransient(*circuit, {.StopTime = 100e-6, .TimeStep = 10e-6});

    REQUIRE(result.has_value());
    const std::vector<double> &times = result->Times;
    const double falling_edge = 0.5 / 30e3;
    const bool has_edge =
        std::ranges::any_of(times, [falling_edge](const double time) { return std::fabs(time - falling_edge) < 1e-9; });
    CHECK(has_edge);
    CHECK(std::ranges::is_sorted(times));
}

TEST_CASE("The corners of a pulse come in order, edge by edge and period by period", "[nodalys][transient]") {
    const Nodalys::PulseParameters pulse{
        .Low = 0.0, .High = 1.0, .Delay = 1.0, .RiseTime = 0.1, .FallTime = 0.2, .Width = 0.5, .Period = 2.0};

    CHECK_THAT(Nodalys::FindNextPulseCorner(pulse, 0.0), WithinRel(1.0));
    CHECK_THAT(Nodalys::FindNextPulseCorner(pulse, 1.0), WithinRel(1.1));
    CHECK_THAT(Nodalys::FindNextPulseCorner(pulse, 1.1), WithinRel(1.6));
    CHECK_THAT(Nodalys::FindNextPulseCorner(pulse, 1.6), WithinRel(1.8));
    // After the fall, the next corner starts the following period
    CHECK_THAT(Nodalys::FindNextPulseCorner(pulse, 1.8), WithinRel(3.0));
    CHECK_THAT(Nodalys::FindNextPulseCorner(pulse, 3.05), WithinRel(3.1));
}

TEST_CASE("A single pulse has no corner once it has ended", "[nodalys][transient]") {
    const Nodalys::PulseParameters pulse{
        .Low = 0.0, .High = 1.0, .Delay = 0.0, .RiseTime = 0.1, .FallTime = 0.1, .Width = 0.3, .Period = 0.0};

    CHECK_THAT(Nodalys::FindNextPulseCorner(pulse, 0.45), WithinRel(0.5));
    CHECK(Nodalys::FindNextPulseCorner(pulse, 0.5) == std::numeric_limits<double>::infinity());
}

TEST_CASE("A piecewise linear source lands on each of its points", "[nodalys][transient]") {
    const Nodalys::PwlParameters pwl{.Points = {{0.0, 0.0}, {3e-6, 0.0}, {3.5e-6, 2.0}, {4e-6, 0.0}}};
    Nodalys::CircuitBuilder builder;
    builder.AddVpwl("V1", 1, 0, pwl);
    builder.AddResistor("R1", 1, 0, 1e3);
    auto circuit = builder.Build();
    REQUIRE(circuit.has_value());

    const auto result = Nodalys::ComputeTransient(*circuit, {.StopTime = 10e-6, .TimeStep = 5e-6});

    REQUIRE(result.has_value());
    CHECK_THAT(FindPeak(*result, 1), WithinAbs(2.0, 1e-9));
}
