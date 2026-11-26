/**
 * @file    thyristor_tests.cpp
 * @brief   Tests for the thyristor (SCR) and TRIAC: their figures, their netlists, the controlled rectifier they drive
 *          on ngspice and on Nodalys and the light dimmer on Nodalys, and the warnings of a run past their ratings.
 */

#include "backends/ngspice/ngspice_backend.h"
#include "circuit/circuit.h"
#include "circuit/circuit_currents.h"
#include "circuit/components/ground.h"
#include "circuit/components/resistor.h"
#include "circuit/components/thyristor.h"
#include "circuit/components/voltage_source.h"
#include "nodalys_in_use.h"
#include "simulation/simulation_backend.h"
#include "simulation/simulator.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
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

/// The peak of a 230 V mains sine, which a part on the line must block in both directions.
constexpr double MAINS_PEAK = 325.0;
/// The faster mains frequency, whose sine rises the steeper through zero.
constexpr double MAINS_FREQUENCY = 60.0;
/// The largest load current an idle part may let through: its leakage stays at microamperes.
constexpr double IDLE_CURRENT_LIMIT = 1e-3;

/// A thyristor or TRIAC left idle on the mains: the sine feeds it through a load, its gate tied to the cathode or MT1
/// through a resistor, so nothing should fire it.
struct IdleOnMains {
    Core::VoltageSource Supply;
    Core::Resistor Load;
    Core::Thyristor Part;
    Core::Resistor GateTie;
    Core::Ground Reference;
    Core::Circuit Circuit;

    explicit IdleOnMains(const Core::ComponentType type) : Part(type) {
        Supply.SetName("Vin1");
        Supply.SetSourceType(Core::VoltageSource::SourceType::AC);
        Supply.SetAC({.Amplitude = MAINS_PEAK, .Frequency = MAINS_FREQUENCY});
        Load.SetName("R1");
        Load.SetValue(LOAD_OHMS);
        Part.SetName("TR1");
        GateTie.SetName("R2");
        GateTie.SetValue(1e3);
        Circuit.Add(Supply, {1, 0});
        Circuit.Add(Load, {1, 2});
        Circuit.Add(Part, {2, 0, 3});
        Circuit.Add(GateTie, {3, 0});
        Circuit.Add(Reference, {0});
    }
};

/// The largest load current over two mains periods on the backend in use.
double PeakIdleCurrent(const Core::Circuit &circuit) {
    const Core::TransientRun run =
        Core::RunTransient(circuit, {.StopTime = 2.0 / MAINS_FREQUENCY, .TimeStep = 1.0 / MAINS_FREQUENCY / 200.0});
    REQUIRE(run.Result.has_value());
    const Core::Transient &transient = *run.Result;
    double peak = 0.0;
    for (std::size_t i = 0; i < transient.Times.size(); ++i) {
        peak = std::max(peak, std::abs(transient.NodeVoltages[1][i] - transient.NodeVoltages[2][i]) / LOAD_OHMS);
    }
    return peak;
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

/// The ratings a run of the rectifier or the dimmer stays well within, so none of them warns.
constexpr Core::ThyristorParameters GENEROUS_RATINGS = {.MaxRepetitivePeakVoltage = 50.0,
                                                        .MaxOnStateCurrent = 1.0,
                                                        .MaxSurgeCurrent = 1.0,
                                                        .MaxI2t = 1.0};

/// A thyristor latched on by a steady gate drive, carrying a direct current through a load from the first instant.
struct LatchedOnDC {
    Core::VoltageSource Supply;
    Core::Resistor Load;
    Core::Thyristor Part;
    Core::VoltageSource Gate;
    Core::Ground Reference;
    Core::Circuit Circuit;

    explicit LatchedOnDC(const double load_ohms) {
        Supply.SetName("Vin1");
        Supply.SetValue(AMPLITUDE);
        Load.SetName("R1");
        Load.SetValue(load_ohms);
        Part.SetName("SCR1");
        Gate.SetName("Vgate1");
        Gate.SetValue(5.0);
        Circuit.Add(Supply, {1, 0});
        Circuit.Add(Load, {1, 2});
        Circuit.Add(Part, {2, 0, 3});
        Circuit.Add(Gate, {3, 0});
        Circuit.Add(Reference, {0});
    }
};

/// True when some warning starts with the text, so a test can check one rating at a time.
bool HasWarning(const std::vector<std::string> &warnings, const std::string_view start) {
    return std::ranges::any_of(warnings, [&](const std::string &warning) { return warning.starts_with(start); });
}

/// True when some warning says a part exceeds a rating.
bool HasRatingWarning(const std::vector<std::string> &warnings) {
    return std::ranges::any_of(warnings, [](const std::string &warning) { return warning.contains("exceeds its"); });
}

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
    CHECK_FALSE(Core::Thyristor::IsValidParameters({.CriticalDvDt = -1.0}));
    CHECK_FALSE(Core::Thyristor::IsValidParameters({.TurnOffTime = -1.0}));
    // The dv/dt and turn-off figures are kept like the rest, and zero disables each
    part.SetParameters({.CriticalDvDt = 2e6, .TurnOffTime = 60e-6});
    CHECK(part.GetParameters().CriticalDvDt == 2e6);
    CHECK(part.GetParameters().TurnOffTime == 60e-6);
    CHECK(Core::Thyristor::IsValidParameters(part.GetParameters()));
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

TEST_CASE("A TRIAC dimmer chops both half-cycles on Nodalys", "[thyristor][simulator]") {
    Dimmer dimmer(LOAD_OHMS);
    const TestSupport::NodalysInUse nodalys;
    Core::TransientRun run;
    const Core::Transient &transient = RunRectifier(dimmer.Circuit, run);
    // Before the first firing the load is dark; at the peaks of the second period it follows the sine both ways
    INFO("positive peak " << LoadVoltageAt(transient, 25e-3) << " V, negative peak " << LoadVoltageAt(transient, 35e-3)
                          << " V");
    CHECK(LoadVoltageAt(transient, 1e-3) < 0.1);
    CHECK(LoadVoltageAt(transient, 25e-3) > 8.0);
    CHECK(LoadVoltageAt(transient, 35e-3) < -8.0);
}

TEST_CASE("ngspice refuses a TRIAC with the reason, and Nodalys runs it", "[thyristor][simulation_backend]") {
    const Core::NgspiceBackend ngspice;
    const std::optional<std::string> refusal =
        Core::FindUnsupported(ngspice, {Core::ComponentType::TRIAC}, Core::AnalysisKind::TRANSIENT);
    REQUIRE(refusal.has_value());
    CHECK(refusal->contains("Nodalys"));
    // The SCR stays on both engines
    CHECK_FALSE(
        Core::FindUnsupported(ngspice, {Core::ComponentType::THYRISTOR}, Core::AnalysisKind::TRANSIENT).has_value());

    const TestSupport::NodalysInUse nodalys;
    CHECK_FALSE(Core::FindUnsupported(Core::GetBackend(), {Core::ComponentType::TRIAC}, Core::AnalysisKind::TRANSIENT)
                    .has_value());
}

TEST_CASE("A thyristor left idle on the mains blocks both half-cycles on both engines, and a TRIAC on Nodalys",
          "[thyristor][simulator]") {
    // The ngspice subcircuit once fired by itself here, through the displacement current of the collector junctions,
    // which the steep mains sine drove into the bases
    const IdleOnMains thyristor(Core::ComponentType::THYRISTOR);
    CHECK(PeakIdleCurrent(thyristor.Circuit) < IDLE_CURRENT_LIMIT);

    const TestSupport::NodalysInUse nodalys;
    CHECK(PeakIdleCurrent(thyristor.Circuit) < IDLE_CURRENT_LIMIT);
    const IdleOnMains triac(Core::ComponentType::TRIAC);
    CHECK(PeakIdleCurrent(triac.Circuit) < IDLE_CURRENT_LIMIT);
}

TEST_CASE("A thyristor past its voltage, current, surge or I2t rating is reported on both engines",
          "[thyristor][simulator][ratings]") {
    HalfWaveRectifier rectifier(LOAD_OHMS);
    // The rectifier blocks a 10 V peak, averages about 22 mA forward and peaks near 92 mA; a half-cycle of conduction
    // from 60 degrees heats it by about 34 uA2s
    const Core::ThyristorParameters tight = {.MaxRepetitivePeakVoltage = 5.0,
                                             .MaxOnStateCurrent = 10e-3,
                                             .MaxSurgeCurrent = 50e-3,
                                             .MaxI2t = 10e-6};
    const auto check_engine = [&] {
        rectifier.Part.SetParameters({});
        Core::TransientRun unrated;
        // A part that sets no rating is not checked
        CHECK_FALSE(HasRatingWarning(RunRectifier(rectifier.Circuit, unrated).Warnings));

        rectifier.Part.SetParameters(GENEROUS_RATINGS);
        Core::TransientRun within;
        CHECK_FALSE(HasRatingWarning(RunRectifier(rectifier.Circuit, within).Warnings));

        rectifier.Part.SetParameters(tight);
        Core::TransientRun past;
        const std::vector<std::string> &warnings = RunRectifier(rectifier.Circuit, past).Warnings;
        CAPTURE(warnings);
        CHECK(HasWarning(warnings, "SCR1 exceeds its VDRM/VRRM rating: "));
        CHECK(HasWarning(warnings, "SCR1 exceeds its IT(AV) rating: "));
        CHECK(HasWarning(warnings, "SCR1 exceeds its ITSM rating: "));
        CHECK(HasWarning(warnings, "SCR1 exceeds its I2t rating: "));
    };
    SECTION("ngspice") {
        check_engine();
    }
    SECTION("Nodalys") {
        TestSupport::NodalysInUse nodalys;
        check_engine();
    }
}

TEST_CASE("A TRIAC is judged on its RMS current, since its average is near zero", "[thyristor][simulator][ratings]") {
    // The chopped sine through 100 Ohm averages near zero but has about 63 mA RMS
    const TestSupport::NodalysInUse nodalys;
    Dimmer dimmer(LOAD_OHMS);
    dimmer.Part.SetParameters({.MaxOnStateCurrent = 0.1});
    Core::TransientRun within;
    CHECK_FALSE(HasRatingWarning(RunRectifier(dimmer.Circuit, within).Warnings));

    dimmer.Part.SetParameters({.MaxOnStateCurrent = 30e-3});
    Core::TransientRun past;
    const std::vector<std::string> &warnings = RunRectifier(dimmer.Circuit, past).Warnings;
    CAPTURE(warnings);
    CHECK(HasWarning(warnings, "TR1 exceeds its IT(RMS) rating: "));
}

TEST_CASE("A thyristor is judged on the I2t of the worst half-cycle, not of the whole run",
          "[thyristor][simulator][ratings]") {
    // About 0.92 A for 40 ms: 8.5 mA2s over a 10 ms half-cycle, four times that over the whole run
    LatchedOnDC latched(10.0);
    const Core::TransientSettings settings = {.StopTime = 40e-3, .TimeStep = 0.1e-3};
    latched.Part.SetParameters({.MaxI2t = 15e-3});
    const Core::TransientRun within = Core::RunTransient(latched.Circuit, settings);
    REQUIRE(within.Result.has_value());
    CAPTURE(within.Result->Warnings);
    CHECK_FALSE(HasRatingWarning(within.Result->Warnings));

    latched.Part.SetParameters({.MaxI2t = 5e-3});
    const Core::TransientRun past = Core::RunTransient(latched.Circuit, settings);
    REQUIRE(past.Result.has_value());
    CAPTURE(past.Result->Warnings);
    CHECK(HasWarning(past.Result->Warnings, "SCR1 exceeds its I2t rating: "));
}

TEST_CASE("An operating point checks the voltage and current of a thyristor, but has no I2t",
          "[thyristor][simulator][ratings]") {
    LatchedOnDC latched(10.0);
    // On, it drops about 0.8 V and carries about 0.92 A; a single point has no span to heat it over
    latched.Part.SetParameters({.MaxRepetitivePeakVoltage = 0.5, .MaxOnStateCurrent = 0.5, .MaxI2t = 1e-9});
    const Core::OperatingPointRun run = Core::RunOperatingPoint(latched.Circuit);
    REQUIRE(run.Result.has_value());
    CAPTURE(run.Result->Warnings);
    CHECK(HasWarning(run.Result->Warnings, "SCR1 exceeds its VDRM/VRRM rating: "));
    CHECK(HasWarning(run.Result->Warnings, "SCR1 exceeds its IT(AV) rating: "));
    CHECK_FALSE(HasWarning(run.Result->Warnings, "SCR1 exceeds its I2t rating"));
}

TEST_CASE("A thyristor refuses negative ratings", "[thyristor][ratings]") {
    CHECK(Core::Thyristor::IsValidParameters(GENEROUS_RATINGS));
    CHECK_FALSE(Core::Thyristor::IsValidParameters({.MaxRepetitivePeakVoltage = -1.0}));
    CHECK_FALSE(Core::Thyristor::IsValidParameters({.MaxOnStateCurrent = -1.0}));
    CHECK_FALSE(Core::Thyristor::IsValidParameters({.MaxSurgeCurrent = -1.0}));
    CHECK_FALSE(Core::Thyristor::IsValidParameters({.MaxI2t = -1.0}));
}
