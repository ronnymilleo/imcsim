/**
 * @file    thyristor_device_tests.cpp
 * @brief   Tests of the thyristor of Nodalys: the gate firing, the latch, the holding current, the blocking states
 *          and the controlled rectifier against its closed form.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <nodalys/nodalys.h>
#include <numbers>
#include <string_view>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

/// A small SCR: 0.9 V + 10 mOhm on-state, 5 mA holding, 1 mA gate trigger behind 100 Ohm.
constexpr Nodalys::ThyristorParameters SCR = {.OnResistance = 0.01,
                                              .OnThresholdVoltage = 0.9,
                                              .OffResistance = 1e9,
                                              .HoldingCurrent = 5e-3,
                                              .GateTriggerCurrent = 1e-3,
                                              .GateTriggerVoltage = 0.7,
                                              .GateResistance = 100.0,
                                              .BreakoverVoltage = 0.0};
/// Node of the supply, of the load and of the gate drive.
constexpr int SUPPLY = 1;
constexpr int LOAD = 2;
constexpr int GATE = 3;

/// A supply on node 1 feeding the thyristor to node 2, where a load goes to ground, and a gate drive on node 3.
Nodalys::Circuit BuildScrLoop(const Nodalys::ThyristorParameters &params,
                              const double supply_volts,
                              const double load_ohms,
                              const double gate_volts) {
    Nodalys::CircuitBuilder builder;
    builder.AddVdc("V1", SUPPLY, 0, supply_volts);
    builder.AddThyristor("SCR1", SUPPLY, LOAD, GATE, params);
    builder.AddResistor("R1", LOAD, 0, load_ohms);
    builder.AddVdc("Vg", GATE, LOAD, gate_volts);
    auto circuit = builder.Build();
    REQUIRE(circuit.has_value());
    return std::move(*circuit);
}

/// The same loop, with the gate driven by a pulse instead of a steady voltage.
Nodalys::Circuit BuildPulsedScrLoop(const Nodalys::ThyristorParameters &params,
                                    const double supply_volts,
                                    const double load_ohms,
                                    const Nodalys::PulseParameters &gate_pulse) {
    Nodalys::CircuitBuilder builder;
    builder.AddVdc("V1", SUPPLY, 0, supply_volts);
    builder.AddThyristor("SCR1", SUPPLY, LOAD, GATE, params);
    builder.AddResistor("R1", LOAD, 0, load_ohms);
    builder.AddVpulse("Vg", GATE, LOAD, gate_pulse);
    auto circuit = builder.Build();
    REQUIRE(circuit.has_value());
    return std::move(*circuit);
}

Nodalys::TransientResult RunTransient(const Nodalys::Circuit &circuit, const double stop_time, const double time_step) {
    const Nodalys::TransientConfig config{.StopTime = stop_time,
                                          .TimeStep = time_step,
                                          .MaxStep = time_step,
                                          .Method = Nodalys::IntegrationMethod::GEAR2};
    auto result = Nodalys::ComputeTransient(circuit, config);
    REQUIRE(result.has_value());
    return std::move(*result);
}

Nodalys::OperatingPointResult RunOperatingPoint(const Nodalys::Circuit &circuit) {
    auto result = Nodalys::ComputeOperatingPoint(circuit);
    REQUIRE(result.has_value());
    return std::move(*result);
}

/// The load voltage at the first time point at or after an instant.
double LoadVoltageAt(const Nodalys::TransientResult &result, const double time) {
    const auto point = std::ranges::lower_bound(result.Times, time);
    REQUIRE(point != result.Times.end());
    return result.NodeVoltages[LOAD][static_cast<std::size_t>(point - result.Times.begin())];
}

/// The values of one named current trace of a transient.
const std::vector<double> &CurrentTrace(const Nodalys::TransientResult &result, const std::string_view name) {
    const auto trace = std::ranges::find(result.Currents, name, &Nodalys::ComponentTrace::Name);
    REQUIRE(trace != result.Currents.end());
    return trace->Values;
}

/// The mean of the load voltage over one period of the steady state, from the second period on.
double MeanLoadVoltageOverPeriod(const Nodalys::TransientResult &result, const double period) {
    double sum = 0.0;
    int count = 0;
    for (std::size_t i = 0; i < result.Times.size(); ++i) {
        if (result.Times[i] >= period && result.Times[i] < 2.0 * period) {
            sum += result.NodeVoltages[LOAD][i];
            ++count;
        }
    }
    REQUIRE(count > 0);
    return sum / static_cast<double>(count);
}

} // namespace

TEST_CASE("A thyristor blocks forward voltage until the gate fires it", "[nodalys][thyristor]") {
    // One gate pulse at 1 ms; before it the 10 V supply sits across the blocking device
    const Nodalys::PulseParameters pulse{.High = 5.0, .Delay = 1e-3, .Width = 0.2e-3, .Period = 1.0};
    const Nodalys::TransientResult result = RunTransient(BuildPulsedScrLoop(SCR, 10.0, 100.0, pulse), 2e-3, 1e-5);
    CHECK_THAT(LoadVoltageAt(result, 0.5e-3), WithinAbs(0.0, 1e-3));
    CHECK_THAT(LoadVoltageAt(result, 1.1e-3), WithinAbs(9.099, 0.01));
}

TEST_CASE("A thyristor latches on after the gate pulse ends", "[nodalys][thyristor]") {
    // The pulse ends at 1.2 ms; the 91 mA anode current is far above the 5 mA holding current
    const Nodalys::PulseParameters pulse{.High = 5.0, .Delay = 1e-3, .Width = 0.2e-3, .Period = 1.0};
    const Nodalys::TransientResult result = RunTransient(BuildPulsedScrLoop(SCR, 10.0, 100.0, pulse), 2e-3, 1e-5);
    CHECK_THAT(LoadVoltageAt(result, 1.5e-3), WithinAbs(9.099, 0.01));
    const std::vector<double> &gate = CurrentTrace(result, "SCR1.G");
    const auto point = std::ranges::lower_bound(result.Times, 1.5e-3);
    CHECK_THAT(gate[static_cast<std::size_t>(point - result.Times.begin())], WithinAbs(0.0, 1e-6));
}

TEST_CASE("A thyristor releases when its current falls below the holding current", "[nodalys][thyristor]") {
    // A 10 kOhm load carries only 0.91 mA, below the 5 mA holding current: the gate sustains conduction during the
    // pulse, and the latch releases once the pulse ends
    const Nodalys::PulseParameters pulse{.High = 5.0, .Delay = 1e-3, .Width = 0.2e-3, .Period = 1.0};
    const Nodalys::TransientResult result = RunTransient(BuildPulsedScrLoop(SCR, 10.0, 10e3, pulse), 2e-3, 1e-5);
    CHECK_THAT(LoadVoltageAt(result, 1.1e-3), WithinAbs(9.099, 0.01));
    CHECK_THAT(LoadVoltageAt(result, 1.5e-3), WithinAbs(0.0, 0.1));
}

TEST_CASE("A thyristor blocks reverse voltage even with gate current", "[nodalys][thyristor]") {
    const Nodalys::OperatingPointResult result = RunOperatingPoint(BuildScrLoop(SCR, -10.0, 100.0, 5.0));
    CHECK_THAT(result.NodeVoltages[LOAD], WithinAbs(0.0, 1e-3));
}

TEST_CASE("Forward breakover fires a thyristor without a gate", "[nodalys][thyristor]") {
    Nodalys::ThyristorParameters breakover = SCR;
    breakover.BreakoverVoltage = 5.0;
    // 10 V is past the 5 V breakover; 3 V is not
    const Nodalys::OperatingPointResult fired = RunOperatingPoint(BuildScrLoop(breakover, 10.0, 100.0, 0.0));
    CHECK_THAT(fired.NodeVoltages[LOAD], WithinAbs(9.099, 0.01));
    const Nodalys::OperatingPointResult blocked = RunOperatingPoint(BuildScrLoop(breakover, 3.0, 100.0, 0.0));
    CHECK_THAT(blocked.NodeVoltages[LOAD], WithinAbs(0.0, 1e-3));
}

TEST_CASE("The on-state follows the datasheet line V0 + RT*I", "[nodalys][thyristor]") {
    const Nodalys::OperatingPointResult result = RunOperatingPoint(BuildScrLoop(SCR, 10.0, 100.0, 5.0));
    const double current = result.NodeVoltages[LOAD] / 100.0;
    const double vak = 10.0 - result.NodeVoltages[LOAD];
    CHECK_THAT(vak, WithinAbs(SCR.OnThresholdVoltage + SCR.OnResistance * current, 1e-9));
}

TEST_CASE("The gate draws the current its junction line gives", "[nodalys][thyristor]") {
    // The gate drive is referenced to the cathode, so it puts the full 5 V across the junction: (5 - 0.7) / 100
    const Nodalys::OperatingPointResult result = RunOperatingPoint(BuildScrLoop(SCR, 0.0, 100.0, 5.0));
    const auto gate = std::ranges::find(result.Currents, "SCR1.G", &Nodalys::ComponentCurrent::Name);
    REQUIRE(gate != result.Currents.end());
    CHECK_THAT(gate->Current, WithinAbs(0.043, 1e-6));
}

TEST_CASE("A half-wave controlled rectifier averages the closed form of its firing angle", "[nodalys][thyristor]") {
    // An ideal on-state (no V0) so the average is exactly Vm(1 + cos alpha) / (2 pi)
    Nodalys::ThyristorParameters ideal = SCR;
    ideal.OnThresholdVoltage = 0.0;
    const double amplitude = 10.0;
    const double frequency = 50.0;
    const double period = 1.0 / frequency;
    for (const double angle_degrees : {60.0, 90.0}) {
        const double alpha = angle_degrees * std::numbers::pi / 180.0;
        const Nodalys::PulseParameters pulse{.High = 5.0,
                                             .Delay = alpha / (2.0 * std::numbers::pi * frequency),
                                             .RiseTime = 1e-6,
                                             .FallTime = 1e-6,
                                             .Width = 0.5e-3,
                                             .Period = period};
        Nodalys::CircuitBuilder builder;
        builder.AddVsine("V1", SUPPLY, 0, {.Amplitude = amplitude, .Frequency = frequency});
        builder.AddThyristor("SCR1", SUPPLY, LOAD, GATE, ideal);
        builder.AddResistor("R1", LOAD, 0, 100.0);
        builder.AddVpulse("Vg", GATE, 0, pulse);
        auto circuit = builder.Build();
        REQUIRE(circuit.has_value());
        const Nodalys::TransientResult result = RunTransient(*circuit, 2.0 * period, 20e-6);
        const double expected = amplitude * (1.0 + std::cos(alpha)) / (2.0 * std::numbers::pi);
        INFO("firing angle " << angle_degrees << " degrees");
        CHECK_THAT(MeanLoadVoltageOverPeriod(result, period), WithinAbs(expected, 0.05));
    }
}

TEST_CASE("The transient lands on the commutation instant of a controlled rectifier", "[nodalys][thyristor]") {
    // The anode current falls to the 5 mA holding current just before the zero of the 50 Hz sine, at
    // pi - arcsin(IH*R/Vm); the predicted breakpoint must land the turn-off there, not a step late
    Nodalys::ThyristorParameters ideal = SCR;
    ideal.OnThresholdVoltage = 0.0;
    const double amplitude = 10.0;
    const double frequency = 50.0;
    const double period = 1.0 / frequency;
    const Nodalys::PulseParameters pulse{.High = 5.0,
                                         .Delay = period / 6.0,
                                         .RiseTime = 1e-6,
                                         .FallTime = 1e-6,
                                         .Width = 0.5e-3,
                                         .Period = period};
    Nodalys::CircuitBuilder builder;
    builder.AddVsine("V1", SUPPLY, 0, {.Amplitude = amplitude, .Frequency = frequency});
    builder.AddThyristor("SCR1", SUPPLY, LOAD, GATE, ideal);
    builder.AddResistor("R1", LOAD, 0, 100.0);
    builder.AddVpulse("Vg", GATE, 0, pulse);
    auto circuit = builder.Build();
    REQUIRE(circuit.has_value());
    const Nodalys::TransientResult result = RunTransient(*circuit, period, 20e-6);
    // The first point after the firing where the load has lost its voltage is the turn-off
    const std::vector<double> &load = result.NodeVoltages[LOAD];
    const auto fired = std::ranges::find_if(load, [](const double volts) { return volts > 1.0; });
    REQUIRE(fired != load.end());
    const auto off = std::find_if(fired, load.end(), [](const double volts) { return volts < 0.1; });
    REQUIRE(off != load.end());
    const double turn_off = result.Times[static_cast<std::size_t>(off - load.begin())];
    const double expected =
        (std::numbers::pi - std::asin(SCR.HoldingCurrent * 100.0 / amplitude)) / (2.0 * std::numbers::pi * frequency);
    INFO("turn-off at " << turn_off << " s, holding-current crossing " << expected << " s");
    CHECK_THAT(turn_off, WithinAbs(expected, 1e-4));
}

TEST_CASE("Harmonic balance refuses a thyristor", "[nodalys][thyristor]") {
    Nodalys::CircuitBuilder builder;
    builder.AddVsine("V1", SUPPLY, 0, {.Amplitude = 10.0, .Frequency = 50.0});
    builder.AddThyristor("SCR1", SUPPLY, LOAD, GATE, SCR);
    builder.AddResistor("R1", LOAD, 0, 100.0);
    builder.AddVdc("Vg", GATE, 0, 0.0);
    auto circuit = builder.Build();
    REQUIRE(circuit.has_value());
    const auto result = Nodalys::ComputeHarmonicBalance(*circuit, {.FundamentalFrequency = 50.0, .Harmonics = 4});
    CHECK_FALSE(result.has_value());
}
