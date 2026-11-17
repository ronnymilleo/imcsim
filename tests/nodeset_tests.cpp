/**
 * @file    nodeset_tests.cpp
 * @brief   Tests for the nodeset part: its place in the schematic, the checks, the netlist and what each engine does
 *          with it when it looks for an operating point.
 * @details The reference is a bistable circuit, which has two stable operating points: the nodeset is the guess that
 *          picks the one it lands on, on both engines, in the operating point and in the transient that starts from
 *          it. A divider shows that a nodeset on a circuit with one operating point changes nothing.
 */

#include "circuit/components/bjt.h"
#include "circuit/components/capacitor.h"
#include "circuit/components/ground.h"
#include "circuit/components/nodeset.h"
#include "circuit/components/resistor.h"
#include "circuit/components/subcircuit.h"
#include "circuit/components/vcc.h"
#include "circuit/node_voltage_targets.h"
#include "circuit/subcircuit.h"
#include "nodalys_in_use.h"
#include "schematic/clipboard.h"
#include "schematic/element_factory.h"
#include "schematic/part_catalog.h"
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

/// Collector voltage, in volts, of a saturated transistor of the bistable below, at most.
constexpr double LOW_LEVEL_VOLTS = 0.5;
/// Collector voltage, in volts, of a cut-off transistor of the bistable below, at least.
constexpr double HIGH_LEVEL_VOLTS = 4.0;

/**
 * @struct  Bistable
 * @brief   Two NPN transistors with cross-coupled collectors and bases: node 1 is the 5 V rail, node 2 and node 3 are
 *          the collectors.
 * @details The circuit has two stable operating points, one with node 2 low and node 3 high and the other the other
 *          way round, and an unstable one in between.
 */
struct Bistable {
    Core::VCC Supply;
    Core::BJT First{Core::ComponentType::NPN};
    Core::BJT Second{Core::ComponentType::NPN};
    Core::Resistor FirstLoad;
    Core::Resistor SecondLoad;
    Core::Resistor FirstFeedback;
    Core::Resistor SecondFeedback;
    Core::Ground Reference;
    Core::Circuit Circuit;

    Bistable() {
        Supply.SetName("V1");
        Supply.SetValue(5.0);
        First.SetName("Q1");
        Second.SetName("Q2");
        for (Core::Resistor *load : {&FirstLoad, &SecondLoad}) {
            load->SetValue(1e3);
        }
        for (Core::Resistor *feedback : {&FirstFeedback, &SecondFeedback}) {
            feedback->SetValue(10e3);
        }
        FirstLoad.SetName("R1");
        SecondLoad.SetName("R2");
        FirstFeedback.SetName("R3");
        SecondFeedback.SetName("R4");
        Circuit.Add(Supply, {1});
        // Node 4 is the base of the first transistor and node 5 the base of the second
        Circuit.Add(First, {2, 4, 0});
        Circuit.Add(Second, {3, 5, 0});
        Circuit.Add(FirstLoad, {1, 2});
        Circuit.Add(SecondLoad, {1, 3});
        Circuit.Add(FirstFeedback, {2, 5});
        Circuit.Add(SecondFeedback, {3, 4});
        Circuit.Add(Reference, {0});
    }
};

/// Whether node 2 is low and node 3 high, or the other way round, in the voltages of a result.
enum class State {
    FIRST_ON,
    SECOND_ON,
    OTHER
};

State ReadState(const std::vector<double> &voltages) {
    if (voltages[2] < LOW_LEVEL_VOLTS && voltages[3] > HIGH_LEVEL_VOLTS) {
        return State::FIRST_ON;
    }
    if (voltages[3] < LOW_LEVEL_VOLTS && voltages[2] > HIGH_LEVEL_VOLTS) {
        return State::SECOND_ON;
    }
    return State::OTHER;
}

/// Starts the operating point near the state in which the first transistor conducts, or the second one.
void SetNodesets(Core::Circuit &circuit, const State wanted) {
    const bool is_first_on = wanted == State::FIRST_ON;
    circuit.AddNodeset({.Node = 2, .Volts = is_first_on ? 0.2 : 5.0});
    circuit.AddNodeset({.Node = 3, .Volts = is_first_on ? 5.0 : 0.2});
}

State OperatingPointState(const Core::Circuit &circuit) {
    const Core::OperatingPointRun run = Core::RunOperatingPoint(circuit);
    REQUIRE(run.Result.has_value());
    return ReadState(run.Result->NodeVoltages);
}

State StartOfTransientState(const Core::Circuit &circuit) {
    const Core::TransientRun run = Core::RunTransient(circuit, {.StopTime = 1e-3, .TimeStep = 10e-6});
    REQUIRE(run.Result.has_value());
    std::vector<double> start;
    std::vector<double> end;
    for (const std::vector<double> &node : run.Result->NodeVoltages) {
        start.push_back(node.front());
        end.push_back(node.back());
    }
    // A circuit that starts in a stable state stays in it
    REQUIRE(ReadState(start) == ReadState(end));
    return ReadState(start);
}

/// Runs a check on ngspice and then on Nodalys.
template <typename Check> void OnBothEngines(Check check) {
    check();
    const TestSupport::NodalysInUse nodalys_in_use;
    check();
}

/// A schematic of a resistor to ground on a wire that a nodeset marker of the given value also touches.
struct MarkedResistor {
    GUI::Schematic Schematic;

    explicit MarkedResistor(const double volts) {
        Schematic.AddElement(GUI::CreateElement(Core::ComponentType::RESISTOR, {10, 10}, GUI::Rotation::R90));
        Schematic.AddElement(GUI::CreateElement(Core::ComponentType::GROUND, {10, 14}, GUI::Rotation::R0));
        Schematic.AddWire({10, 12}, {10, 14});
        AddMarker({10, 8}, volts);
    }

    void AddMarker(const GUI::GridPoint position, const double volts) {
        auto marker = GUI::CreateElement(Core::ComponentType::NODESET, position, GUI::Rotation::R0);
        marker->GetComponent().SetValue(volts);
        Schematic.AddElement(std::move(marker));
    }
};

/**
 * @brief   A block that holds the bistable, with a nodeset on its collectors: the pins are the rail and the two
 *          collectors, which the outer circuit reads.
 */
struct BistableBlock {
    Bistable Body;
    std::shared_ptr<Core::SubcircuitDefinition> Definition = std::make_shared<Core::SubcircuitDefinition>();

    explicit BistableBlock(const State wanted) {
        // The supply is outside the block, on the rail pin
        Core::Circuit &circuit = Definition->Body;
        circuit.Add(Body.First, {2, 4, 0});
        circuit.Add(Body.Second, {3, 5, 0});
        circuit.Add(Body.FirstLoad, {1, 2});
        circuit.Add(Body.SecondLoad, {1, 3});
        circuit.Add(Body.FirstFeedback, {2, 5});
        circuit.Add(Body.SecondFeedback, {3, 4});
        SetNodesets(circuit, wanted);
        Definition->Name = "FLIP";
        Definition->Pins = {{.Name = "VCC", .Node = 1}, {.Name = "Q", .Node = 2}, {.Name = "QB", .Node = 3}};
    }
};

} // namespace

TEST_CASE("A nodeset has a value in volts, no name, and is found by its name", "[nodeset]") {
    const auto marker = GUI::CreateElement(Core::ComponentType::NODESET, {0, 0}, GUI::Rotation::R0);
    const Core::Component &part = marker->GetComponent();
    CHECK(part.GetType() == Core::ComponentType::NODESET);
    CHECK(part.HasValue());
    CHECK(std::string_view(part.GetUnit()) == "V");
    CHECK(std::string_view(part.GetNamePrefix()).empty());
    CHECK(part.GetValue() == 2.5);
    CHECK(part.IsValidValue(-3.0));
    CHECK_FALSE(part.IsValidValue(std::nan("")));
    CHECK(Core::ParseComponentType("Nodeset") == Core::ComponentType::NODESET);
    CHECK(part.Clone()->GetType() == Core::ComponentType::NODESET);
    CHECK(std::string_view(GUI::GetPartName(Core::ComponentType::NODESET)) == "Nodeset");
    CHECK(std::ranges::contains(GUI::FindParts("nodeset"), Core::ComponentType::NODESET));
    // It touches one node, like a net label
    CHECK(marker->GetTerminals() == std::vector<GUI::GridPoint>{{0, 0}});
}

TEST_CASE("A marker on a wire gives its node a nodeset and stays out of the parts and the initial voltages",
          "[nodeset]") {
    MarkedResistor setup(3.0);
    const Core::Circuit circuit = setup.Schematic.BuildCircuit();
    // The resistor and the ground only: the marker writes no element
    CHECK(circuit.GetEntries().size() == 2);
    CHECK(circuit.GetInitialVoltages().empty());
    REQUIRE(circuit.GetNodesets().size() == 1);
    CHECK(circuit.GetNodesets()[0].Volts == 3.0);
    CHECK(circuit.GetNodesets()[0].Node == circuit.GetEntries()[0].Nodes[0]);
    CHECK_FALSE(Core::CheckCircuit(circuit).has_value());
}

TEST_CASE("A nodeset and an initial voltage on one node may differ, since one is a guess", "[nodeset]") {
    MarkedResistor setup(3.0);
    auto initial = GUI::CreateElement(Core::ComponentType::INITIAL_VOLTAGE, {10, 6}, GUI::Rotation::R0);
    initial->GetComponent().SetValue(1.0);
    setup.Schematic.AddElement(std::move(initial));
    setup.Schematic.AddWire({10, 6}, {10, 8});
    CHECK_FALSE(Core::CheckCircuit(setup.Schematic.BuildCircuit()).has_value());
}

TEST_CASE("Two nodesets on one node agree or are refused, naming both voltages", "[nodeset]") {
    SECTION("The same voltage is fine and is written once") {
        MarkedResistor setup(3.0);
        setup.AddMarker({10, 6}, 3.0);
        setup.Schematic.AddWire({10, 6}, {10, 8});
        const Core::Circuit circuit = setup.Schematic.BuildCircuit();
        CHECK(circuit.GetNodesets().size() == 2);
        CHECK_FALSE(Core::CheckCircuit(circuit).has_value());
        const std::string netlist = circuit.ToSpiceNetlist();
        const std::size_t first = netlist.find(".nodeset V(");
        REQUIRE(first != std::string::npos);
        CHECK(netlist.find(".nodeset V(", first + 1) == std::string::npos);
    }
    SECTION("Different voltages are an error with both values") {
        MarkedResistor setup(3.0);
        setup.AddMarker({10, 6}, 1.5);
        setup.Schematic.AddWire({10, 6}, {10, 8});
        const std::optional<std::string> problem = Core::CheckCircuit(setup.Schematic.BuildCircuit());
        REQUIRE(problem.has_value());
        CHECK_THAT(*problem, ContainsSubstring("two nodesets"));
        CHECK_THAT(*problem, ContainsSubstring("3 V"));
        CHECK_THAT(*problem, ContainsSubstring("1.5 V"));
    }
}

TEST_CASE("A nodeset on ground or on a node nothing is connected to is refused", "[nodeset]") {
    Core::Resistor resistor;
    resistor.SetName("R1");
    Core::Ground ground;
    Core::Circuit circuit;
    circuit.Add(resistor, {1, 0});
    circuit.Add(ground, {0});
    SECTION("Ground is always 0 V") {
        circuit.AddNodeset({.Node = 0, .Volts = 2.0});
        const std::optional<std::string> problem = Core::CheckCircuit(circuit);
        REQUIRE(problem.has_value());
        CHECK_THAT(*problem, ContainsSubstring("A nodeset is on ground"));
    }
    SECTION("A loose node has no equations to start from") {
        circuit.AddNodeset({.Node = 5, .Volts = 2.0});
        const std::optional<std::string> problem = Core::CheckCircuit(circuit);
        REQUIRE(problem.has_value());
        CHECK_THAT(*problem, ContainsSubstring("A nodeset of 2 V is on node 5, which no part is connected to"));
    }
}

TEST_CASE("A nodeset inside a block is checked where it sits and per instance", "[nodeset]") {
    BistableBlock block(State::FIRST_ON);
    Core::SubcircuitInstance instance("FLIP", {"VCC", "Q", "QB"});
    instance.SetName("X1");
    Core::VCC supply;
    supply.SetName("V1");
    Core::Ground ground;
    Core::Circuit circuit;
    circuit.AddSubcircuit(block.Definition);
    circuit.Add(supply, {1});
    circuit.Add(instance, {1, 2, 3});
    circuit.Add(ground, {0});
    CHECK_FALSE(Core::CheckCircuit(circuit).has_value());

    // The nodes of the pins are the outer nodes of the instance
    const std::vector<Core::NodeVoltageTarget> targets =
        Core::ResolveNodeVoltages(circuit, Core::NodeVoltageKind::NODESET);
    REQUIRE(targets.size() == 2);
    CHECK(targets[0].Node == 2);
    CHECK(targets[0].Volts == 0.2);
    CHECK(targets[1].Node == 3);
    CHECK(targets[1].Volts == 5.0);
    CHECK(Core::ResolveNodeVoltages(circuit, Core::NodeVoltageKind::INITIAL).empty());

    SECTION("Another nodeset on the outer node must agree") {
        circuit.AddNodeset({.Node = 2, .Volts = 3.0});
        const std::optional<std::string> problem = Core::CheckCircuit(circuit);
        REQUIRE(problem.has_value());
        CHECK_THAT(*problem, ContainsSubstring("two nodesets"));
    }
    SECTION("A nodeset in a body on a node no part of the body touches is refused") {
        block.Definition->Body.AddNodeset({.Node = 9, .Volts = 1.0});
        const std::optional<std::string> problem = Core::CheckCircuit(circuit);
        REQUIRE(problem.has_value());
        CHECK_THAT(*problem, ContainsSubstring("in block FLIP"));
    }
}

TEST_CASE("The netlist writes a nodeset as .nodeset and no element, apart from the initial voltages", "[nodeset]") {
    MarkedResistor setup(3.0);
    const Core::Circuit circuit = setup.Schematic.BuildCircuit();
    const std::string netlist = circuit.ToSpiceNetlist();
    CHECK_THAT(netlist, ContainsSubstring(std::format(".nodeset V({})=3\n", circuit.GetEntries()[0].Nodes[0])));
    CHECK_THAT(netlist, !ContainsSubstring(".ic"));
    CHECK(std::ranges::count(netlist, '\n') == 4);
}

TEST_CASE("The netlist writes the nodeset of each instance of a block at the top, and none inside it", "[nodeset]") {
    BistableBlock block(State::FIRST_ON);
    Core::SubcircuitInstance first("FLIP", {"VCC", "Q", "QB"});
    first.SetName("X1");
    Core::SubcircuitInstance second("FLIP", {"VCC", "Q", "QB"});
    second.SetName("X2");
    Core::VCC supply;
    supply.SetName("V1");
    Core::Ground ground;
    Core::Circuit circuit;
    circuit.AddSubcircuit(block.Definition);
    circuit.Add(supply, {1});
    circuit.Add(first, {1, 2, 3});
    circuit.Add(second, {1, 4, 5});
    circuit.Add(ground, {0});
    const std::string netlist = circuit.ToSpiceNetlist();
    // The pins are outer nodes, so each instance sets the node its pins join
    CHECK_THAT(netlist, ContainsSubstring(".nodeset V(2)=200m\n"));
    CHECK_THAT(netlist, ContainsSubstring(".nodeset V(5)=5\n"));
    const std::size_t subcircuit = netlist.find(".subckt");
    REQUIRE(subcircuit != std::string::npos);
    CHECK(netlist.find(".nodeset", subcircuit) == std::string::npos);
}

TEST_CASE("The circuit copies carry the nodesets", "[nodeset]") {
    Core::Resistor resistor;
    resistor.SetName("R1");
    Core::Circuit circuit;
    circuit.Add(resistor, {1, 0});
    circuit.AddNodeset({.Node = 1, .Volts = 4.0});

    const Core::CircuitCopy copy(circuit);
    REQUIRE(copy.GetCircuit().GetNodesets().size() == 1);
    CHECK(copy.GetCircuit().GetNodesets()[0].Volts == 4.0);

    Core::Circuit derived;
    derived.Add(resistor, {1, 0});
    derived.AddDefinitionsOf(circuit);
    REQUIRE(derived.GetNodesets().size() == 1);
    CHECK(derived.GetNodesets()[0].Node == 1);
}

TEST_CASE("A saved schematic keeps its nodesets and copying a marker keeps its voltage", "[nodeset]") {
    MarkedResistor setup(-1.25);
    const std::string text = GUI::SaveSchematic(setup.Schematic.GetElements(), setup.Schematic.GetWires(),
                                                setup.Schematic.GetSimulationSettings(), {});
    const auto loaded = GUI::LoadSchematic(text);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->Elements.size() == 3);
    const Core::Component &marker = loaded->Elements[2]->GetComponent();
    CHECK(marker.GetType() == Core::ComponentType::NODESET);
    CHECK(marker.GetValue() == -1.25);
    CHECK(loaded->Warnings.empty());

    const GUI::Selection selection{.Elements = {2}, .Wires = {}};
    auto group = GUI::ParseCopiedSchematic(
        GUI::CopySelection(setup.Schematic.GetElements(), setup.Schematic.GetWires(), selection));
    REQUIRE(group.has_value());
    REQUIRE(group->Elements.size() == 1);
    CHECK(group->Elements[0]->GetComponent().GetType() == Core::ComponentType::NODESET);
    CHECK(group->Elements[0]->GetComponent().GetValue() == -1.25);
}

TEST_CASE("The nodeset picks the stable state the operating point lands on, on both engines", "[nodeset]") {
    for (const State wanted : {State::FIRST_ON, State::SECOND_ON}) {
        Bistable setup;
        SetNodesets(setup.Circuit, wanted);
        REQUIRE_FALSE(Core::CheckCircuit(setup.Circuit).has_value());
        OnBothEngines([&] { CHECK(OperatingPointState(setup.Circuit) == wanted); });
    }
}

TEST_CASE("The transient starts from the state the nodeset picked, on both engines", "[nodeset]") {
    for (const State wanted : {State::FIRST_ON, State::SECOND_ON}) {
        Bistable setup;
        SetNodesets(setup.Circuit, wanted);
        OnBothEngines([&] { CHECK(StartOfTransientState(setup.Circuit) == wanted); });
    }
}

TEST_CASE("A nodeset inside a block picks the state through the pins of each instance, on both engines", "[nodeset]") {
    for (const State wanted : {State::FIRST_ON, State::SECOND_ON}) {
        BistableBlock block(wanted);
        Core::SubcircuitInstance instance("FLIP", {"VCC", "Q", "QB"});
        instance.SetName("X1");
        Core::VCC supply;
        supply.SetName("V1");
        supply.SetValue(5.0);
        Core::Ground ground;
        Core::Circuit circuit;
        circuit.AddSubcircuit(block.Definition);
        circuit.Add(supply, {1});
        circuit.Add(instance, {1, 2, 3});
        circuit.Add(ground, {0});
        REQUIRE_FALSE(Core::CheckCircuit(circuit).has_value());
        OnBothEngines([&] { CHECK(OperatingPointState(circuit) == wanted); });
    }
}

TEST_CASE("A nodeset on a circuit with one operating point changes nothing on both engines", "[nodeset]") {
    const auto make = [](Core::Circuit &circuit, Core::VCC &supply, std::vector<Core::Resistor> &resistors,
                         Core::Capacitor &capacitor, Core::Ground &ground) {
        supply.SetName("V1");
        supply.SetValue(5.0);
        for (std::size_t index = 0; index < resistors.size(); ++index) {
            resistors[index].SetName(std::format("R{}", index + 1));
            resistors[index].SetValue(1e3);
        }
        capacitor.SetName("C1");
        capacitor.SetValue(1e-6);
        circuit.Add(supply, {1});
        circuit.Add(resistors[0], {1, 2});
        circuit.Add(resistors[1], {2, 3});
        circuit.Add(resistors[2], {3, 0});
        circuit.Add(capacitor, {3, 0});
        circuit.Add(ground, {0});
    };
    Core::VCC supply;
    std::vector<Core::Resistor> resistors(3);
    Core::Capacitor capacitor;
    Core::Ground ground;
    Core::Circuit plain;
    make(plain, supply, resistors, capacitor, ground);
    Core::Circuit guessed;
    make(guessed, supply, resistors, capacitor, ground);
    // A guess far from the solution, which the release brings back
    guessed.AddNodeset({.Node = 2, .Volts = 4.0});
    guessed.AddNodeset({.Node = 3, .Volts = -2.0});

    const auto compare = [&] {
        const Core::OperatingPointRun without = Core::RunOperatingPoint(plain);
        const Core::OperatingPointRun with = Core::RunOperatingPoint(guessed);
        REQUIRE(without.Result.has_value());
        REQUIRE(with.Result.has_value());
        for (std::size_t node = 0; node < without.Result->NodeVoltages.size(); ++node) {
            CHECK_THAT(with.Result->NodeVoltages[node], WithinAbs(without.Result->NodeVoltages[node], 1e-6));
        }
        // The nodeset is released, so the node is where the circuit puts it and not where it was held
        CHECK_THAT(with.Result->NodeVoltages[2], WithinAbs(10.0 / 3.0, 1e-3));

        const Core::DCSweepRun sweep =
            Core::RunDCSweep(guessed, {.Swept = {.Source = "V1", .Start = 1.0, .Stop = 3.0, .Step = 1.0}});
        REQUIRE(sweep.Result.has_value());
        CHECK_THAT(sweep.Result->Curves[0].NodeVoltages[2].back(), WithinAbs(2.0, 1e-3));

        const Core::TransientRun transient = Core::RunTransient(guessed, {.StopTime = 1e-3, .TimeStep = 10e-6});
        REQUIRE(transient.Result.has_value());
        CHECK_THAT(transient.Result->NodeVoltages[2].front(), WithinAbs(10.0 / 3.0, 1e-3));
        CHECK_THAT(transient.Result->NodeVoltages[2].back(), WithinAbs(10.0 / 3.0, 1e-3));
    };
    OnBothEngines(compare);
}
