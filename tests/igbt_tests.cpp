/**
 * @file    igbt_tests.cpp
 * @brief   Tests for the IGBT part: its models, currents and netlist, the refusal of ngspice, its on-state, tail and
 *          co-packaged diode on Nodalys, and the warnings of a run past its ratings.
 */

#include "backends/ngspice/ngspice_backend.h"
#include "circuit/circuit.h"
#include "circuit/circuit_currents.h"
#include "circuit/circuit_netlist.h"
#include "circuit/components/ground.h"
#include "circuit/components/igbt.h"
#include "circuit/components/resistor.h"
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
#include <string_view>
#include <vector>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

/// Nodes of the switch: the supply, the collector and the gate.
constexpr int SUPPLY_NODE = 1;
constexpr int COLLECTOR_NODE = 2;
constexpr int GATE_NODE = 3;

/**
 * @struct  Switch
 * @brief   A supply feeding the collector of an IGBT through a load, its emitter grounded and its gate held by a
 *          source, which a test may turn into a pulse.
 */
struct Switch {
    Core::VoltageSource Supply;
    Core::Resistor Load;
    Core::IGBT Part;
    Core::VoltageSource Gate;
    Core::Ground Reference;
    Core::Circuit Circuit;

    Switch(const double supply_volts, const double load_ohms, const double gate_volts) {
        Supply.SetName("V1");
        Supply.SetValue(supply_volts);
        Load.SetName("R1");
        Load.SetValue(load_ohms);
        Part.SetName("Q1");
        Gate.SetName("Vg");
        Gate.SetValue(gate_volts);
        Circuit.Add(Supply, {SUPPLY_NODE, 0});
        Circuit.Add(Load, {SUPPLY_NODE, COLLECTOR_NODE});
        Circuit.Add(Part, {COLLECTOR_NODE, GATE_NODE, 0});
        Circuit.Add(Gate, {GATE_NODE, 0});
        Circuit.Add(Reference, {0});
    }
};

/// The default model, the 600 V 30 A one.
const Core::IGBTParameters &DefaultFigures() {
    return Core::GetIGBTModels().front().Parameters;
}

double OperatingCurrent(const Core::OperatingPoint &result, const std::string_view name) {
    const auto current = std::ranges::find(result.Currents, name, &Core::ComponentCurrent::Name);
    REQUIRE(current != result.Currents.end());
    return current->Current;
}

const Core::OperatingPoint &RunOperatingPoint(const Core::Circuit &circuit, Core::OperatingPointRun &run) {
    run = Core::RunOperatingPoint(circuit);
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    return *run.Result;
}

/// True when some warning starts with the text, so a test can check one rating at a time.
bool HasWarning(const std::vector<std::string> &warnings, const std::string_view start) {
    return std::ranges::any_of(warnings, [&](const std::string &warning) { return warning.starts_with(start); });
}

} // namespace

TEST_CASE("An IGBT starts with the first ready model, and clones keep custom figures", "[igbt]") {
    Core::IGBT part;
    CHECK(part.GetType() == Core::ComponentType::IGBT);
    REQUIRE(Core::GetIGBTModels().size() == 2);
    CHECK(std::string_view(part.GetModelName()) == "600V 30A");
    CHECK(Core::FindIGBTModel(Core::ComponentType::IGBT, "1200V 40A") != nullptr);
    CHECK(Core::FindIGBTModel(Core::ComponentType::IGBT, "IRF540N") == nullptr);
    for (const Core::IGBTModel &model : Core::GetIGBTModels()) {
        CHECK(Core::IGBT::IsValidParameters(model.Parameters));
    }

    Core::IGBTParameters figures = part.GetParameters();
    figures.OnResistance = 0.05;
    part.SetCustomParameters(figures);
    const std::unique_ptr<Core::Component> copy = part.Clone();
    CHECK(static_cast<const Core::IGBT &>(*copy).IsCustom());
    CHECK(static_cast<const Core::IGBT &>(*copy).GetParameters().OnResistance == 0.05);
    CHECK(Core::ParseComponentType("IGBT") == Core::ComponentType::IGBT);
}

TEST_CASE("Figures an IGBT cannot have are refused", "[igbt]") {
    const Core::IGBTParameters valid = DefaultFigures();
    Core::IGBTParameters small_input = valid;
    small_input.InputCapacitance = 0.5 * valid.ReverseTransferCapacitance;
    Core::IGBTParameters long_tail = valid;
    long_tail.TailFraction = 1.5;
    Core::IGBTParameters no_line = valid;
    no_line.OnResistance = 0.0;
    Core::IGBTParameters negative_diode = valid;
    negative_diode.DiodeForwardVoltage = -1.0;
    for (const Core::IGBTParameters &figures : {small_input, long_tail, no_line, negative_diode}) {
        CHECK_FALSE(Core::IGBT::IsValidParameters(figures));
    }
    // A diode forward voltage of zero leaves the diode out, which is valid
    Core::IGBTParameters bare = valid;
    bare.DiodeForwardVoltage = 0.0;
    CHECK(Core::IGBT::IsValidParameters(bare));
}

TEST_CASE("An IGBT reports the currents of its collector, gate and emitter", "[igbt]") {
    Core::IGBT part;
    part.SetName("Q1");
    const std::vector<std::string> names = Core::GetCurrentNames(part);
    REQUIRE(names.size() == 3);
    CHECK(names[0] == "Q1.C");
    CHECK(names[1] == "Q1.G");
    CHECK(names[2] == "Q1.E");
}

TEST_CASE("An IGBT writes one instance of its subcircuit, however many there are", "[igbt]") {
    Switch circuit(20.0, 1.0, 15.0);
    Core::IGBT second;
    second.SetName("Q2");
    circuit.Circuit.Add(second, {4, 5, 0});
    const std::string netlist = circuit.Circuit.ToSpiceNetlist();
    // The 600 V model: K = 15^2 / (2 * 30) = 3.75, Cge = 1.6n - 50p, Cce = 110p - 50p
    CHECK(netlist.contains("X-Q1 2 3 0 imcsim_igbt VTO=5 KP=3.75 CGC=50p CGE=1.55n CCE=60p\n"));
    CHECK(netlist.contains("X-Q2 4 5 0 imcsim_igbt"));
    CHECK(netlist.find(".subckt imcsim_igbt") == netlist.rfind(".subckt imcsim_igbt"));
    CHECK(Core::GetElementNames(circuit.Part) == std::vector<std::string>{"X-Q1"});
}

TEST_CASE("ngspice refuses an IGBT with the reason, and Nodalys runs every analysis on it",
          "[igbt][simulation_backend]") {
    const Core::NgspiceBackend ngspice;
    const std::optional<std::string> refusal =
        Core::FindUnsupported(ngspice, {Core::ComponentType::IGBT}, Core::AnalysisKind::TRANSIENT);
    REQUIRE(refusal.has_value());
    CHECK(refusal->contains("Nodalys"));

    const TestSupport::NodalysInUse nodalys;
    // Its conduction is smooth and has no latch, so even the harmonic balance and the pole-zero take it
    for (const Core::AnalysisKind kind :
         {Core::AnalysisKind::TRANSIENT, Core::AnalysisKind::HARMONIC_BALANCE, Core::AnalysisKind::POLE_ZERO}) {
        CHECK_FALSE(Core::FindUnsupported(Core::GetBackend(), {Core::ComponentType::IGBT}, kind).has_value());
    }
}

TEST_CASE("An IGBT driven hard on drops the on-state line of its model on Nodalys", "[igbt][simulator]") {
    const TestSupport::NodalysInUse nodalys;
    // 20 V through 1.27 Ohm leaves about 15 A, which the 600 V model drops as 0.8 V + 25 mOhm * 15 A = 1.18 V
    Switch circuit(20.0, 1.27, 15.0);
    Core::OperatingPointRun run;
    const Core::OperatingPoint &result = RunOperatingPoint(circuit.Circuit, run);
    const double collector_current = OperatingCurrent(result, "Q1.C");
    CHECK_THAT(collector_current, WithinRel(15.0, 0.02));
    const double expected = DefaultFigures().OnThresholdVoltage + DefaultFigures().OnResistance * collector_current;
    CHECK_THAT(result.NodeVoltages[COLLECTOR_NODE], WithinAbs(expected, 0.01));
    CHECK_THAT(OperatingCurrent(result, "Q1.E"), WithinAbs(-collector_current, 1e-6));
}

TEST_CASE("The co-packaged diode of an IGBT drops its forward voltage at the rated current", "[igbt][simulator]") {
    const TestSupport::NodalysInUse nodalys;
    // A reversed supply drives the rated 30 A backward through the diode, which the model drops as 1.6 V
    Switch circuit(-31.6, 1.0, 0.0);
    Core::OperatingPointRun run;
    const Core::OperatingPoint &result = RunOperatingPoint(circuit.Circuit, run);
    CHECK_THAT(OperatingCurrent(result, "Q1.C"), WithinRel(-30.0, 0.01));
    CHECK_THAT(result.NodeVoltages[COLLECTOR_NODE], WithinAbs(-DefaultFigures().DiodeForwardVoltage, 0.02));

    // Without the diode the IGBT blocks the reverse voltage
    Core::IGBTParameters bare = DefaultFigures();
    bare.DiodeForwardVoltage = 0.0;
    circuit.Part.SetCustomParameters(bare);
    Core::OperatingPointRun bare_run;
    CHECK(std::abs(OperatingCurrent(RunOperatingPoint(circuit.Circuit, bare_run), "Q1.C")) < 1e-6);
}

TEST_CASE("An IGBT keeps a tail current flowing after its gate turns it off", "[igbt][simulator]") {
    const TestSupport::NodalysInUse nodalys;
    // 15 A cut at 2 us: the 600 V model carries 15 % of it on, decaying with 150 ns
    Switch circuit(20.0, 1.27, 0.0);
    circuit.Gate.SetSourceType(Core::VoltageSource::SourceType::PULSE);
    circuit.Gate.SetPulse(
        {.Low = 0.0, .High = 15.0, .Delay = 0.0, .RiseTime = 10e-9, .FallTime = 10e-9, .Width = 2e-6, .Period = 1.0});
    const Core::TransientRun run = Core::RunTransient(circuit.Circuit, {.StopTime = 4e-6, .TimeStep = 5e-9});
    REQUIRE(run.Result.has_value());
    const Core::Transient &transient = *run.Result;
    const auto trace = std::ranges::find(transient.Currents, "Q1.C", &Core::ComponentTrace::Name);
    REQUIRE(trace != transient.Currents.end());
    const auto current_at = [&](const double time) {
        const auto point = std::ranges::lower_bound(transient.Times, time);
        REQUIRE(point != transient.Times.end());
        return trace->Values[static_cast<std::size_t>(point - transient.Times.begin())];
    };
    CHECK_THAT(current_at(1.5e-6), WithinRel(15.0, 0.02));
    // A tail time constant after the gate has fallen, the tail is about 15 A * 15 % / e = 0.8 A
    const double tail = current_at(2.01e-6 + DefaultFigures().TailTimeConstant);
    INFO("tail " << tail << " A");
    CHECK(tail > 0.4);
    CHECK(tail < 1.3);
    CHECK(std::abs(current_at(3.9e-6)) < 0.05);
}

TEST_CASE("An IGBT past its VCE, IC or power rating is reported", "[igbt][simulator][ratings]") {
    const TestSupport::NodalysInUse nodalys;
    // 700 V across a 600 V part that the gate keeps off
    Switch blocking(700.0, 1000.0, 0.0);
    Core::OperatingPointRun blocking_run;
    const std::vector<std::string> &blocking_warnings = RunOperatingPoint(blocking.Circuit, blocking_run).Warnings;
    CAPTURE(blocking_warnings);
    CHECK(HasWarning(blocking_warnings, "Q1 (600V 30A) exceeds its VCE rating: "));

    // 50 A through a 30 A part dissipates about 50 A * 2 V = 100 W, within its 200 W
    Switch overloaded(52.0, 1.0, 15.0);
    Core::OperatingPointRun overloaded_run;
    const std::vector<std::string> &overloaded_warnings =
        RunOperatingPoint(overloaded.Circuit, overloaded_run).Warnings;
    CAPTURE(overloaded_warnings);
    CHECK(HasWarning(overloaded_warnings, "Q1 (600V 30A) exceeds its IC rating: "));
    CHECK_FALSE(HasWarning(overloaded_warnings, "Q1 (600V 30A) exceeds its power rating"));

    // A gate driven past VGES
    Switch overdriven(20.0, 10.0, 25.0);
    Core::OperatingPointRun overdriven_run;
    CHECK(HasWarning(RunOperatingPoint(overdriven.Circuit, overdriven_run).Warnings,
                     "Q1 (600V 30A) exceeds its VGE rating: "));

    // Within every rating, nothing is said
    Switch nominal(20.0, 1.27, 15.0);
    Core::OperatingPointRun nominal_run;
    CHECK(RunOperatingPoint(nominal.Circuit, nominal_run).Warnings.empty());
}
