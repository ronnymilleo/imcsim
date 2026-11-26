/**
 * @file    thyristor_device_tests.cpp
 * @brief   Tests of the thyristor (SCR) and TRIAC of Nodalys: the gate firing, the latch, the holding current, the
 *          blocking states, the dv/dt firing, the turn-off recovery, the four quadrants of the TRIAC and the
 *          controlled rectifier against its closed form.
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
/// The same figures as a TRIAC, which conducts both ways and fires in all four gate quadrants.
constexpr Nodalys::ThyristorParameters TRIAC = [] {
    Nodalys::ThyristorParameters params = SCR;
    params.IsBidirectional = true;
    return params;
}();
/// Node of the supply, of the load and of the gate drive.
constexpr int SUPPLY = 1;
constexpr int LOAD = 2;
constexpr int GATE = 3;
/// Node between the two sources of the TRIAC that reads a fast voltage fall.
constexpr int BIAS = 4;

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

/// The same loop with a pulsed supply, which the dv/dt firing and the turn-off recovery read; a gate pulse of
/// High = 0 leaves the gate silent.
Nodalys::Circuit BuildCommutatedLoop(const Nodalys::ThyristorParameters &params,
                                     const Nodalys::PulseParameters &supply_pulse,
                                     const double load_ohms,
                                     const Nodalys::PulseParameters &gate_pulse) {
    Nodalys::CircuitBuilder builder;
    builder.AddVpulse("V1", SUPPLY, 0, supply_pulse);
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
    if (!result.has_value()) {
        FAIL("transient failed: " << result.error());
    }
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

TEST_CASE("A thyristor fires on an anode voltage rise faster than its critical dv/dt", "[nodalys][thyristor]") {
    Nodalys::ThyristorParameters sensitive = SCR;
    sensitive.CriticalDvDt = 5e5;
    // The gate stays silent throughout; only the rate of rise of the supply can fire the device
    const Nodalys::PulseParameters silent_gate{.High = 0.0, .Delay = 0.0, .Width = 1.0, .Period = 10.0};
    // 10 V in 1 us is 1e7 V/s, far past the critical rate
    const Nodalys::PulseParameters fast{.High = 10.0,
                                        .Delay = 0.5e-3,
                                        .RiseTime = 1e-6,
                                        .FallTime = 1e-6,
                                        .Width = 1e-3,
                                        .Period = 10.0};
    const Nodalys::TransientResult fired =
        RunTransient(BuildCommutatedLoop(sensitive, fast, 100.0, silent_gate), 2e-3, 1e-5);
    CHECK_THAT(LoadVoltageAt(fired, 0.6e-3), WithinAbs(9.099, 0.01));
    // The pulse falls at 1.5 ms: the latch releases, and an SCR never fires on a fall
    CHECK_THAT(LoadVoltageAt(fired, 1.6e-3), WithinAbs(0.0, 0.05));
    // The same 10 V spread over 1 ms is 1e4 V/s, below the critical rate: the device keeps blocking
    Nodalys::PulseParameters slow = fast;
    slow.RiseTime = 1e-3;
    const Nodalys::TransientResult blocked =
        RunTransient(BuildCommutatedLoop(sensitive, slow, 100.0, silent_gate), 2e-3, 1e-5);
    CHECK_THAT(LoadVoltageAt(blocked, 1.2e-3), WithinAbs(0.0, 0.05));
}

TEST_CASE("Forward voltage re-applied within the turn-off time re-fires a thyristor", "[nodalys][thyristor]") {
    Nodalys::ThyristorParameters slow_turn_off = SCR;
    slow_turn_off.TurnOffTime = 0.5e-3;
    // The gate fires the device at 0.3 ms; the supply collapses at 0.8 ms and releases the latch, which then
    // recovers until 1.3 ms
    const Nodalys::PulseParameters gate{.High = 5.0, .Delay = 0.3e-3, .Width = 0.2e-3, .Period = 10.0};
    const Nodalys::PulseParameters supply{.High = 10.0,
                                          .Delay = 0.2e-3,
                                          .RiseTime = 1e-6,
                                          .FallTime = 1e-6,
                                          .Width = 0.6e-3,
                                          .Period = 1.0e-3};
    const Nodalys::TransientResult refired =
        RunTransient(BuildCommutatedLoop(slow_turn_off, supply, 100.0, gate), 2e-3, 1e-5);
    CHECK_THAT(LoadVoltageAt(refired, 0.5e-3), WithinAbs(9.099, 0.01));
    CHECK_THAT(LoadVoltageAt(refired, 1.0e-3), WithinAbs(0.0, 0.05));
    // The supply returns at 1.2 ms, inside the window: the device re-fires without a gate
    CHECK_THAT(LoadVoltageAt(refired, 1.25e-3), WithinAbs(9.099, 0.01));
    // The same circuit with the supply returning at 2.2 ms, past the window: the device blocks again
    Nodalys::PulseParameters late = supply;
    late.Period = 2.0e-3;
    const Nodalys::TransientResult blocked =
        RunTransient(BuildCommutatedLoop(slow_turn_off, late, 100.0, gate), 3e-3, 1e-5);
    CHECK_THAT(LoadVoltageAt(blocked, 1.0e-3), WithinAbs(0.0, 0.05));
    CHECK_THAT(LoadVoltageAt(blocked, 2.25e-3), WithinAbs(0.0, 0.05));
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

TEST_CASE("A TRIAC blocks both polarities until the gate fires it", "[nodalys][thyristor]") {
    // Without a gate the 10 V supply sits across the blocking device, whichever way it points
    const Nodalys::OperatingPointResult forward = RunOperatingPoint(BuildScrLoop(TRIAC, 10.0, 100.0, 0.0));
    CHECK_THAT(forward.NodeVoltages[LOAD], WithinAbs(0.0, 1e-3));
    const Nodalys::OperatingPointResult reverse = RunOperatingPoint(BuildScrLoop(TRIAC, -10.0, 100.0, 0.0));
    CHECK_THAT(reverse.NodeVoltages[LOAD], WithinAbs(0.0, 1e-3));
}

TEST_CASE("A TRIAC fires in all four gate quadrants", "[nodalys][thyristor]") {
    // MT2 positive or negative, gate positive or negative relative to MT1: each pair fires the latch
    const Nodalys::OperatingPointResult q1 = RunOperatingPoint(BuildScrLoop(TRIAC, 10.0, 100.0, 5.0));
    CHECK_THAT(q1.NodeVoltages[LOAD], WithinAbs(9.099, 0.01));
    const Nodalys::OperatingPointResult q2 = RunOperatingPoint(BuildScrLoop(TRIAC, 10.0, 100.0, -5.0));
    CHECK_THAT(q2.NodeVoltages[LOAD], WithinAbs(9.099, 0.01));
    const Nodalys::OperatingPointResult q3 = RunOperatingPoint(BuildScrLoop(TRIAC, -10.0, 100.0, -5.0));
    CHECK_THAT(q3.NodeVoltages[LOAD], WithinAbs(-9.099, 0.01));
    const Nodalys::OperatingPointResult q4 = RunOperatingPoint(BuildScrLoop(TRIAC, -10.0, 100.0, 5.0));
    CHECK_THAT(q4.NodeVoltages[LOAD], WithinAbs(-9.099, 0.01));
}

TEST_CASE("A TRIAC latches on after the gate pulse ends, either way", "[nodalys][thyristor]") {
    // The pulse ends at 1.2 ms; the 91 mA main current is far above the 5 mA holding current, both ways
    const Nodalys::PulseParameters pulse{.High = 5.0, .Delay = 1e-3, .Width = 0.2e-3, .Period = 1.0};
    const Nodalys::TransientResult forward = RunTransient(BuildPulsedScrLoop(TRIAC, 10.0, 100.0, pulse), 2e-3, 1e-5);
    CHECK_THAT(LoadVoltageAt(forward, 1.5e-3), WithinAbs(9.099, 0.01));
    const Nodalys::TransientResult reverse = RunTransient(BuildPulsedScrLoop(TRIAC, -10.0, 100.0, pulse), 2e-3, 1e-5);
    CHECK_THAT(LoadVoltageAt(reverse, 1.5e-3), WithinAbs(-9.099, 0.01));
}

TEST_CASE("A TRIAC releases below the holding current in either direction", "[nodalys][thyristor]") {
    // A 10 kOhm load carries only 0.91 mA, below the 5 mA holding current: the latch releases once the pulse ends
    const Nodalys::PulseParameters pulse{.High = 5.0, .Delay = 1e-3, .Width = 0.2e-3, .Period = 1.0};
    const Nodalys::TransientResult forward = RunTransient(BuildPulsedScrLoop(TRIAC, 10.0, 10e3, pulse), 2e-3, 1e-5);
    CHECK_THAT(LoadVoltageAt(forward, 1.1e-3), WithinAbs(9.099, 0.01));
    CHECK_THAT(LoadVoltageAt(forward, 1.5e-3), WithinAbs(0.0, 0.1));
    const Nodalys::TransientResult reverse = RunTransient(BuildPulsedScrLoop(TRIAC, -10.0, 10e3, pulse), 2e-3, 1e-5);
    CHECK_THAT(LoadVoltageAt(reverse, 1.1e-3), WithinAbs(-9.099, 0.01));
    CHECK_THAT(LoadVoltageAt(reverse, 1.5e-3), WithinAbs(0.0, 0.1));
}

TEST_CASE("Breakover fires a TRIAC without a gate, both ways", "[nodalys][thyristor]") {
    Nodalys::ThyristorParameters breakover = TRIAC;
    breakover.BreakoverVoltage = 5.0;
    // 10 V is past the 5 V breakover either way; 3 V is not
    const Nodalys::OperatingPointResult forward = RunOperatingPoint(BuildScrLoop(breakover, 10.0, 100.0, 0.0));
    CHECK_THAT(forward.NodeVoltages[LOAD], WithinAbs(9.099, 0.01));
    const Nodalys::OperatingPointResult reverse = RunOperatingPoint(BuildScrLoop(breakover, -10.0, 100.0, 0.0));
    CHECK_THAT(reverse.NodeVoltages[LOAD], WithinAbs(-9.099, 0.01));
    const Nodalys::OperatingPointResult blocked = RunOperatingPoint(BuildScrLoop(breakover, 3.0, 100.0, 0.0));
    CHECK_THAT(blocked.NodeVoltages[LOAD], WithinAbs(0.0, 1e-3));
}

TEST_CASE("A TRIAC dimmer conducts both half-cycles and commutates at the holding current", "[nodalys][thyristor]") {
    // An ideal on-state (no V0); a gate pulse train referenced to MT1 fires at 60 degrees of every half-cycle, so the
    // load follows the sine from 60 degrees on, positive and negative alike
    Nodalys::ThyristorParameters ideal = TRIAC;
    ideal.OnThresholdVoltage = 0.0;
    const double amplitude = 10.0;
    const double frequency = 50.0;
    const double period = 1.0 / frequency;
    const Nodalys::PulseParameters pulse{.High = 5.0,
                                         .Delay = period / 6.0,
                                         .RiseTime = 1e-6,
                                         .FallTime = 1e-6,
                                         .Width = 0.5e-3,
                                         .Period = period / 2.0};
    Nodalys::CircuitBuilder builder;
    builder.AddVsine("V1", SUPPLY, 0, {.Amplitude = amplitude, .Frequency = frequency});
    builder.AddThyristor("TR1", SUPPLY, LOAD, GATE, ideal);
    builder.AddResistor("R1", LOAD, 0, 100.0);
    builder.AddVpulse("Vg", GATE, LOAD, pulse);
    auto circuit = builder.Build();
    REQUIRE(circuit.has_value());
    const Nodalys::TransientResult result = RunTransient(*circuit, 2.0 * period, 20e-6);
    // Before the first firing the load is dark; at the peaks of the second period it follows the sine both ways
    CHECK_THAT(LoadVoltageAt(result, 1e-3), WithinAbs(0.0, 1e-3));
    CHECK_THAT(LoadVoltageAt(result, period + period / 4.0), WithinAbs(amplitude, 0.05));
    CHECK_THAT(LoadVoltageAt(result, period + 3.0 * period / 4.0), WithinAbs(-amplitude, 0.05));
}

TEST_CASE("A TRIAC fires on a fast main-terminal voltage rise and on a fast fall", "[nodalys][thyristor]") {
    Nodalys::ThyristorParameters sensitive = TRIAC;
    sensitive.CriticalDvDt = 5e5;
    const Nodalys::PulseParameters silent_gate{.High = 0.0, .Delay = 0.0, .Width = 1.0, .Period = 10.0};
    // The rise: 10 V in 1 us across a blocking TRIAC fires it the positive way
    const Nodalys::PulseParameters fast{.High = 10.0,
                                        .Delay = 0.5e-3,
                                        .RiseTime = 1e-6,
                                        .FallTime = 1e-6,
                                        .Width = 1e-3,
                                        .Period = 10.0};
    const Nodalys::TransientResult risen =
        RunTransient(BuildCommutatedLoop(sensitive, fast, 100.0, silent_gate), 1e-3, 1e-5);
    CHECK_THAT(LoadVoltageAt(risen, 0.6e-3), WithinAbs(9.099, 0.01));
    // The fall: a +10 V bias holds the TRIAC blocking; a fast -5 V pulse at 0.5 ms takes the supply to +5 V,
    // a fall of 5e6 V/s across the device, which fires it while the voltage stays positive
    const Nodalys::PulseParameters step_down{.High = -5.0,
                                             .Delay = 0.5e-3,
                                             .RiseTime = 1e-6,
                                             .FallTime = 1e-6,
                                             .Width = 1e-3,
                                             .Period = 10.0};
    Nodalys::CircuitBuilder builder;
    builder.AddVdc("Vbias", SUPPLY, BIAS, 10.0);
    builder.AddVpulse("Vstep", BIAS, 0, step_down);
    builder.AddThyristor("TR1", SUPPLY, LOAD, GATE, sensitive);
    builder.AddResistor("R1", LOAD, 0, 100.0);
    builder.AddVdc("Vg", GATE, LOAD, 0.0);
    auto circuit = builder.Build();
    REQUIRE(circuit.has_value());
    const Nodalys::TransientResult fallen = RunTransient(*circuit, 1e-3, 1e-5);
    CHECK_THAT(LoadVoltageAt(fallen, 0.4e-3), WithinAbs(0.0, 1e-3));
    // The fired device drops the on-state line from the +5 V supply: (5 - 0.9) * 100 / 100.01
    CHECK_THAT(LoadVoltageAt(fallen, 0.6e-3), WithinAbs(4.0996, 0.01));
}

TEST_CASE("A TRIAC re-fires on a voltage re-applied within the turn-off time, the negative way",
          "[nodalys][thyristor]") {
    Nodalys::ThyristorParameters slow_turn_off = TRIAC;
    slow_turn_off.TurnOffTime = 0.5e-3;
    // A 300 Ohm load: it holds 30 mA while the supply is up, and the mirrored on-state line drops to 3 mA, below
    // the holding current, once the supply collapses, which releases the latch at 0.8 ms; the window runs to 1.3 ms
    const Nodalys::PulseParameters gate{.High = 5.0, .Delay = 0.3e-3, .Width = 0.2e-3, .Period = 10.0};
    const Nodalys::PulseParameters supply{.High = -10.0,
                                          .Delay = 0.2e-3,
                                          .RiseTime = 1e-6,
                                          .FallTime = 1e-6,
                                          .Width = 0.6e-3,
                                          .Period = 1.0e-3};
    const Nodalys::TransientResult refired =
        RunTransient(BuildCommutatedLoop(slow_turn_off, supply, 300.0, gate), 2e-3, 1e-5);
    CHECK_THAT(LoadVoltageAt(refired, 0.5e-3), WithinAbs(-9.099, 0.01));
    CHECK_THAT(LoadVoltageAt(refired, 1.0e-3), WithinAbs(0.0, 0.05));
    // The supply returns at 1.2 ms, inside the window: the magnitude of the voltage rises back past the on-state
    // threshold and the device re-fires without a gate
    CHECK_THAT(LoadVoltageAt(refired, 1.25e-3), WithinAbs(-9.099, 0.01));
}

TEST_CASE("A conducting TRIAC follows its main-terminal voltage across zero onto the other half of its line",
          "[nodalys][thyristor]") {
    // The gate stays on, so the TRIAC conducts throughout while the supply swings from +10 V to -10 V in 1 us at
    // 0.5 ms. Newton starts the step after the swing from the positive on-state, where a line mirrored by the sign
    // of the voltage also has a solution with a small positive voltage and the current reversed
    const Nodalys::PulseParameters gate{.Low = 5.0, .High = 5.0, .Delay = 0.0, .Width = 1.0, .Period = 10.0};
    const Nodalys::PulseParameters supply{.Low = 10.0,
                                          .High = -10.0,
                                          .Delay = 0.5e-3,
                                          .RiseTime = 1e-6,
                                          .FallTime = 1e-6,
                                          .Width = 1.0,
                                          .Period = 10.0};
    const Nodalys::TransientResult result = RunTransient(BuildCommutatedLoop(TRIAC, supply, 100.0, gate), 1e-3, 1e-5);
    // The on-state line from each supply: (10 - 0.9) * 100 / 100.01 one way, the same negative the other
    CHECK_THAT(LoadVoltageAt(result, 0.4e-3), WithinAbs(9.099, 0.01));
    CHECK_THAT(LoadVoltageAt(result, 0.6e-3), WithinAbs(-9.099, 0.01));
    CHECK_THAT(LoadVoltageAt(result, 0.95e-3), WithinAbs(-9.099, 0.01));
}

TEST_CASE("A DIAC breaks over at its breakover voltage, snaps back and releases below its holding current",
          "[nodalys][thyristor][diac]") {
    // A relaxation oscillator: 50 V charges 100 nF through 100 kOhm, and the DIAC across the capacitor discharges it
    // into 100 Ohm each time its voltage reaches the 32 V breakover. The DIAC snaps back to the 27 V on-state line,
    // the capacitor falls until the current drops below 1 mA, and the ramp starts again. The supply steps up from
    // 0 V, since the oscillator has no operating point to start from
    constexpr int OSCILLATOR_SUPPLY = 1;
    constexpr int OSCILLATOR_CAPACITOR = 2;
    constexpr int OSCILLATOR_DISCHARGE = 3;
    const Nodalys::PulseParameters step{.Low = 0.0, .High = 50.0, .Delay = 0.0, .Width = 1.0, .Period = 10.0};
    Nodalys::CircuitBuilder builder;
    builder.AddVpulse("V1", OSCILLATOR_SUPPLY, 0, step);
    builder.AddResistor("R1", OSCILLATOR_SUPPLY, OSCILLATOR_CAPACITOR, 100e3);
    builder.AddCapacitor("C1", OSCILLATOR_CAPACITOR, 0, 100e-9);
    builder.AddDiac("DIAC1", OSCILLATOR_CAPACITOR, OSCILLATOR_DISCHARGE, Nodalys::DiacParameters{});
    builder.AddResistor("R2", OSCILLATOR_DISCHARGE, 0, 100.0);
    auto circuit = builder.Build();
    REQUIRE(circuit.has_value());
    const Nodalys::TransientResult result = RunTransient(*circuit, 20e-3, 1e-6);
    const std::vector<double> &voltages = result.NodeVoltages[OSCILLATOR_CAPACITOR];
    // The capacitor never charges past the breakover, and each discharge stops near the on-state threshold plus
    // the drop of the holding current across both resistances
    const auto [lowest, highest] = std::ranges::minmax(
        std::vector<double>(voltages.begin() + static_cast<std::ptrdiff_t>(voltages.size() / 2), voltages.end()));
    CHECK_THAT(highest, WithinAbs(32.0, 0.1));
    CHECK_THAT(lowest, WithinAbs(27.0, 0.2));
    // It fires more than once: the discharge resistor sees a pulse at each breakover
    const std::vector<double> &current = CurrentTrace(result, "DIAC1");
    int pulses = 0;
    for (std::size_t i = 1; i < current.size(); ++i) {
        if (current[i] > 10e-3 && current[i - 1] <= 10e-3) {
            ++pulses;
        }
    }
    CHECK(pulses >= 3);
}

TEST_CASE("A DIAC fires a TRIAC at the same angle of each half-cycle of a light dimmer", "[nodalys][thyristor][diac]") {
    // A 170 V, 60 Hz line feeds a 100 Ohm lamp through a TRIAC; R2 and C1 across the TRIAC time the firing, and the
    // DIAC from C1 to the gate fires it when C1 reaches the 32 V breakover
    constexpr int DIMMER_LINE = 1;
    constexpr int DIMMER_LAMP = 2;
    constexpr int DIMMER_TIMING = 3;
    constexpr int DIMMER_GATE = 4;
    Nodalys::CircuitBuilder builder;
    builder.AddVsine("V1", DIMMER_LINE, 0, {.Amplitude = 170.0, .Frequency = 60.0});
    builder.AddThyristor("TR1", DIMMER_LINE, DIMMER_LAMP, DIMMER_GATE, TRIAC);
    builder.AddResistor("R1", DIMMER_LAMP, 0, 100.0);
    builder.AddResistor("R2", DIMMER_LINE, DIMMER_TIMING, 47e3);
    builder.AddCapacitor("C1", DIMMER_TIMING, DIMMER_LAMP, 100e-9);
    builder.AddDiac("DIAC1", DIMMER_TIMING, DIMMER_GATE, Nodalys::DiacParameters{});
    auto circuit = builder.Build();
    REQUIRE(circuit.has_value());
    const Nodalys::TransientResult result = RunTransient(*circuit, 50e-3, 10e-6);
    // The firing delay from the start of a half-cycle: the first point where the lamp takes the line, past 10 V
    const auto firing_delay = [&result](const double start, const double sign) {
        for (std::size_t i = 0; i < result.Times.size(); ++i) {
            if (result.Times[i] >= start && sign * result.NodeVoltages[DIMMER_LAMP][i] > 10.0) {
                return result.Times[i] - start;
            }
        }
        FAIL("the TRIAC never fired after t = " << start);
        return 0.0;
    };
    constexpr double HALF_PERIOD = 1.0 / 120.0;
    // In the second period both half-cycles fire at the same delay, well inside the half-cycle
    const double positive = firing_delay(2.0 * HALF_PERIOD, 1.0);
    const double negative = firing_delay(3.0 * HALF_PERIOD, -1.0);
    CHECK(positive > 1e-3);
    CHECK(positive < HALF_PERIOD - 1e-3);
    CHECK_THAT(negative, WithinAbs(positive, 0.2e-3));
}
