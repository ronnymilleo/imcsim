/**
 * @file    thyristor_tests.cpp
 * @brief   Tests for the thyristor (SCR) and TRIAC: their figures, their netlists, and the controlled rectifier and
 *          light dimmer they drive on ngspice and on Nodalys.
 */

#include "circuit/circuit.h"
#include "circuit/circuit_currents.h"
#include "circuit/components/ground.h"
#include "circuit/components/resistor.h"
#include "circuit/components/thyristor.h"
#include "circuit/components/voltage_source.h"
#include "nodalys_in_use.h"
#include "simulation/simulator.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

using Catch::Matchers::WithinAbs;

namespace {

/// The sine the rectifier runs on: 10 V at 50 Hz.
constexpr double AMPLITUDE = 10.0;
constexpr double FREQUENCY = 50.0;
constexpr double PERIOD = 1.0 / FREQUENCY;
/// The firing angle of the rectifier, 60 degrees, and the gate pulse delay it asks.
constexpr double ANGLE = std::numbers::pi / 3.0;
constexpr double GATE_DELAY = ANGLE / (2.0 * std::numbers::pi * FREQUENCY);
/// The load of the rectifier, which keeps the anode current far above the holding current.
constexpr double LOAD_OHMS = 100.0;
/// The transient settings the rectifier runs with: two periods at a 50th of a period per point.
constexpr Core::TransientSettings RECTIFIER_SETTINGS = {.StopTime = 2.0 * PERIOD, .TimeStep = PERIOD / 50.0};

/// A half-wave controlled rectifier: a 50 Hz sine feeding the thyristor into a load, with a gate pulse train
/// referenced to the cathode firing at 60 degrees of every half-cycle.
struct HalfWaveRectifier {
    Core::VoltageSource Supply;
    Core::Thyristor Part;
    Core::Resistor Load;
    Core::VoltageSource Gate;
    Core::Ground Reference;
    Core::Circuit Circuit;

    explicit HalfWaveRectifier(const double load_ohms) {
        Supply.SetName("Vin1");
        Supply.SetSourceType(Core::VoltageSource::SourceType::AC);
        Supply.SetAC({.Amplitude = AMPLITUDE, .Frequency = FREQUENCY});
        Part.SetName("SCR1");
        Load.SetName("R1");
        Load.SetValue(load_ohms);
        Gate.SetName("Vgate1");
        Gate.SetSourceType(Core::VoltageSource::SourceType::PULSE);
        Gate.SetPulse({.Low = 0.0,
                       .High = 5.0,
                       .Delay = GATE_DELAY,
                       .RiseTime = 1e-6,
                       .FallTime = 1e-6,
                       .Width = 0.5e-3,
                       .Period = PERIOD});
        Circuit.Add(Supply, {1, 0});
        Circuit.Add(Part, {1, 2, 3});
        Circuit.Add(Load, {2, 0});
        Circuit.Add(Gate, {3, 2});
        Circuit.Add(Reference, {0});
    }
};

/// The load voltage at the first time point at or after an instant.
double LoadVoltageAt(const Core::Transient &transient, const double time) {
    const auto point = std::ranges::lower_bound(transient.Times, time);
    REQUIRE(point != transient.Times.end());
    return transient.NodeVoltages[2][static_cast<std::size_t>(point - transient.Times.begin())];
}

/**
 * @brief   The mean of the load voltage over one steady-state period, from the second period on.
 * @details  The mean is the trapezoid over the points the engine itself reported, not the plain mean of their
 *           values: ngspice clusters its points where it takes small steps, in the conduction of the pair, so a
 *           plain mean would count those twice.
 */
double MeanLoadVoltage(const Core::Transient &transient) {
    const double from = PERIOD;
    const double to = 2.0 * PERIOD;
    double area = 0.0;
    for (std::size_t i = 0; i + 1 < transient.Times.size(); ++i) {
        const double t0 = transient.Times[i];
        const double t1 = transient.Times[i + 1];
        const double lo = std::max(t0, from);
        const double hi = std::min(t1, to);
        if (hi <= lo) {
            continue;
        }
        const double slope =
            t1 > t0 ? (transient.NodeVoltages[2][i + 1] - transient.NodeVoltages[2][i]) / (t1 - t0) : 0.0;
        const double v0 = transient.NodeVoltages[2][i] + slope * (lo - t0);
        const double v1 = transient.NodeVoltages[2][i] + slope * (hi - t0);
        area += 0.5 * (v0 + v1) * (hi - lo);
    }
    REQUIRE(area >= 0.0);
    return area / (to - from);
}

/// Runs the rectifier on the backend in use and returns its transient.
const Core::Transient &RunRectifier(const Core::Circuit &circuit, Core::TransientRun &run) {
    run = Core::RunTransient(circuit, RECTIFIER_SETTINGS);
    REQUIRE(run.Result.has_value());
    INFO("backend message: " << (run.Messages.empty() ? std::string("(none)") : run.Messages.front().Text));
    return *run.Result;
}

/// A light dimmer: a 50 Hz sine feeding the TRIAC into a load, with a gate pulse train referenced to MT1 firing at
/// 60 degrees of every half-cycle, so the load sees a full-wave chopped sine.
struct Dimmer {
    Core::VoltageSource Supply;
    Core::Thyristor Part;
    Core::Resistor Load;
    Core::VoltageSource Gate;
    Core::Ground Reference;
    Core::Circuit Circuit;

    explicit Dimmer(const double load_ohms) : Part(Core::ComponentType::TRIAC) {
        Supply.SetName("Vin1");
        Supply.SetSourceType(Core::VoltageSource::SourceType::AC);
        Supply.SetAC({.Amplitude = AMPLITUDE, .Frequency = FREQUENCY});
        Part.SetName("TR1");
        Load.SetName("R1");
        Load.SetValue(load_ohms);
        Gate.SetName("Vgate1");
        Gate.SetSourceType(Core::VoltageSource::SourceType::PULSE);
        Gate.SetPulse({.Low = 0.0,
                       .High = 5.0,
                       .Delay = GATE_DELAY,
                       .RiseTime = 1e-6,
                       .FallTime = 1e-6,
                       .Width = 0.5e-3,
                       .Period = PERIOD / 2.0});
        Circuit.Add(Supply, {1, 0});
        Circuit.Add(Part, {1, 2, 3});
        Circuit.Add(Load, {2, 0});
        Circuit.Add(Gate, {3, 2});
        Circuit.Add(Reference, {0});
    }
};

} // namespace

TEST_CASE("A thyristor starts with the datasheet figures, and clones keep them", "[thyristor]") {
    Core::Thyristor part;
    CHECK(part.GetParameters() == Core::ThyristorParameters{});
    part.SetParameters({.OnThresholdVoltage = 1.1, .HoldingCurrent = 10e-3});
    const std::unique_ptr<Core::Component> copy = part.Clone();
    CHECK(static_cast<const Core::Thyristor &>(*copy).GetParameters().OnThresholdVoltage == 1.1);
    CHECK(static_cast<const Core::Thyristor &>(*copy).GetParameters().HoldingCurrent == 10e-3);

    CHECK(Core::Thyristor::IsValidParameters(Core::ThyristorParameters{}));
    CHECK_FALSE(Core::Thyristor::IsValidParameters({.OnResistance = 0.0}));
    CHECK_FALSE(Core::Thyristor::IsValidParameters({.OnResistance = 1e9}));
    CHECK_FALSE(Core::Thyristor::IsValidParameters({.HoldingCurrent = 0.0}));
    CHECK_FALSE(Core::Thyristor::IsValidParameters({.GateTriggerCurrent = 0.0}));
    CHECK_FALSE(Core::Thyristor::IsValidParameters({.GateResistance = 0.0}));
}

TEST_CASE("A thyristor reports the currents of its anode, cathode and gate", "[thyristor]") {
    Core::Thyristor part;
    part.SetName("SCR1");
    const std::vector<std::string> names = Core::GetCurrentNames(part);
    REQUIRE(names.size() == 3);
    CHECK(names[0] == "SCR1.A");
    CHECK(names[1] == "SCR1.K");
    CHECK(names[2] == "SCR1.G");
}

TEST_CASE("A thyristor writes one instance of its two-transistor subcircuit, however many there are", "[thyristor]") {
    HalfWaveRectifier rectifier(LOAD_OHMS);
    Core::Thyristor second;
    second.SetName("SCR2");
    rectifier.Circuit.Add(second, {4, 5, 6});

    const std::string netlist = rectifier.Circuit.ToSpiceNetlist();
    CHECK(netlist.contains("X-SCR1 1 2 3 imcsim_scr RG=100 RT=10m\n"));
    CHECK(netlist.contains("X-SCR2 4 5 6 imcsim_scr RG=100 RT=10m\n"));
    CHECK(netlist.find(".subckt imcsim_scr") == netlist.rfind(".subckt imcsim_scr"));
    CHECK(netlist.contains(".ends imcsim_scr\n"));
}

TEST_CASE("A half-wave controlled rectifier fires, latches and commutates on both engines", "[thyristor][simulator]") {
    HalfWaveRectifier rectifier(LOAD_OHMS);
    // The closed form of the mean over a period: conduction from the firing angle to the instant the anode current
    // falls to the holding current, dropping V0 on the way
    const double on_threshold = Core::ThyristorParameters{}.OnThresholdVoltage;
    const double release = std::numbers::pi - std::asin((on_threshold + 5e-3 * LOAD_OHMS) / AMPLITUDE);
    const double expected = (AMPLITUDE * (std::cos(ANGLE) - std::cos(release)) - on_threshold * (release - ANGLE))
                          / (2.0 * std::numbers::pi);

    Core::TransientRun ngspice_run;
    const Core::Transient &ngspice_transient = RunRectifier(rectifier.Circuit, ngspice_run);
    // At the peak of the second period the gate pulse is long over, so the latch holds without it
    CHECK(LoadVoltageAt(ngspice_transient, 25e-3) > 8.0);
    INFO("ngspice: mean " << MeanLoadVoltage(ngspice_transient) << " V, peak load "
                          << LoadVoltageAt(ngspice_transient, 25e-3) << " V, reverse load "
                          << LoadVoltageAt(ngspice_transient, 35e-3) << " V");
    CHECK_THAT(MeanLoadVoltage(ngspice_transient), WithinAbs(expected, 0.1));

    TestSupport::NodalysInUse nodalys;
    Core::TransientRun nodalys_run;
    const Core::Transient &nodalys_transient = RunRectifier(rectifier.Circuit, nodalys_run);
    CHECK(LoadVoltageAt(nodalys_transient, 25e-3) > 8.0);
    INFO("Nodalys: mean " << MeanLoadVoltage(nodalys_transient) << " V, peak load "
                          << LoadVoltageAt(nodalys_transient, 25e-3) << " V, reverse load "
                          << LoadVoltageAt(nodalys_transient, 35e-3) << " V");
    INFO("closed form " << expected << " V");
    CHECK_THAT(MeanLoadVoltage(nodalys_transient), WithinAbs(expected, 0.1));
    // The pair of the subcircuit and the latching device of Nodalys agree on the rectification
    CHECK_THAT(MeanLoadVoltage(nodalys_transient), WithinAbs(MeanLoadVoltage(ngspice_transient), 0.05));
}

TEST_CASE("A TRIAC starts with the datasheet figures, and clones keep them", "[thyristor]") {
    Core::Thyristor part(Core::ComponentType::TRIAC);
    CHECK(part.GetType() == Core::ComponentType::TRIAC);
    CHECK(part.GetParameters().IsBidirectional);
    CHECK(part.GetParameters() == Core::ThyristorParameters{.IsBidirectional = true});
    // The editor cannot turn a TRIAC into an SCR: the part type fixes the direction
    part.SetParameters({.OnThresholdVoltage = 1.1, .HoldingCurrent = 10e-3});
    CHECK(part.GetParameters().IsBidirectional);
    const std::unique_ptr<Core::Component> copy = part.Clone();
    CHECK(copy->GetType() == Core::ComponentType::TRIAC);
    CHECK(static_cast<const Core::Thyristor &>(*copy).GetParameters().OnThresholdVoltage == 1.1);
    CHECK(static_cast<const Core::Thyristor &>(*copy).GetParameters().IsBidirectional);

    Core::Thyristor scr;
    CHECK_FALSE(scr.GetParameters().IsBidirectional);
    scr.SetParameters({.IsBidirectional = true});
    CHECK_FALSE(scr.GetParameters().IsBidirectional);
}

TEST_CASE("A TRIAC reports the currents of MT2, MT1 and gate", "[thyristor]") {
    Core::Thyristor part(Core::ComponentType::TRIAC);
    part.SetName("TR1");
    const std::vector<std::string> names = Core::GetCurrentNames(part);
    REQUIRE(names.size() == 3);
    CHECK(names[0] == "TR1.MT2");
    CHECK(names[1] == "TR1.MT1");
    CHECK(names[2] == "TR1.G");
}

TEST_CASE("A TRIAC writes one instance of its anti-parallel subcircuit, however many there are", "[thyristor]") {
    Dimmer dimmer(LOAD_OHMS);
    Core::Thyristor second(Core::ComponentType::TRIAC);
    second.SetName("TR2");
    dimmer.Circuit.Add(second, {4, 5, 6});

    const std::string netlist = dimmer.Circuit.ToSpiceNetlist();
    CHECK(netlist.contains("X-TR1 1 2 3 imcsim_triac RG=100 RT=10m\n"));
    CHECK(netlist.contains("X-TR2 4 5 6 imcsim_triac RG=100 RT=10m\n"));
    CHECK(netlist.find(".subckt imcsim_triac") == netlist.rfind(".subckt imcsim_triac"));
    CHECK(netlist.contains(".ends imcsim_triac\n"));
}

TEST_CASE("A TRIAC dimmer chops both half-cycles on both engines", "[thyristor][simulator]") {
    Dimmer dimmer(LOAD_OHMS);

    Core::TransientRun ngspice_run;
    const Core::Transient &ngspice_transient = RunRectifier(dimmer.Circuit, ngspice_run);
    // Before the first firing the load is dark; at the peaks of the second period it follows the sine both ways
    CHECK(LoadVoltageAt(ngspice_transient, 1e-3) < 0.1);
    CHECK(LoadVoltageAt(ngspice_transient, 25e-3) > 8.0);
    CHECK(LoadVoltageAt(ngspice_transient, 35e-3) < -8.0);
    INFO("ngspice: positive peak " << LoadVoltageAt(ngspice_transient, 25e-3) << " V, negative peak "
                                   << LoadVoltageAt(ngspice_transient, 35e-3) << " V");

    TestSupport::NodalysInUse nodalys;
    Core::TransientRun nodalys_run;
    const Core::Transient &nodalys_transient = RunRectifier(dimmer.Circuit, nodalys_run);
    CHECK(LoadVoltageAt(nodalys_transient, 1e-3) < 0.1);
    CHECK(LoadVoltageAt(nodalys_transient, 25e-3) > 8.0);
    CHECK(LoadVoltageAt(nodalys_transient, 35e-3) < -8.0);
    INFO("Nodalys: positive peak " << LoadVoltageAt(nodalys_transient, 25e-3) << " V, negative peak "
                                   << LoadVoltageAt(nodalys_transient, 35e-3) << " V");
    // The anti-parallel pair of the subcircuit and the bidirectional device of Nodalys agree on the peaks
    CHECK_THAT(LoadVoltageAt(nodalys_transient, 25e-3), WithinAbs(LoadVoltageAt(ngspice_transient, 25e-3), 0.1));
    CHECK_THAT(LoadVoltageAt(nodalys_transient, 35e-3), WithinAbs(LoadVoltageAt(ngspice_transient, 35e-3), 0.1));
}
