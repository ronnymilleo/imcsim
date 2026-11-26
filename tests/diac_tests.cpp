/**
 * @file    diac_tests.cpp
 * @brief   Tests for the DIAC: its figures, its current, its netlist, the refusal of ngspice and the relaxation
 *          oscillator it makes on Nodalys.
 */

#include "backends/ngspice/ngspice_backend.h"
#include "circuit/circuit.h"
#include "circuit/circuit_currents.h"
#include "circuit/components/capacitor.h"
#include "circuit/components/diac.h"
#include "circuit/components/ground.h"
#include "circuit/components/resistor.h"
#include "circuit/components/voltage_source.h"
#include "nodalys_in_use.h"
#include "simulation/simulation_backend.h"
#include "simulation/simulator.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <memory>
#include <string>
#include <vector>

using Catch::Matchers::WithinAbs;

namespace {

/// Nodes of the relaxation oscillator: the supply, the capacitor the DIAC discharges, and its discharge resistor.
constexpr int SUPPLY_NODE = 1;
constexpr int CAPACITOR_NODE = 2;
constexpr int DISCHARGE_NODE = 3;

/**
 * @struct  RelaxationOscillator
 * @brief   50 V charging 100 nF through 100 kOhm, with a DIAC across the capacitor discharging it into 100 Ohm each
 *          time the capacitor reaches the breakover.
 * @details The supply steps up from 0 V, since an oscillator has no operating point to start from.
 */
struct RelaxationOscillator {
    Core::VoltageSource Supply;
    Core::Resistor Charge;
    Core::Capacitor Timing;
    Core::Diac Trigger;
    Core::Resistor Discharge;
    Core::Ground Reference;
    Core::Circuit Circuit;

    RelaxationOscillator() {
        Supply.SetName("V1");
        Supply.SetSourceType(Core::VoltageSource::SourceType::PULSE);
        Supply.SetPulse({.Low = 0.0, .High = 50.0, .Delay = 0.0, .Width = 1.0, .Period = 10.0});
        Charge.SetName("R1");
        Charge.SetValue(100e3);
        Timing.SetName("C1");
        Timing.SetValue(100e-9);
        Trigger.SetName("DIAC1");
        Discharge.SetName("R2");
        Discharge.SetValue(100.0);
        Circuit.Add(Supply, {SUPPLY_NODE, 0});
        Circuit.Add(Charge, {SUPPLY_NODE, CAPACITOR_NODE});
        Circuit.Add(Timing, {CAPACITOR_NODE, 0});
        Circuit.Add(Trigger, {CAPACITOR_NODE, DISCHARGE_NODE});
        Circuit.Add(Discharge, {DISCHARGE_NODE, 0});
        Circuit.Add(Reference, {0});
    }
};

} // namespace

TEST_CASE("A DIAC starts with the figures of a DB3, and clones keep them", "[diac]") {
    Core::Diac part;
    CHECK(part.GetType() == Core::ComponentType::DIAC);
    CHECK(part.GetParameters() == Core::DiacParameters{});
    CHECK(part.GetParameters().BreakoverVoltage == 32.0);
    CHECK(part.GetParameters().OnThresholdVoltage == 27.0);
    part.SetParameters({.BreakoverVoltage = 40.0, .OnThresholdVoltage = 33.0});
    const std::unique_ptr<Core::Component> copy = part.Clone();
    CHECK(copy->GetType() == Core::ComponentType::DIAC);
    CHECK(static_cast<const Core::Diac &>(*copy).GetParameters().BreakoverVoltage == 40.0);
}

TEST_CASE("A DIAC refuses figures whose release does not lie below the breakover", "[diac]") {
    CHECK(Core::Diac::IsValidParameters({}));
    // The on-state line at the holding current reaches the breakover, so the device would never snap back
    CHECK_FALSE(Core::Diac::IsValidParameters({.BreakoverVoltage = 27.0, .OnThresholdVoltage = 27.0}));
    CHECK_FALSE(Core::Diac::IsValidParameters({.OnResistance = 0.0}));
    CHECK_FALSE(Core::Diac::IsValidParameters({.OnResistance = 2e9}));
    CHECK_FALSE(Core::Diac::IsValidParameters({.HoldingCurrent = 0.0}));
}

TEST_CASE("A DIAC reports one current, into MT2, as a two-terminal part does", "[diac]") {
    Core::Diac part;
    part.SetName("DIAC1");
    CHECK(Core::GetCurrentNames(part) == std::vector<std::string>{"DIAC1"});
}

TEST_CASE("A DIAC writes one instance of its subcircuit with its figures", "[diac]") {
    RelaxationOscillator oscillator;
    const std::string netlist = oscillator.Circuit.ToSpiceNetlist();
    CHECK(netlist.contains("X-DIAC1 2 3 imcsim_diac VBO=32 V0=27 RT=10 IH=1m ROFF=1G\n"));
    CHECK(netlist.contains(".subckt imcsim_diac mt2 mt1"));
    CHECK(netlist.find(".subckt imcsim_diac") == netlist.rfind(".subckt imcsim_diac"));
}

TEST_CASE("ngspice refuses a DIAC with the reason, and Nodalys runs it", "[diac][simulation_backend]") {
    Core::NgspiceBackend ngspice;
    const std::optional<std::string> refusal =
        Core::FindUnsupported(ngspice, {Core::ComponentType::DIAC}, Core::AnalysisKind::TRANSIENT);
    REQUIRE(refusal.has_value());
    CHECK(refusal->contains("Nodalys"));

    const TestSupport::NodalysInUse guard;
    CHECK_FALSE(Core::FindUnsupported(Core::GetBackend(), {Core::ComponentType::DIAC}, Core::AnalysisKind::TRANSIENT)
                    .has_value());
    // The latch has no smooth periodic steady state and no small-signal roots
    CHECK(Core::FindUnsupported(Core::GetBackend(), {Core::ComponentType::DIAC}, Core::AnalysisKind::HARMONIC_BALANCE)
              .has_value());
    CHECK(Core::FindUnsupported(Core::GetBackend(), {Core::ComponentType::DIAC}, Core::AnalysisKind::POLE_ZERO)
              .has_value());
}

TEST_CASE("A DIAC relaxation oscillator swings between the breakover and the release on Nodalys", "[diac][simulator]") {
    const TestSupport::NodalysInUse guard;
    RelaxationOscillator oscillator;
    const Core::TransientRun run = Core::RunTransient(oscillator.Circuit, {.StopTime = 20e-3, .TimeStep = 1e-6});
    REQUIRE(run.Result.has_value());
    const std::vector<double> &voltages = run.Result->NodeVoltages[CAPACITOR_NODE];
    // Over the second half the capacitor ramps to the 32 V breakover and drops to the 27 V on-state line plus the
    // drop of the 1 mA holding current across 10 Ohm and 100 Ohm
    const std::vector<double> settled(voltages.begin() + static_cast<std::ptrdiff_t>(voltages.size() / 2),
                                      voltages.end());
    const auto [lowest, highest] = std::ranges::minmax(settled);
    CHECK_THAT(highest, WithinAbs(32.0, 0.1));
    CHECK_THAT(lowest, WithinAbs(27.1, 0.2));
}
