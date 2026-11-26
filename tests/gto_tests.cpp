/**
 * @file    gto_tests.cpp
 * @brief   Tests for the GTO: its figures, its currents, its netlist, the refusal of ngspice and the DC chopper it
 *          makes on Nodalys, fired by a positive gate pulse and turned off by a negative one.
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
#include <memory>
#include <optional>
#include <string>
#include <vector>

using Catch::Matchers::WithinAbs;

namespace {

/// Nodes of the chopper: the supply, the load, and the two steps of the gate drive.
constexpr int SUPPLY_NODE = 1;
constexpr int LOAD_NODE = 2;
constexpr int GATE_NODE = 3;
constexpr int TURN_OFF_NODE = 4;
constexpr double SUPPLY_VOLTS = 100.0;
constexpr double LOAD_OHMS = 10.0;
/// The instants of the gate drive: the firing pulse and the turn-off pulse, in seconds.
constexpr double FIRE_AT = 1e-3;
constexpr double TURN_OFF_AT = 2e-3;
constexpr double PULSE_WIDTH = 0.2e-3;

/**
 * @struct  Chopper
 * @brief   100 V feeding a GTO into 10 Ohm, so 9.9 A flows once it is on, with a firing pulse of +5 V and a turn-off
 *          pulse of -15 V in series as its gate drive.
 * @details The supply never leaves 100 V: only the gate turns the current off. The GTO has a 10 Ohm gate and a
 *          turn-off current of 0.5 A, so the firing pulse draws 0.42 A and the turn-off pulse 1.42 A.
 */
struct Chopper {
    Core::VoltageSource Supply;
    Core::Thyristor Switch{Core::ComponentType::GTO};
    Core::Resistor Load;
    Core::VoltageSource FirePulse;
    Core::VoltageSource TurnOffPulse;
    Core::Ground Reference;
    Core::Circuit Circuit;

    explicit Chopper(const double turn_off_volts) {
        Supply.SetName("V1");
        Supply.SetValue(SUPPLY_VOLTS);
        Switch.SetName("GTO1");
        Switch.SetParameters({.GateResistance = 10.0, .GateTurnOffCurrent = 0.5});
        Load.SetName("R1");
        Load.SetValue(LOAD_OHMS);
        FirePulse.SetName("Vfire");
        FirePulse.SetSourceType(Core::VoltageSource::SourceType::PULSE);
        FirePulse.SetPulse({.Low = 0.0, .High = 5.0, .Delay = FIRE_AT, .Width = PULSE_WIDTH, .Period = 1.0});
        TurnOffPulse.SetName("Voff");
        TurnOffPulse.SetSourceType(Core::VoltageSource::SourceType::PULSE);
        TurnOffPulse.SetPulse(
            {.Low = 0.0, .High = turn_off_volts, .Delay = TURN_OFF_AT, .Width = PULSE_WIDTH, .Period = 1.0});
        Circuit.Add(Supply, {SUPPLY_NODE, 0});
        Circuit.Add(Switch, {SUPPLY_NODE, LOAD_NODE, GATE_NODE});
        Circuit.Add(Load, {LOAD_NODE, 0});
        Circuit.Add(TurnOffPulse, {GATE_NODE, TURN_OFF_NODE});
        Circuit.Add(FirePulse, {TURN_OFF_NODE, LOAD_NODE});
        Circuit.Add(Reference, {0});
    }
};

/// The load voltage at the first time point at or after an instant.
double LoadVoltageAt(const Core::Transient &transient, const double time) {
    const auto point = std::ranges::lower_bound(transient.Times, time);
    REQUIRE(point != transient.Times.end());
    return transient.NodeVoltages[LOAD_NODE][static_cast<std::size_t>(point - transient.Times.begin())];
}

/// The anode current at the first time point at or after an instant.
double AnodeCurrentAt(const Core::Transient &transient, const double time) {
    const auto trace = std::ranges::find(transient.Currents, "GTO1.A", &Core::ComponentTrace::Name);
    REQUIRE(trace != transient.Currents.end());
    const auto point = std::ranges::lower_bound(transient.Times, time);
    REQUIRE(point != transient.Times.end());
    return trace->Values[static_cast<std::size_t>(point - transient.Times.begin())];
}

} // namespace

TEST_CASE("A GTO starts with the gate figures of a power device, and clones keep them", "[gto]") {
    Core::Thyristor part(Core::ComponentType::GTO);
    CHECK(part.GetType() == Core::ComponentType::GTO);
    CHECK_FALSE(part.GetParameters().IsBidirectional);
    CHECK(part.GetParameters().GateTurnOffCurrent == Core::DEFAULT_GTO_TURN_OFF_CURRENT);
    CHECK(part.GetParameters().GateTriggerCurrent == Core::DEFAULT_GTO_TRIGGER_CURRENT);
    CHECK(part.GetParameters().GateResistance == Core::DEFAULT_GTO_GATE_RESISTANCE);
    CHECK(Core::Thyristor::IsValidParameters(part.GetParameters()));

    part.SetParameters({.GateTurnOffCurrent = 3.0});
    const std::unique_ptr<Core::Component> copy = part.Clone();
    CHECK(copy->GetType() == Core::ComponentType::GTO);
    CHECK(static_cast<const Core::Thyristor &>(*copy).GetParameters().GateTurnOffCurrent == 3.0);
    CHECK(Core::ParseComponentType("Gto") == Core::ComponentType::GTO);
    CHECK(Core::IsThyristor(Core::ComponentType::GTO));
    CHECK_FALSE(Core::IsThyristor(Core::ComponentType::DIAC));
}

TEST_CASE("Only a GTO keeps a gate turn-off current, and a negative one is refused", "[gto]") {
    Core::Thyristor scr;
    scr.SetParameters({.GateTurnOffCurrent = 2.0});
    CHECK(scr.GetParameters().GateTurnOffCurrent == 0.0);
    Core::Thyristor triac(Core::ComponentType::TRIAC);
    triac.SetParameters({.GateTurnOffCurrent = 2.0});
    CHECK(triac.GetParameters().GateTurnOffCurrent == 0.0);
    CHECK_FALSE(Core::Thyristor::IsValidParameters({.GateTurnOffCurrent = -1.0}));
}

TEST_CASE("A GTO reports the currents of its anode, cathode and gate", "[gto]") {
    Core::Thyristor part(Core::ComponentType::GTO);
    part.SetName("GTO1");
    const std::vector<std::string> names = Core::GetCurrentNames(part);
    REQUIRE(names.size() == 3);
    CHECK(names[0] == "GTO1.A");
    CHECK(names[1] == "GTO1.K");
    CHECK(names[2] == "GTO1.G");
}

TEST_CASE("A GTO writes one instance of its subcircuit, however many there are", "[gto]") {
    Chopper chopper(-15.0);
    Core::Thyristor second(Core::ComponentType::GTO);
    second.SetName("GTO2");
    chopper.Circuit.Add(second, {5, 6, 7});

    const std::string netlist = chopper.Circuit.ToSpiceNetlist();
    CHECK(netlist.contains("X-GTO1 1 2 3 imcsim_gto RG=10 RT=10m\n"));
    CHECK(netlist.contains("X-GTO2 5 6 7 imcsim_gto RG=1 RT=10m\n"));
    CHECK(netlist.find(".subckt imcsim_gto") == netlist.rfind(".subckt imcsim_gto"));
    CHECK(netlist.contains(".ends imcsim_gto\n"));
}

TEST_CASE("ngspice refuses a GTO with the reason, and Nodalys runs it", "[gto][simulation_backend]") {
    Core::NgspiceBackend ngspice;
    const std::optional<std::string> refusal =
        Core::FindUnsupported(ngspice, {Core::ComponentType::GTO}, Core::AnalysisKind::TRANSIENT);
    REQUIRE(refusal.has_value());
    CHECK(refusal->contains("Nodalys"));

    const TestSupport::NodalysInUse guard;
    CHECK_FALSE(Core::FindUnsupported(Core::GetBackend(), {Core::ComponentType::GTO}, Core::AnalysisKind::TRANSIENT)
                    .has_value());
    // The latch has no smooth periodic steady state and no small-signal roots
    CHECK(Core::FindUnsupported(Core::GetBackend(), {Core::ComponentType::GTO}, Core::AnalysisKind::HARMONIC_BALANCE)
              .has_value());
    CHECK(Core::FindUnsupported(Core::GetBackend(), {Core::ComponentType::GTO}, Core::AnalysisKind::POLE_ZERO)
              .has_value());
}

TEST_CASE("A negative gate pulse turns a GTO off while the supply still drives its load", "[gto][simulator]") {
    const TestSupport::NodalysInUse guard;
    Chopper chopper(-15.0);
    const Core::TransientRun run = Core::RunTransient(chopper.Circuit, {.StopTime = 3e-3, .TimeStep = 10e-6});
    REQUIRE(run.Result.has_value());
    const Core::Transient &transient = *run.Result;
    // Blocking until the firing pulse, then on with 9.9 A through the load, far above the 5 mA holding current
    CHECK_THAT(LoadVoltageAt(transient, 0.5 * FIRE_AT), WithinAbs(0.0, 1e-3));
    CHECK_THAT(AnodeCurrentAt(transient, 1.5e-3), WithinAbs(9.91, 0.05));
    CHECK_THAT(AnodeCurrentAt(transient, 1.99e-3), WithinAbs(9.91, 0.05));
    // The pulse pulls 1.42 A out of the gate, above the 0.5 A turn-off current: the load current drops to the
    // blocking leakage, and stays there after the pulse, with the 100 V supply still across the device
    CHECK_THAT(AnodeCurrentAt(transient, 2.1e-3), WithinAbs(0.0, 1e-6));
    CHECK_THAT(AnodeCurrentAt(transient, 2.9e-3), WithinAbs(0.0, 1e-6));
    CHECK_THAT(LoadVoltageAt(transient, 2.9e-3), WithinAbs(0.0, 1e-3));
}

TEST_CASE("A negative gate pulse under the turn-off current leaves a GTO on", "[gto][simulator]") {
    const TestSupport::NodalysInUse guard;
    // -3 V draws (3 - 0.8) / 10 = 0.22 A, below the 0.5 A turn-off current
    Chopper chopper(-3.0);
    const Core::TransientRun run = Core::RunTransient(chopper.Circuit, {.StopTime = 3e-3, .TimeStep = 10e-6});
    REQUIRE(run.Result.has_value());
    CHECK_THAT(AnodeCurrentAt(*run.Result, 2.9e-3), WithinAbs(9.91, 0.05));
}
