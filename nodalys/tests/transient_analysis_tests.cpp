/**
 * @file    transient_analysis_tests.cpp
 * @brief   Tests of the time step control of the transient analysis: the corners of the source waveforms.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "devices/waveforms.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <nodalys/nodalys.h>
#include <numbers>

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

/**
 * @brief   Runs a transient with the given method.
 * @details The steps stay at the output step as long as the truncation error is small, which the circuits of the
 *          accuracy tests keep, so halving the output step halves every step.
 */
Nodalys::TransientResult RunTransient(const Nodalys::Circuit &circuit, const double stop_time, const double time_step,
                                      const Nodalys::IntegrationMethod method, const bool uses_initial_conditions) {
    const Nodalys::TransientConfig config{.StopTime = stop_time,
                                          .TimeStep = time_step,
                                          .MaxStep = time_step,
                                          .UseInitialConditions = uses_initial_conditions,
                                          .Method = method};
    auto result = Nodalys::ComputeTransient(circuit, config, uses_initial_conditions);
    REQUIRE(result.has_value());
    return std::move(*result);
}

/**
 * @brief   Largest absolute value of a series after the given time.
 */
double FindPeakAfter(const std::vector<double> &times, const std::vector<double> &values, const double start_time) {
    double peak = 0.0;
    for (std::size_t index = 0; index < times.size(); ++index) {
        if (times[index] >= start_time) {
            peak = std::max(peak, std::fabs(values[index]));
        }
    }
    return peak;
}

/// RC discharge: 1 V on the capacitor, v(t) = exp(-t / tau) with tau = 1 ms.
double MeasureRcDischargeError(const Nodalys::IntegrationMethod method, const double time_step) {
    constexpr double RESISTANCE = 1e3;
    constexpr double CAPACITANCE = 1e-6;
    constexpr double STOP_TIME = 2e-3;
    Nodalys::CircuitBuilder builder;
    builder.AddResistor("R1", 1, 0, RESISTANCE);
    builder.AddCapacitor("C1", 1, 0, CAPACITANCE, 1.0);
    auto circuit = builder.Build();
    REQUIRE(circuit.has_value());
    const Nodalys::TransientResult result = RunTransient(*circuit, STOP_TIME, time_step, method, true);
    return std::fabs(result.NodeVoltages[1].back() - std::exp(-result.Times.back() / (RESISTANCE * CAPACITANCE)));
}

/// RL decay: 1 A in the inductor, i(t) = exp(-t R / L) with L / R = 1 ms.
double MeasureRlDecayError(const Nodalys::IntegrationMethod method, const double time_step) {
    constexpr double RESISTANCE = 10.0;
    constexpr double INDUCTANCE = 10e-3;
    constexpr double STOP_TIME = 2e-3;
    Nodalys::CircuitBuilder builder;
    builder.AddResistor("R1", 1, 0, RESISTANCE);
    builder.AddInductor("L1", 1, 0, INDUCTANCE, 1.0);
    auto circuit = builder.Build();
    REQUIRE(circuit.has_value());
    const Nodalys::TransientResult result = RunTransient(*circuit, STOP_TIME, time_step, method, true);
    // The inductor current flows from node 1 to ground through L1 and back through R1: v = -R i
    const double current = -result.NodeVoltages[1].back() / RESISTANCE;
    return std::fabs(current - std::exp(-result.Times.back() * RESISTANCE / INDUCTANCE));
}

/**
 * @brief   Value of a node at an instant, interpolated between the output points around it.
 */
double ReadNodeAt(const Nodalys::TransientResult &result, const int node, const double time) {
    const auto after = std::ranges::lower_bound(result.Times, time);
    REQUIRE(after != result.Times.end());
    const std::size_t upper = static_cast<std::size_t>(after - result.Times.begin());
    if (upper == 0) {
        return result.NodeVoltages[static_cast<std::size_t>(node)][0];
    }
    const std::vector<double> &voltages = result.NodeVoltages[static_cast<std::size_t>(node)];
    const double share = (time - result.Times[upper - 1]) / (result.Times[upper] - result.Times[upper - 1]);
    return voltages[upper - 1] + share * (voltages[upper] - voltages[upper - 1]);
}

/**
 * @brief   Checks that the two methods agree on the voltage of a node at several instants of a smooth transient.
 * @details A wrong history in the companion model of a device makes a method drift away from the other one, so the
 *          agreement tests that each charge of the device is integrated by both formulas.
 */
void CheckMethodsAgree(const Nodalys::Circuit &circuit, const int node, const double stop_time,
                       const double time_step) {
    const Nodalys::TransientResult trapezoidal =
        RunTransient(circuit, stop_time, time_step, Nodalys::IntegrationMethod::TRAPEZOIDAL, false);
    const Nodalys::TransientResult gear =
        RunTransient(circuit, stop_time, time_step, Nodalys::IntegrationMethod::GEAR2, false);
    const double scale = FindPeak(trapezoidal, node);
    REQUIRE(scale > 0.1);
    for (const double share : {0.1, 0.2, 0.35, 0.5, 0.7, 0.95}) {
        const double time = share * stop_time;
        CAPTURE(time);
        CHECK_THAT(ReadNodeAt(gear, node, time), WithinAbs(ReadNodeAt(trapezoidal, node, time), 0.02 * scale));
    }
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

TEST_CASE("Both integration methods follow an RC discharge and an RL decay of the closed form",
          "[nodalys][transient][gear]") {
    const Nodalys::IntegrationMethod method =
        GENERATE(Nodalys::IntegrationMethod::TRAPEZOIDAL, Nodalys::IntegrationMethod::GEAR2);
    CAPTURE(method);

    CHECK(MeasureRcDischargeError(method, 50e-6) < 2e-4);
    CHECK(MeasureRlDecayError(method, 50e-6) < 2e-4);
}

TEST_CASE("Halving the step divides the error of either method by about four, as a second order method does",
          "[nodalys][transient][gear]") {
    const Nodalys::IntegrationMethod method =
        GENERATE(Nodalys::IntegrationMethod::TRAPEZOIDAL, Nodalys::IntegrationMethod::GEAR2);
    CAPTURE(method);

    const double coarse_rc = MeasureRcDischargeError(method, 100e-6);
    const double fine_rc = MeasureRcDischargeError(method, 50e-6);
    const double coarse_rl = MeasureRlDecayError(method, 100e-6);
    const double fine_rl = MeasureRlDecayError(method, 50e-6);

    CHECK_THAT(coarse_rc / fine_rc, WithinAbs(4.0, 0.8));
    CHECK_THAT(coarse_rl / fine_rl, WithinAbs(4.0, 0.8));
}

TEST_CASE("Gear-2 damps a lossless LC resonance that the trapezoidal rule keeps", "[nodalys][transient][gear]") {
    constexpr double INDUCTANCE = 1e-3;
    constexpr double CAPACITANCE = 1e-6;
    constexpr int PERIODS = 10;
    constexpr int STEPS_PER_PERIOD = 20;
    const double period = 2.0 * std::numbers::pi * std::sqrt(INDUCTANCE * CAPACITANCE);
    Nodalys::CircuitBuilder builder;
    builder.AddInductor("L1", 1, 0, INDUCTANCE);
    builder.AddCapacitor("C1", 1, 0, CAPACITANCE, 1.0);
    auto circuit = builder.Build();
    REQUIRE(circuit.has_value());

    const double stop_time = PERIODS * period;
    const double last_period = (PERIODS - 1) * period;
    const Nodalys::TransientResult trapezoidal =
        RunTransient(*circuit, stop_time, period / STEPS_PER_PERIOD, Nodalys::IntegrationMethod::TRAPEZOIDAL, true);
    const Nodalys::TransientResult gear =
        RunTransient(*circuit, stop_time, period / STEPS_PER_PERIOD, Nodalys::IntegrationMethod::GEAR2, true);

    // The samples of a 20-point period reach cos(18 degrees) = 0.95 of the amplitude at worst
    CHECK(FindPeakAfter(trapezoidal.Times, trapezoidal.NodeVoltages[1], last_period) > 0.94);
    // The principal root of BDF-2 at 18 degrees per step has modulus 0.99805, so nine periods of 20 steps leave 0.70
    CHECK(FindPeakAfter(gear.Times, gear.NodeVoltages[1], last_period) < 0.75);
}

TEST_CASE("Gear-2 settles an inductor whose current is cut, where the trapezoidal rule rings",
          "[nodalys][transient][gear]") {
    // 10 V through 10 ohm feeds 1 A into 1 mH; at 50 us a switch of 1 Gohm opens in series with the inductor, whose
    // current then has nowhere to flow. The stiff mode has a time constant of 1 ps, a million times shorter than the
    // step
    constexpr double CUT_TIME = 50e-6;
    constexpr double TIME_STEP = 1e-6;
    Nodalys::CircuitBuilder builder;
    builder.AddVdc("V1", 1, 0, 10.0);
    builder.AddResistor("R1", 1, 2, 10.0);
    builder.AddVpulse(
        "VC", 5, 0,
        {.Low = 5.0, .High = 0.0, .Delay = CUT_TIME, .RiseTime = 1e-9, .FallTime = 1e-9, .Width = 1.0, .Period = 2.0});
    builder.AddSwitch("S1", 2, 3, 5, 0,
                      {.Threshold = 2.5, .Hysteresis = 0.1, .OnResistance = 0.01, .OffResistance = 1e9});
    builder.AddInductor("L1", 3, 0, 1e-3);
    auto circuit = builder.Build();
    REQUIRE(circuit.has_value());

    const double settled_from = CUT_TIME + 10.0 * TIME_STEP;
    const Nodalys::TransientResult trapezoidal =
        RunTransient(*circuit, 100e-6, TIME_STEP, Nodalys::IntegrationMethod::TRAPEZOIDAL, false);
    const Nodalys::TransientResult gear =
        RunTransient(*circuit, 100e-6, TIME_STEP, Nodalys::IntegrationMethod::GEAR2, false);

    // The trapezoidal rule flips the voltage of the inductor between two values of opposite sign on every step, for
    // ever: it is still hundreds of volts at the end of the run. The Backward Euler step that follows the switch
    // only shrinks the amplitude the flip seeds, it cannot remove what the next trapezoidal steps keep
    CHECK(FindPeakAfter(trapezoidal.Times, trapezoidal.NodeVoltages[3], settled_from) > 100.0);
    CHECK(std::fabs(trapezoidal.NodeVoltages[3].back()) > 100.0);
    // Gear-2 leaves the correct answer: the inductor carries nothing and has no voltage
    CHECK(FindPeakAfter(gear.Times, gear.NodeVoltages[3], settled_from) < 1e-3);
}

TEST_CASE("Gear-2 follows an underdamped RLC step with the adaptive step", "[nodalys][transient][gear]") {
    // R = 20 ohm, L = 10 mH, C = 1 uF: alpha = 1000 1/s, omega_d = sqrt(1e8 - 1e6)
    constexpr double ALPHA = 1000.0;
    const double omega_d = std::sqrt(1e8 - ALPHA * ALPHA);
    Nodalys::CircuitBuilder builder;
    builder.AddVpulse(
        "V1", 1, 0,
        {.Low = 0.0, .High = 10.0, .Delay = 0.0, .RiseTime = 1e-9, .FallTime = 1e-9, .Width = 1.0, .Period = 2.0});
    builder.AddResistor("R1", 1, 2, 20.0);
    builder.AddInductor("L1", 2, 3, 10e-3);
    builder.AddCapacitor("C1", 3, 0, 1e-6);
    auto circuit = builder.Build();
    REQUIRE(circuit.has_value());

    const Nodalys::TransientResult gear =
        RunTransient(*circuit, 2.5e-3, 10e-6, Nodalys::IntegrationMethod::GEAR2, false);

    for (const double time : {0.31e-3, 1.0e-3, 2.0e-3}) {
        const double expected =
            10.0 *
            (1.0 - std::exp(-ALPHA * time) * (std::cos(omega_d * time) + ALPHA / omega_d * std::sin(omega_d * time)));
        CAPTURE(time);
        // Gear-2 loses a little amplitude at this step, a few tenths of a volt of a 17 V peak
        CHECK_THAT(ReadNodeAt(gear, 3, time), WithinAbs(expected, 0.4));
    }
}

TEST_CASE("Gear-2 integrates the junction charges of a diode, a BJT and a MOSFET like the trapezoidal rule",
          "[nodalys][transient][gear]") {
    const Nodalys::PulseParameters step{
        .Low = 0.0, .High = 5.0, .Delay = 0.0, .RiseTime = 1e-9, .FallTime = 1e-9, .Width = 1.0, .Period = 2.0};

    SECTION("A diode with a junction capacitance and a transit time") {
        Nodalys::CircuitBuilder builder;
        builder.AddVpulse("V1", 1, 0, step);
        builder.AddResistor("R1", 1, 2, 1e3);
        builder.AddDiode("D1", 2, 0, {.JunctionCapacitance = 100e-12, .TransitTime = 20e-9});
        builder.AddCapacitor("C1", 2, 0, 50e-12);
        auto circuit = builder.Build();
        REQUIRE(circuit.has_value());
        CheckMethodsAgree(*circuit, 2, 400e-9, 2e-9);
    }
    SECTION("A common-emitter BJT with its two junction capacitances and a transit time") {
        Nodalys::CircuitBuilder builder;
        builder.AddVdc("V2", 3, 0, 5.0);
        builder.AddVpulse("V1", 1, 0, step);
        builder.AddResistor("RB", 1, 2, 10e3);
        builder.AddResistor("RC", 3, 4, 1e3);
        builder.AddBjt("Q1", 4, 2, 0, true,
                       {.EmitterCapacitance = 10e-12, .CollectorCapacitance = 5e-12, .TransitTime = 5e-9});
        auto circuit = builder.Build();
        REQUIRE(circuit.has_value());
        CheckMethodsAgree(*circuit, 4, 2e-6, 5e-9);
    }
    SECTION("A MOSFET with its gate overlap capacitances") {
        Nodalys::CircuitBuilder builder;
        builder.AddVdc("V2", 3, 0, 5.0);
        builder.AddVpulse("V1", 1, 0, step);
        builder.AddResistor("RG", 1, 2, 1e3);
        builder.AddResistor("RD", 3, 4, 1e3);
        builder.AddMosfet("M1", 4, 2, 0, 0, true,
                          {.ThresholdVoltage = 1.0,
                           .Transconductance = 2e-3,
                           .GateSourceOverlap = 5e-10,
                           .GateDrainOverlap = 5e-10,
                           .Width = 100e-6,
                           .Length = 10e-6});
        auto circuit = builder.Build();
        REQUIRE(circuit.has_value());
        CheckMethodsAgree(*circuit, 4, 2e-6, 5e-9);
    }
}

TEST_CASE("Gear-2 integrates the coupled windings of a transformer like the trapezoidal rule",
          "[nodalys][transient][gear]") {
    Nodalys::CircuitBuilder builder;
    builder.AddVsine("V1", 1, 0, {.Amplitude = 5.0, .Frequency = 10e3, .Offset = 0.0});
    builder.AddTransformer("T1", 1, 0, 2, 0, 10e-3, 10e-3, 0.99, 1.0);
    builder.AddResistor("RL", 2, 0, 100.0);
    auto circuit = builder.Build();
    REQUIRE(circuit.has_value());
    CheckMethodsAgree(*circuit, 2, 300e-6, 1e-6);
}

TEST_CASE("Gear-2 integrates the filter capacitance of a smooth logic gate like the trapezoidal rule",
          "[nodalys][transient][gear]") {
    Nodalys::CircuitBuilder builder;
    builder.AddVpulse(
        "V1", 1, 0,
        {.Low = 0.0, .High = 5.0, .Delay = 1e-6, .RiseTime = 10e-9, .FallTime = 10e-9, .Width = 1.0, .Period = 2.0});
    builder.AddInverter("U1", 2, 1);
    builder.AddResistor("RL", 2, 0, 10e3);
    auto circuit = builder.Build();
    REQUIRE(circuit.has_value());
    CheckMethodsAgree(*circuit, 2, 5e-6, 10e-9);
}

TEST_CASE("A switch that closes onto a capacitor leaves no ringing of the trapezoidal rule behind",
          "[nodalys][transient][switch]") {
    // 12 V reach a 110 pF capacitor and a 1 kohm load through 0.01 ohm, a time constant of 1 ps against steps of 1 us.
    // The step that holds the flip is trapezoidal, which would leave the capacitor current flipping sign on every
    // step; the next step restarts with Backward Euler, which damps it at once
    constexpr double CLOSE_TIME = 20e-6;
    constexpr double TIME_STEP = 1e-6;
    Nodalys::CircuitBuilder builder;
    builder.AddVdc("V1", 1, 0, 12.0);
    builder.AddVpulse("VC", 5, 0,
                      {.Low = 0.0,
                       .High = 5.0,
                       .Delay = CLOSE_TIME,
                       .RiseTime = 100e-9,
                       .FallTime = 100e-9,
                       .Width = 1.0,
                       .Period = 2.0});
    builder.AddSwitch("S1", 1, 2, 5, 0,
                      {.Threshold = 2.5, .Hysteresis = 0.1, .OnResistance = 0.01, .OffResistance = 1e9});
    builder.AddCapacitor("C1", 2, 0, 110e-12);
    builder.AddResistor("R1", 2, 0, 1e3);
    auto circuit = builder.Build();
    REQUIRE(circuit.has_value());

    const Nodalys::TransientResult result =
        RunTransient(*circuit, 40e-6, TIME_STEP, Nodalys::IntegrationMethod::TRAPEZOIDAL, false);

    const auto source_current =
        std::ranges::find_if(result.Currents, [](const Nodalys::ComponentTrace &trace) { return trace.Name == "V1"; });
    REQUIRE(source_current != result.Currents.end());
    // After the switch closes, the load alone draws 12 mA; the capacitor must carry nothing
    const double settled_from = CLOSE_TIME + 2e-6;
    double largest_error = 0.0;
    for (std::size_t index = 0; index < result.Times.size(); ++index) {
        if (result.Times[index] >= settled_from) {
            largest_error = std::max(largest_error, std::fabs(std::fabs(source_current->Values[index]) - 12e-3));
        }
    }
    CHECK(largest_error < 1e-5);
}
