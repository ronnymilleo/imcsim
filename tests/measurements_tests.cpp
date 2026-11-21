/**
 * @file    measurements_tests.cpp
 * @brief   Tests for the traces the plots show: measured voltages and currents, math channels and logic buses.
 */

#include "schematic/connectivity.h"
#include "schematic/element_factory.h"
#include "schematic/measurements.h"
#include "test_printers.h"
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <optional>
#include <vector>

namespace {

/**
 * @struct  TwoNets
 * @brief   A resistor from (-2, 0) to (2, 0) with a wire from (2, 0) to (6, 0), and a ground at (0, 8) joined by a wire
 *          from (-4, 8) to it.
 */
struct TwoNets {
    std::vector<std::unique_ptr<GUI::UIElement>> Elements;
    std::vector<GUI::UIWire> Wires;

    TwoNets() {
        Elements.push_back(GUI::CreateElement(Core::ComponentType::RESISTOR, {0, 0}, GUI::Rotation::R0));
        Elements.push_back(GUI::CreateElement(Core::ComponentType::GROUND, {0, 8}, GUI::Rotation::R0));
        Wires.emplace_back(GUI::GridPoint{2, 0}, GUI::GridPoint{6, 0});
        Wires.emplace_back(GUI::GridPoint{-4, 8}, GUI::GridPoint{0, 8});
    }
};

} // namespace

TEST_CASE("A voltage is measured once, however many times it is asked for", "[measurements]") {
    const TwoNets schematic;
    const GUI::Connectivity connectivity(schematic.Elements, schematic.Wires);
    const int node = *connectivity.GetNode({6, 0});
    GUI::Measurements measurements;

    measurements.SetVoltageMeasured(node, true, connectivity);
    measurements.SetVoltageMeasured(node, true, connectivity);
    CHECK(measurements.IsVoltageMeasured(node));
    CHECK(measurements.Save(connectivity).Voltages.size() == 1);

    measurements.SetVoltageMeasured(node, false, connectivity);
    CHECK_FALSE(measurements.IsVoltageMeasured(node));
}

TEST_CASE("Renaming a part keeps its currents and the math channels that read them on it", "[measurements]") {
    GUI::Measurements measurements;
    measurements.SetCurrentMeasured("R1", true);
    measurements.SetCurrentMeasured("Q1.C", true);
    measurements.SetMathChannels({{.Name = "M1", .First = "I(Q1.C)", .Second = "I(R1)", .Expression = "I(R10)*2"}});

    measurements.RenamePart("Q1", "QOUT");

    CHECK(measurements.IsCurrentMeasured("QOUT.C"));
    CHECK_FALSE(measurements.IsCurrentMeasured("Q1.C"));
    // A part whose name only starts the same is another part
    CHECK(measurements.IsCurrentMeasured("R1"));
    REQUIRE(measurements.GetMathChannels().size() == 1);
    CHECK(measurements.GetMathChannels()[0].First == "I(QOUT.C)");
    CHECK(measurements.GetMathChannels()[0].Second == "I(R1)");
    CHECK(measurements.GetMathChannels()[0].Expression == "I(R10)*2");
}

TEST_CASE("Saved measurements come back, without the points that lead to ground", "[measurements]") {
    const TwoNets schematic;
    const GUI::Connectivity connectivity(schematic.Elements, schematic.Wires);
    const int node = *connectivity.GetNode({6, 0});
    GUI::Measurements measurements;
    measurements.SetVoltageMeasured(node, true, connectivity);
    measurements.SetCurrentMeasured("R1", true);
    measurements.SetBuses({{.Name = "BUS", .Bits = {"A", "B"}}});
    GUI::SavedMeasurements saved = measurements.Save(connectivity);
    saved.Voltages.push_back({-4, 8});

    GUI::Measurements restored;
    restored.SetCurrentMeasured("R9", true);
    restored.Restore(saved, connectivity);

    CHECK(restored.IsVoltageMeasured(node));
    CHECK(restored.Save(connectivity).Voltages.size() == 1);
    CHECK(restored.IsCurrentMeasured("R1"));
    // Restoring replaces what was there
    CHECK_FALSE(restored.IsCurrentMeasured("R9"));
    CHECK(restored.GetBuses() == measurements.GetBuses());
}

TEST_CASE("Clearing the voltage probes keeps the currents, math channels and buses", "[measurements]") {
    const TwoNets schematic;
    const GUI::Connectivity connectivity(schematic.Elements, schematic.Wires);
    const int node = *connectivity.GetNode({6, 0});
    GUI::Measurements measurements;
    measurements.SetVoltageMeasured(node, true, connectivity);
    measurements.SetCurrentMeasured("R1", true);
    measurements.SetMathChannels({{.Name = "M1", .Expression = "I(R1)"}});
    measurements.SetBuses({{.Name = "BUS", .Bits = {"A"}}});

    measurements.ClearVoltageProbes();
    CHECK_FALSE(measurements.IsVoltageMeasured(node));
    CHECK(measurements.IsCurrentMeasured("R1"));
    CHECK(measurements.GetMathChannels().size() == 1);
    CHECK(measurements.GetBuses().size() == 1);

    measurements.Clear();
    CHECK_FALSE(measurements.IsCurrentMeasured("R1"));
    CHECK(measurements.GetMathChannels().empty());
    CHECK(measurements.GetBuses().empty());
}

TEST_CASE("A probe follows its net when the nodes are numbered again", "[measurements]") {
    TwoNets schematic;
    const GUI::Connectivity before(schematic.Elements, schematic.Wires);
    const int node = *before.GetNode({6, 0});
    GUI::Measurements measurements;
    measurements.SetVoltageMeasured(node, true, before);

    // A second resistor ahead of the first in the list can take the number the net had
    schematic.Elements.insert(schematic.Elements.begin(),
                              GUI::CreateElement(Core::ComponentType::RESISTOR, {0, 20}, GUI::Rotation::R0));
    const GUI::Connectivity after(schematic.Elements, schematic.Wires);
    measurements.FollowNodes(after);

    CHECK(measurements.IsVoltageMeasured(*after.GetNode({6, 0})));
}
