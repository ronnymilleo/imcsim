/**
 * @file    initial_voltages_tests.cpp
 * @brief   Tests for the initial voltage part: its place in the schematic, the checks, the netlist and what each engine
 *          does with it in a transient.
 * @details The reference values are analytic: an RC discharge follows V0 exp(-t / RC) and a divider holds the
 *          consistent point under a node set to a voltage. Both engines run each circuit, so a meaning that differs
 *          between them shows here.
 */

#include "circuit/components/capacitor.h"
#include "circuit/components/ground.h"
#include "circuit/components/resistor.h"
#include "circuit/components/vcc.h"
#include "circuit/components/voltage_source.h"
#include "circuit/subcircuit.h"
#include "nodalys_in_use.h"
#include "schematic/clipboard.h"
#include "schematic/element_factory.h"
#include "schematic/examples.h"
#include "schematic/schematic.h"
#include "schematic/schematic_file.h"
#include "simulation/circuit_checks.h"
#include "simulation/circuit_copy.h"
#include "simulation/simulator.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cmath>
#include <cstddef>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;

/// A transient of 5 ms in 5 us steps, which holds five time constants of the 1 ms circuits below.
constexpr Core::TransientSettings FIVE_MILLISECONDS{.StopTime = 5e-3, .TimeStep = 5e-6};

/// Voltage of a node at the first point at or after a time.
double VoltageAt(const Core::Transient &transient, const std::size_t node, const double time) {
    const auto point = std::ranges::lower_bound(transient.Times, time);
    REQUIRE(point != transient.Times.end());
    return transient.NodeVoltages[node][static_cast<std::size_t>(point - transient.Times.begin())];
}

/**
 * @struct  Discharge
 * @brief   A 1k resistor and a 1u capacitor from node 1 to ground, with no source: time constant 1 ms.
 * @details A second pair on node 2 has a capacitor with its own initial condition when asked, which makes the
 *          transient start from initial conditions (uic) instead of an operating point.
 */
struct Discharge {
    Core::Resistor Resistor;
    Core::Capacitor Capacitor;
    Core::Resistor OtherResistor;
    Core::Capacitor OtherCapacitor;
    Core::Ground Reference;
    Core::Circuit Circuit;

    explicit Discharge(const bool has_other_condition) {
        Resistor.SetName("R1");
        Resistor.SetValue(1e3);
        Capacitor.SetName("C1");
        Capacitor.SetValue(1e-6);
        Circuit.Add(Resistor, {1, 0});
        Circuit.Add(Capacitor, {1, 0});
        if (has_other_condition) {
            OtherResistor.SetName("R2");
            OtherResistor.SetValue(1e3);
            OtherCapacitor.SetName("C2");
            OtherCapacitor.SetValue(1e-6);
            OtherCapacitor.SetInitialCondition(2.0);
            Circuit.Add(OtherResistor, {2, 0});
            Circuit.Add(OtherCapacitor, {2, 0});
        }
        Circuit.Add(Reference, {0});
    }
};

/**
 * @struct  Divider
 * @brief   5 V on node 1 over three 1k resistors in series to ground, nodes 2 and 3 between them, and a 1u capacitor
 *          on node 3.
 * @details At the operating point node 2 sits at 3.33 V and node 3 at 1.67 V. With node 2 held at 4 V node 3 is at 2 V,
 *          which only a solve that holds the node finds.
 */
struct Divider {
    Core::VCC Supply;
    Core::Resistor Top;
    Core::Resistor Middle;
    Core::Resistor Bottom;
    Core::Capacitor Capacitor;
    Core::Ground Reference;
    Core::Circuit Circuit;

    Divider() {
        Supply.SetName("V1");
        Supply.SetValue(5.0);
        for (Core::Resistor *resistor : {&Top, &Middle, &Bottom}) {
            resistor->SetValue(1e3);
        }
        Top.SetName("R1");
        Middle.SetName("R2");
        Bottom.SetName("R3");
        Capacitor.SetName("C1");
        Capacitor.SetValue(1e-6);
        Circuit.Add(Supply, {1});
        Circuit.Add(Top, {1, 2});
        Circuit.Add(Middle, {2, 3});
        Circuit.Add(Bottom, {3, 0});
        Circuit.Add(Capacitor, {3, 0});
        Circuit.Add(Reference, {0});
    }
};

/// Runs a transient on each engine in turn, ngspice first.
std::vector<Core::Transient> RunOnBothEngines(const Core::Circuit &circuit, const Core::TransientSettings &settings) {
    std::vector<Core::Transient> results;
    const Core::TransientRun ngspice = Core::RunTransient(circuit, settings);
    REQUIRE(ngspice.Result.has_value());
    results.push_back(*ngspice.Result);
    const TestSupport::NodalysInUse nodalys_in_use;
    const Core::TransientRun nodalys = Core::RunTransient(circuit, settings);
    REQUIRE(nodalys.Result.has_value());
    results.push_back(*nodalys.Result);
    return results;
}

/// A schematic of a resistor to ground on a wire that an initial voltage marker of the given value also touches.
struct MarkedResistor {
    GUI::Schematic Schematic;

    explicit MarkedResistor(const double volts) {
        Schematic.AddElement(GUI::CreateElement(Core::ComponentType::RESISTOR, {10, 10}, GUI::Rotation::R90));
        Schematic.AddElement(GUI::CreateElement(Core::ComponentType::GROUND, {10, 14}, GUI::Rotation::R0));
        Schematic.AddWire({10, 12}, {10, 14});
        AddMarker({10, 8}, volts);
    }

    void AddMarker(const GUI::GridPoint position, const double volts) {
        auto marker = GUI::CreateElement(Core::ComponentType::INITIAL_VOLTAGE, position, GUI::Rotation::R0);
        marker->GetComponent().SetValue(volts);
        Schematic.AddElement(std::move(marker));
    }
};

} // namespace

TEST_CASE("An initial voltage has a value in volts, no name, and is found by its name", "[initial_voltage]") {
    const auto marker = GUI::CreateElement(Core::ComponentType::INITIAL_VOLTAGE, {0, 0}, GUI::Rotation::R0);
    const Core::Component &part = marker->GetComponent();
    CHECK(part.GetType() == Core::ComponentType::INITIAL_VOLTAGE);
    CHECK(part.HasValue());
    CHECK(std::string_view(part.GetUnit()) == "V");
    CHECK(std::string_view(part.GetNamePrefix()).empty());
    CHECK(part.GetValue() == 2.5);
    CHECK(part.IsValidValue(-3.0));
    CHECK_FALSE(part.IsValidValue(std::nan("")));
    CHECK(Core::ParseComponentType("InitialVoltage") == Core::ComponentType::INITIAL_VOLTAGE);
    CHECK(part.Clone()->GetType() == Core::ComponentType::INITIAL_VOLTAGE);
    CHECK(std::string_view(GUI::GetPartName(Core::ComponentType::INITIAL_VOLTAGE)) == "Initial voltage");
    CHECK(std::ranges::contains(GUI::FindParts("initial"), Core::ComponentType::INITIAL_VOLTAGE));
    // It touches one node, like a net label
    CHECK(marker->GetTerminals() == std::vector<GUI::GridPoint>{{0, 0}});
}

TEST_CASE("A marker on a wire gives its node an initial voltage and stays out of the parts", "[initial_voltage]") {
    MarkedResistor setup(3.0);
    const Core::Circuit circuit = setup.Schematic.BuildCircuit();
    // The resistor and the ground only: the marker writes no element
    CHECK(circuit.GetEntries().size() == 2);
    REQUIRE(circuit.GetInitialVoltages().size() == 1);
    CHECK(circuit.GetInitialVoltages()[0].Volts == 3.0);
    // The resistor's top terminal is at (10, 8), which the marker touches
    CHECK(circuit.GetInitialVoltages()[0].Node == circuit.GetEntries()[0].Nodes[0]);
    CHECK_FALSE(Core::CheckCircuit(circuit).has_value());
}

TEST_CASE("A marker needs no Ground of its own and a circuit of only markers is empty", "[initial_voltage]") {
    GUI::Schematic schematic;
    schematic.AddElement(GUI::CreateElement(Core::ComponentType::INITIAL_VOLTAGE, {4, 4}, GUI::Rotation::R0));
    const std::optional<std::string> problem = Core::CheckCircuit(schematic.BuildCircuit());
    REQUIRE(problem.has_value());
    CHECK_THAT(*problem, ContainsSubstring("empty"));
}

TEST_CASE("Two markers on one node agree or are refused, naming both voltages", "[initial_voltage]") {
    SECTION("The same voltage is fine and is written once") {
        MarkedResistor setup(3.0);
        setup.AddMarker({10, 6}, 3.0);
        setup.Schematic.AddWire({10, 6}, {10, 8});
        const Core::Circuit circuit = setup.Schematic.BuildCircuit();
        CHECK(circuit.GetInitialVoltages().size() == 2);
        CHECK_FALSE(Core::CheckCircuit(circuit).has_value());
        const std::string netlist = circuit.ToSpiceNetlist();
        const std::size_t first = netlist.find(".ic V(");
        REQUIRE(first != std::string::npos);
        CHECK(netlist.find(".ic V(", first + 1) == std::string::npos);
    }
    SECTION("Different voltages are an error with both values") {
        MarkedResistor setup(3.0);
        setup.AddMarker({10, 6}, 1.5);
        setup.Schematic.AddWire({10, 6}, {10, 8});
        const std::optional<std::string> problem = Core::CheckCircuit(setup.Schematic.BuildCircuit());
        REQUIRE(problem.has_value());
        CHECK_THAT(*problem, ContainsSubstring("two initial voltages"));
        CHECK_THAT(*problem, ContainsSubstring("3 V"));
        CHECK_THAT(*problem, ContainsSubstring("1.5 V"));
    }
}

TEST_CASE("A marker on ground or on a node nothing is connected to is refused", "[initial_voltage]") {
    SECTION("Ground is always 0 V") {
        MarkedResistor setup(3.0);
        setup.AddMarker({10, 16}, 1.0);
        setup.Schematic.AddWire({10, 14}, {10, 16});
        const std::optional<std::string> problem = Core::CheckCircuit(setup.Schematic.BuildCircuit());
        REQUIRE(problem.has_value());
        CHECK_THAT(*problem, ContainsSubstring("ground"));
    }
    SECTION("A loose marker has no node to start") {
        MarkedResistor setup(3.0);
        setup.AddMarker({30, 30}, 1.0);
        const std::optional<std::string> problem = Core::CheckCircuit(setup.Schematic.BuildCircuit());
        REQUIRE(problem.has_value());
        CHECK_THAT(*problem, ContainsSubstring("no part is connected"));
    }
}

TEST_CASE("A marker inside a block is refused with the way out", "[initial_voltage]") {
    auto definition = std::make_shared<Core::SubcircuitDefinition>();
    definition->Name = "TANK";
    definition->Body.AddInitialVoltage({.Node = 1, .Volts = 2.0});
    Core::Resistor resistor;
    resistor.SetName("R1");
    Core::Ground ground;
    Core::Circuit circuit;
    circuit.Add(resistor, {1, 0});
    circuit.Add(ground, {0});
    circuit.AddSubcircuit(definition);
    const std::optional<std::string> problem = Core::CheckCircuit(circuit);
    REQUIRE(problem.has_value());
    CHECK_THAT(*problem, ContainsSubstring("Block TANK holds an initial voltage"));
    CHECK_THAT(*problem, ContainsSubstring("pin"));
}

TEST_CASE("The circuit copies carry the initial voltages", "[initial_voltage]") {
    Core::Resistor resistor;
    resistor.SetName("R1");
    Core::Circuit circuit;
    circuit.Add(resistor, {1, 0});
    circuit.AddInitialVoltage({.Node = 1, .Volts = 4.0});

    const Core::CircuitCopy copy(circuit);
    REQUIRE(copy.GetCircuit().GetInitialVoltages().size() == 1);
    CHECK(copy.GetCircuit().GetInitialVoltages()[0].Volts == 4.0);

    Core::Circuit derived;
    derived.Add(resistor, {1, 0});
    derived.AddDefinitionsOf(circuit);
    REQUIRE(derived.GetInitialVoltages().size() == 1);
    CHECK(derived.GetInitialVoltages()[0].Node == 1);
}

TEST_CASE("The netlist writes the initial voltage of a node as .ic and no element", "[initial_voltage]") {
    MarkedResistor setup(3.0);
    const std::string netlist = setup.Schematic.BuildCircuit().ToSpiceNetlist();
    const Core::Circuit circuit = setup.Schematic.BuildCircuit();
    CHECK_THAT(netlist, ContainsSubstring(std::format(".ic V({})=3\n", circuit.GetEntries()[0].Nodes[0])));
    CHECK(std::ranges::count(netlist, '\n') == 4);
}

TEST_CASE("A saved schematic keeps its initial voltages and a file without them loads as before", "[initial_voltage]") {
    MarkedResistor setup(-1.25);
    const std::string text = GUI::SaveSchematic(setup.Schematic.GetElements(), setup.Schematic.GetWires(),
                                                setup.Schematic.GetSimulationSettings(), {});
    const auto loaded = GUI::LoadSchematic(text);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->Elements.size() == 3);
    const Core::Component &marker = loaded->Elements[2]->GetComponent();
    CHECK(marker.GetType() == Core::ComponentType::INITIAL_VOLTAGE);
    CHECK(marker.GetValue() == -1.25);
    CHECK(loaded->Elements[2]->GetPosition() == GUI::GridPoint{10, 8});
    CHECK(loaded->Warnings.empty());

    // A file of a schematic made before the part existed has none
    const auto old = GUI::LoadSchematic(
        R"({"format": "imcsim-schematic", "version": 1, "elements": [
            {"type": "Resistor", "name": "R1", "value": 1000.0, "x": 0, "y": 0, "rotation": 0}], "wires": []})");
    REQUIRE(old.has_value());
    CHECK(old->Elements.size() == 1);
}

TEST_CASE("Copying a marker and pasting it keeps its voltage", "[initial_voltage]") {
    MarkedResistor setup(1.8);
    const GUI::Selection selection{.Elements = {2}, .Wires = {}};
    auto group = GUI::ParseCopiedSchematic(
        GUI::CopySelection(setup.Schematic.GetElements(), setup.Schematic.GetWires(), selection));
    REQUIRE(group.has_value());
    REQUIRE(group->Elements.size() == 1);
    CHECK(group->Elements[0]->GetComponent().GetType() == Core::ComponentType::INITIAL_VOLTAGE);
    CHECK(group->Elements[0]->GetComponent().GetValue() == 1.8);
}

TEST_CASE("An RC discharge starts from the voltage of its node on both engines", "[initial_voltage]") {
    // No source: the capacitor holds the node at 3 V while the starting point is found, then it discharges
    Discharge discharge(false);
    discharge.Circuit.AddInitialVoltage({.Node = 1, .Volts = 3.0});
    REQUIRE_FALSE(Core::CheckCircuit(discharge.Circuit).has_value());
    for (const Core::Transient &transient : RunOnBothEngines(discharge.Circuit, FIVE_MILLISECONDS)) {
        CHECK_THAT(transient.NodeVoltages[1].front(), WithinAbs(3.0, 1e-3));
        CHECK_THAT(VoltageAt(transient, 1, 1e-3), WithinAbs(3.0 * std::exp(-1.0), 0.02));
        CHECK_THAT(VoltageAt(transient, 1, 3e-3), WithinAbs(3.0 * std::exp(-3.0), 0.02));
        // The hold is released: the node ends near 0 V and no warning of skipped starting points is made
        CHECK_THAT(transient.NodeVoltages[1].back(), WithinAbs(0.0, 0.05));
        CHECK(transient.Warnings.empty());
    }
}

TEST_CASE("A held node finds the consistent point of the circuit around it on both engines", "[initial_voltage]") {
    Divider divider;
    divider.Circuit.AddInitialVoltage({.Node = 2, .Volts = 4.0});
    for (const Core::Transient &transient : RunOnBothEngines(divider.Circuit, FIVE_MILLISECONDS)) {
        // Held at 4 V, the two lower resistors split it in half, which is not the 1.67 V of the operating point
        CHECK_THAT(transient.NodeVoltages[2].front(), WithinAbs(4.0, 1e-3));
        CHECK_THAT(transient.NodeVoltages[3].front(), WithinAbs(2.0, 1e-3));
        // Released, the node relaxes to the operating point, 3.33 V and 1.67 V
        CHECK_THAT(transient.NodeVoltages[2].back(), WithinAbs(10.0 / 3.0, 0.01));
        CHECK_THAT(transient.NodeVoltages[3].back(), WithinAbs(5.0 / 3.0, 0.01));
    }
}

TEST_CASE("With the initial condition of a part the node voltage reaches the capacitors on it on both engines",
          "[initial_voltage]") {
    // The capacitor C2 has IC=2, so the transient runs with uic and skips the operating point: the node voltage then
    // only sets the start of the capacitors across it, as in SPICE
    Discharge discharge(true);
    discharge.Circuit.AddInitialVoltage({.Node = 1, .Volts = 3.0});
    for (const Core::Transient &transient : RunOnBothEngines(discharge.Circuit, FIVE_MILLISECONDS)) {
        CHECK_THAT(transient.NodeVoltages[1].front(), WithinAbs(3.0, 1e-3));
        CHECK_THAT(transient.NodeVoltages[2].front(), WithinAbs(2.0, 1e-3));
        CHECK_THAT(VoltageAt(transient, 1, 1e-3), WithinAbs(3.0 * std::exp(-1.0), 0.02));
        CHECK_THAT(VoltageAt(transient, 2, 1e-3), WithinAbs(2.0 * std::exp(-1.0), 0.02));
    }
}

TEST_CASE("The operating point, the DC sweep and the AC sweep ignore initial voltages on both engines",
          "[initial_voltage]") {
    const auto run_all = [](const Core::Circuit &circuit) {
        struct Results {
            std::vector<double> Point;
            std::vector<std::vector<double>> Sweep;
            std::vector<std::vector<double>> Bode;
        } results;
        const Core::OperatingPointRun point = Core::RunOperatingPoint(circuit);
        REQUIRE(point.Result.has_value());
        results.Point = point.Result->NodeVoltages;
        const Core::DCSweepRun sweep =
            Core::RunDCSweep(circuit, {.Swept = {.Source = "V1", .Start = 1.0, .Stop = 3.0, .Step = 1.0}});
        REQUIRE(sweep.Result.has_value());
        results.Sweep = sweep.Result->Curves[0].NodeVoltages;
        const Core::ACSweepRun bode =
            Core::RunACSweep(circuit, {.StartFrequency = 10.0, .StopFrequency = 1e5, .PointsPerDecade = 5});
        REQUIRE(bode.Result.has_value());
        results.Bode = bode.Result->NodeMagnitudesDecibels;
        return results;
    };

    Core::VoltageSource source;
    source.SetName("V1");
    source.SetSourceType(Core::Source::SourceType::AC);
    source.SetAC({.Amplitude = 1.0, .Frequency = 1e3, .Offset = 2.0, .Magnitude = 1.0});
    Core::Resistor resistor;
    resistor.SetName("R1");
    resistor.SetValue(1e3);
    Core::Capacitor capacitor;
    capacitor.SetName("C1");
    capacitor.SetValue(1e-7);
    Core::Ground ground;
    Core::Circuit plain;
    plain.Add(source, {1, 0});
    plain.Add(resistor, {1, 2});
    plain.Add(capacitor, {2, 0});
    plain.Add(ground, {0});
    Core::Circuit marked;
    marked.Add(source, {1, 0});
    marked.Add(resistor, {1, 2});
    marked.Add(capacitor, {2, 0});
    marked.Add(ground, {0});
    marked.AddInitialVoltage({.Node = 2, .Volts = 4.0});

    const auto compare = [&] {
        const auto without = run_all(plain);
        const auto with = run_all(marked);
        CHECK(with.Point == without.Point);
        CHECK(with.Sweep == without.Sweep);
        CHECK(with.Bode == without.Bode);
        // The marker would show: the capacitor node of the operating point is the 2 V of the source
        CHECK_THAT(with.Point[2], WithinAbs(2.0, 1e-6));
    };
    SECTION("ngspice") {
        compare();
    }
    SECTION("Nodalys") {
        const TestSupport::NodalysInUse nodalys_in_use;
        compare();
    }
}

TEST_CASE("The astable multivibrator example needs its initial voltage to start and runs on its own with it",
          "[initial_voltage]") {
    const auto example = std::ranges::find(GUI::GetExamples(), std::string_view("astable_multivibrator.imcsim"),
                                           &GUI::Example::FileName);
    REQUIRE(example != GUI::GetExamples().end());
    auto loaded = GUI::LoadExample(*example);
    REQUIRE(loaded.has_value());
    GUI::Schematic schematic;
    schematic.Replace(std::move(loaded->Elements), std::move(loaded->Wires), std::move(loaded->Settings),
                      std::move(loaded->Subcircuits), std::move(loaded->Models));
    const Core::Circuit circuit = schematic.BuildCircuit();
    REQUIRE(circuit.GetInitialVoltages().size() == 1);
    CHECK(circuit.GetInitialVoltages()[0].Volts == 5.0);

    // Both collectors swing between the saturated level and the supply on both engines
    const Core::TransientSettings settings = schematic.GetSimulationSettings().Transient;
    for (const Core::Transient &transient : RunOnBothEngines(circuit, settings)) {
        for (const std::size_t collector : {std::size_t{1}, std::size_t{4}}) {
            const auto [low, high] = std::ranges::minmax(transient.NodeVoltages[collector]);
            CHECK(low < 0.3);
            CHECK(high > 4.5);
        }
    }

    // Without the part the circuit is exactly symmetric, so its operating point is the balance point with both
    // collectors alike; only rounding noise ever takes a transient from it, at a moment nobody chooses
    const auto marker = std::ranges::find_if(schematic.GetElements(), [](const auto &element) {
        return element->GetComponent().GetType() == Core::ComponentType::INITIAL_VOLTAGE;
    });
    schematic.SelectElement(static_cast<std::size_t>(marker - schematic.GetElements().begin()));
    schematic.DeleteSelection();
    const Core::Circuit bare = schematic.BuildCircuit();
    REQUIRE(bare.GetInitialVoltages().empty());
    // The nodes are numbered again without the part, so the collectors are found from the transistors
    std::vector<std::size_t> collectors;
    for (const Core::CircuitEntry &entry : bare.GetEntries()) {
        if (entry.Part->GetType() == Core::ComponentType::NPN) {
            collectors.push_back(static_cast<std::size_t>(entry.Nodes[0]));
        }
    }
    REQUIRE(collectors.size() == 2);
    const Core::OperatingPointRun point = Core::RunOperatingPoint(bare);
    REQUIRE(point.Result.has_value());
    CHECK_THAT(point.Result->NodeVoltages[collectors[0]], WithinAbs(point.Result->NodeVoltages[collectors[1]], 0.01));
}
