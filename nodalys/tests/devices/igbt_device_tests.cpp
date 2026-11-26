/**
 * @file    igbt_device_tests.cpp
 * @brief   Tests of the IGBT of Nodalys: the on-state line, the gate threshold, the saturated channel, the Miller
 *          plateau, the tail current of the turn-off, the co-packaged diode, the harmonic balance, the convergence of
 *          its operating point and the figures it refuses.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <nodalys/nodalys.h>
#include <string_view>
#include <vector>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

/// The default figures: VGE(th) 5.5 V, gfs 10 S at 20 A (K = 2.5 A/V2), on-state 0.8 V + 40 mOhm.
constexpr Nodalys::IgbtParameters IGBT = {};
/// Node of the supply, of the collector and of the gate drive.
constexpr int SUPPLY = 1;
constexpr int COLLECTOR = 2;
constexpr int GATE = 3;
/// Node of the gate source behind its gate resistor.
constexpr int DRIVE = 4;

/// A supply feeding the collector through a load, the emitter grounded and the gate held by a DC source.
Nodalys::Circuit BuildSwitch(const Nodalys::IgbtParameters &params,
                             const double supply_volts,
                             const double load_ohms,
                             const double gate_volts) {
    Nodalys::CircuitBuilder builder;
    builder.AddVdc("V1", SUPPLY, 0, supply_volts);
    builder.AddResistor("R1", SUPPLY, COLLECTOR, load_ohms);
    builder.AddIgbt("Q1", COLLECTOR, GATE, 0, params);
    builder.AddVdc("Vg", GATE, 0, gate_volts);
    auto circuit = builder.Build();
    REQUIRE(circuit.has_value());
    return std::move(*circuit);
}

/// The same switch with its gate driven by a pulse through a gate resistor.
Nodalys::Circuit BuildPulsedSwitch(const Nodalys::IgbtParameters &params,
                                   const double supply_volts,
                                   const double load_ohms,
                                   const double gate_ohms,
                                   const Nodalys::PulseParameters &gate_pulse) {
    Nodalys::CircuitBuilder builder;
    builder.AddVdc("V1", SUPPLY, 0, supply_volts);
    builder.AddResistor("R1", SUPPLY, COLLECTOR, load_ohms);
    builder.AddIgbt("Q1", COLLECTOR, GATE, 0, params);
    builder.AddResistor("Rg", DRIVE, GATE, gate_ohms);
    builder.AddVpulse("Vg", DRIVE, 0, gate_pulse);
    auto circuit = builder.Build();
    REQUIRE(circuit.has_value());
    return std::move(*circuit);
}

Nodalys::OperatingPointResult RunOperatingPoint(const Nodalys::Circuit &circuit) {
    auto result = Nodalys::ComputeOperatingPoint(circuit);
    if (!result.has_value()) {
        FAIL("operating point failed: " << result.error());
    }
    return std::move(*result);
}

Nodalys::TransientResult RunTransient(const Nodalys::Circuit &circuit, const double stop_time, const double step) {
    const Nodalys::TransientConfig config{.StopTime = stop_time, .TimeStep = step, .MaxStep = step};
    auto result = Nodalys::ComputeTransient(circuit, config);
    if (!result.has_value()) {
        FAIL("transient failed: " << result.error());
    }
    return std::move(*result);
}

double OperatingCurrent(const Nodalys::OperatingPointResult &result, const std::string_view name) {
    const auto current = std::ranges::find(result.Currents, name, &Nodalys::ComponentCurrent::Name);
    REQUIRE(current != result.Currents.end());
    return current->Current;
}

const std::vector<double> &TransientCurrent(const Nodalys::TransientResult &result, const std::string_view name) {
    const auto current = std::ranges::find(result.Currents, name, &Nodalys::ComponentTrace::Name);
    REQUIRE(current != result.Currents.end());
    return current->Values;
}

/// The charge a current carries from an instant to the end of the run, by the trapezoid over the points reported.
double ChargeAfter(const Nodalys::TransientResult &result, const std::vector<double> &current, const double from) {
    double charge = 0.0;
    for (std::size_t i = 1; i < result.Times.size(); ++i) {
        if (result.Times[i - 1] >= from) {
            charge += 0.5 * (current[i] + current[i - 1]) * (result.Times[i] - result.Times[i - 1]);
        }
    }
    return charge;
}

} // namespace

TEST_CASE("An IGBT driven hard on drops the on-state line of its datasheet", "[nodalys][igbt]") {
    // 20 V through 1.88 Ohm leaves about 10 A, which the line drops as 0.8 V + 40 mOhm * 10 A = 1.2 V
    const Nodalys::OperatingPointResult result = RunOperatingPoint(BuildSwitch(IGBT, 20.0, 1.88, 15.0));
    const double collector_current = OperatingCurrent(result, "Q1.C");
    CHECK_THAT(collector_current, WithinRel(10.0, 0.02));
    const double expected = IGBT.OnThresholdVoltage + IGBT.OnResistance * collector_current;
    CHECK_THAT(result.NodeVoltages[COLLECTOR], WithinAbs(expected, 0.01));
    // The emitter carries the same current out, and the gate draws only its leakage
    CHECK_THAT(OperatingCurrent(result, "Q1.E"), WithinAbs(-collector_current, 1e-6));
    CHECK(std::fabs(OperatingCurrent(result, "Q1.G")) < 1e-6);
}

TEST_CASE("An IGBT below its gate threshold blocks the supply", "[nodalys][igbt]") {
    const Nodalys::OperatingPointResult result = RunOperatingPoint(BuildSwitch(IGBT, 100.0, 10.0, 4.5));
    CHECK(OperatingCurrent(result, "Q1.C") < 1e-6);
    CHECK_THAT(result.NodeVoltages[COLLECTOR], WithinAbs(100.0, 1e-3));
}

TEST_CASE("An IGBT whose gate is just above threshold saturates at the channel current", "[nodalys][igbt]") {
    // One volt of overdrive lets K / 2 = 1.25 A through, far less than the 20 A the load would take
    const Nodalys::OperatingPointResult result = RunOperatingPoint(BuildSwitch(IGBT, 20.0, 1.0, 6.5));
    const double channel = 0.5 * IGBT.Transconductance * IGBT.Transconductance / (2.0 * IGBT.TransconductanceCurrent);
    CHECK_THAT(OperatingCurrent(result, "Q1.C"), WithinRel(channel, 0.02));
    CHECK(result.NodeVoltages[COLLECTOR] > 15.0);
}

TEST_CASE("The gate of an IGBT holds at the Miller plateau while its collector falls", "[nodalys][igbt]") {
    // A 1 nF Cres slowed by a 100 Ohm gate resistor: while the collector swings, the gate sits where the channel
    // carries the load current, VGE(th) + sqrt(2 I / K)
    Nodalys::IgbtParameters params = IGBT;
    params.InputCapacitance = 3e-9;
    params.ReverseTransferCapacitance = 1e-9;
    params.OutputCapacitance = 1.2e-9;
    const Nodalys::PulseParameters pulse{.Low = 0.0,
                                         .High = 15.0,
                                         .Delay = 0.1e-6,
                                         .RiseTime = 1e-9,
                                         .FallTime = 1e-9,
                                         .Width = 10e-6,
                                         .Period = 20e-6};
    const Nodalys::TransientResult result =
        RunTransient(BuildPulsedSwitch(params, 100.0, 10.0, 100.0, pulse), 5e-6, 5e-9);
    const std::vector<double> &collector = result.NodeVoltages[COLLECTOR];
    const auto halfway = std::ranges::find_if(collector, [](const double volts) { return volts < 50.0; });
    REQUIRE(halfway != collector.end());
    const auto index = static_cast<std::size_t>(halfway - collector.begin());
    const double load_current = 100.0 / 10.0 / 2.0;
    const double gain = params.Transconductance * params.Transconductance / (2.0 * params.TransconductanceCurrent);
    const double plateau = params.GateThresholdVoltage + std::sqrt(2.0 * load_current / gain);
    INFO("gate at " << result.NodeVoltages[GATE][index] << " V when the collector crosses 50 V, at "
                    << result.Times[index] << " s");
    CHECK_THAT(result.NodeVoltages[GATE][index], WithinAbs(plateau, 0.3));
}

TEST_CASE("An IGBT carries a tail current after its gate turns it off", "[nodalys][igbt]") {
    // 10 A through 2 Ohm, cut at 2 us: the tail carries a fifth of the current, decaying with 200 ns, which is
    // 0.2 * 10 A * 200 ns = 0.4 uC more than the same part without a tail
    const Nodalys::PulseParameters pulse{.Low = 0.0,
                                         .High = 15.0,
                                         .Delay = 0.0,
                                         .RiseTime = 10e-9,
                                         .FallTime = 10e-9,
                                         .Width = 2e-6,
                                         .Period = 10e-6};
    Nodalys::IgbtParameters without_tail = IGBT;
    without_tail.TailFraction = 0.0;
    const Nodalys::TransientResult tailed = RunTransient(BuildPulsedSwitch(IGBT, 21.2, 2.0, 1.0, pulse), 4e-6, 5e-9);
    const Nodalys::TransientResult clean =
        RunTransient(BuildPulsedSwitch(without_tail, 21.2, 2.0, 1.0, pulse), 4e-6, 5e-9);
    const double tail_charge = ChargeAfter(tailed, TransientCurrent(tailed, "Q1.C"), 2.0e-6)
                             - ChargeAfter(clean, TransientCurrent(clean, "Q1.C"), 2.0e-6);
    const double expected = IGBT.TailFraction * 10.0 * IGBT.TailTimeConstant;
    INFO("tail charge " << tail_charge << " C against " << expected << " C");
    CHECK_THAT(tail_charge, WithinRel(expected, 0.25));
    // Long after the tail has decayed, the device blocks
    CHECK(std::fabs(TransientCurrent(tailed, "Q1.C").back()) < 1e-3);
}

TEST_CASE("The diode of a co-packaged IGBT conducts from emitter to collector", "[nodalys][igbt]") {
    Nodalys::IgbtParameters with_diode = IGBT;
    with_diode.AntiParallelDiode = Nodalys::DiodeParameters{.SaturationCurrent = 1e-9,
                                                            .EmissionCoefficient = 1.5,
                                                            .SeriesResistance = 0.02,
                                                            .BreakdownVoltage = 1e4};
    // A reversed supply drives about 1 A backward: the diode carries it, the bare IGBT blocks it
    const Nodalys::OperatingPointResult diode = RunOperatingPoint(BuildSwitch(with_diode, -10.0, 10.0, 0.0));
    CHECK_THAT(OperatingCurrent(diode, "Q1.C"), WithinRel(-0.92, 0.03));
    CHECK(diode.NodeVoltages[COLLECTOR] < -0.5);
    const Nodalys::OperatingPointResult bare = RunOperatingPoint(BuildSwitch(IGBT, -10.0, 10.0, 0.0));
    CHECK(std::fabs(OperatingCurrent(bare, "Q1.C")) < 1e-6);
}

TEST_CASE("An IGBT runs in a harmonic balance, since its conduction is smooth", "[nodalys][igbt]") {
    Nodalys::CircuitBuilder builder;
    builder.AddVsine("V1", SUPPLY, 0, {.Amplitude = 5.0, .Frequency = 1e3, .Offset = 10.0});
    builder.AddResistor("R1", SUPPLY, COLLECTOR, 5.0);
    builder.AddIgbt("Q1", COLLECTOR, GATE, 0, IGBT);
    builder.AddVdc("Vg", GATE, 0, 15.0);
    const auto circuit = builder.Build();
    REQUIRE(circuit.has_value());
    const auto result = Nodalys::ComputeHarmonicBalance(*circuit, {.FundamentalFrequency = 1e3, .Harmonics = 4});
    if (!result.has_value()) {
        FAIL("harmonic balance failed: " << result.error());
    }
    // The DC collector current is that of the mean supply through the load and the on-state line
    const double mean_current = (10.0 - IGBT.OnThresholdVoltage) / (5.0 + IGBT.OnResistance);
    CHECK_THAT(std::abs(result->NodePhasors[COLLECTOR][0]), WithinRel(10.0 - 5.0 * mean_current, 0.05));
}

TEST_CASE("Figures an IGBT cannot have are refused when the circuit is built", "[nodalys][igbt]") {
    Nodalys::IgbtParameters smaller_input = IGBT;
    smaller_input.InputCapacitance = 10e-12;
    Nodalys::IgbtParameters long_tail = IGBT;
    long_tail.TailFraction = 1.5;
    for (const Nodalys::IgbtParameters &params : {smaller_input, long_tail}) {
        Nodalys::CircuitBuilder builder;
        builder.AddVdc("V1", SUPPLY, 0, 10.0);
        builder.AddIgbt("Q1", SUPPLY, GATE, 0, params);
        builder.AddVdc("Vg", GATE, 0, 15.0);
        const auto circuit = builder.Build();
        REQUIRE_FALSE(circuit.has_value());
        CHECK(circuit.error().contains("Q1"));
    }
}

TEST_CASE("An IGBT finds its operating point at any supply, load and gate drive", "[nodalys][igbt]") {
    // The knee of the on-state line and the saturated channel once threw Newton back and forth; the near-threshold
    // gates and the reverse supplies, where the diode takes over, were the hard ones
    Nodalys::IgbtParameters with_diode = IGBT;
    with_diode.AntiParallelDiode = Nodalys::DiodeParameters{.SaturationCurrent = 1e-9,
                                                            .EmissionCoefficient = 1.5,
                                                            .SeriesResistance = 0.02,
                                                            .BreakdownVoltage = 1e4};
    for (const Nodalys::IgbtParameters &params : {IGBT, with_diode}) {
        for (const double supply : {-600.0, 1.0, 5.0, 100.0, 600.0}) {
            for (const double load : {1000.0, 10.0, 0.1}) {
                for (const double gate : {0.0, 5.5, 5.8, 6.5, 10.0, 20.0}) {
                    CAPTURE(supply, load, gate, params.AntiParallelDiode.has_value());
                    CHECK(Nodalys::ComputeOperatingPoint(BuildSwitch(params, supply, load, gate)).has_value());
                }
            }
        }
    }
}
