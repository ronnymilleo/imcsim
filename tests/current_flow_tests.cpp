/**
 * @file    current_flow_tests.cpp
 * @brief   Tests for the current along each piece of wire.
 */

#include "current_flow.h"
#include "element_factory.h"
#include "test_printers.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <memory>
#include <string>
#include <vector>

namespace {

std::unique_ptr<GUI::UIElement> Part(const Core::ComponentType type, const GUI::GridPoint position,
                                     const std::string &name) {
    auto element = GUI::CreateElement(type, position, GUI::Rotation::R0);
    element->GetComponent().SetName(name);
    return element;
}

const GUI::WireCurrent &FindPiece(const std::vector<GUI::WireCurrent> &pieces, const GUI::GridPoint start,
                                  const GUI::GridPoint end) {
    const auto piece = std::ranges::find_if(
        pieces, [&](const GUI::WireCurrent &candidate) { return candidate.Start == start && candidate.End == end; });
    REQUIRE(piece != pieces.end());
    return *piece;
}

} // namespace

TEST_CASE("Terminal currents follow the part: into the first terminal, out of the second", "[current_flow]") {
    const std::vector<Core::ComponentCurrent> currents = {{"R1", 2e-3}, {"Q1.C", 5e-3}, {"Q1.B", 1e-5}};
    const auto resistor = Part(Core::ComponentType::Resistor, {0, 0}, "R1");
    CHECK(GUI::GetTerminalCurrent(resistor->GetComponent(), 0, currents) == 2e-3);
    CHECK(GUI::GetTerminalCurrent(resistor->GetComponent(), 1, currents) == -2e-3);
    const auto transistor = Part(Core::ComponentType::NPN, {0, 0}, "Q1");
    CHECK(GUI::GetTerminalCurrent(transistor->GetComponent(), 0, currents) == 5e-3);
    CHECK(GUI::GetTerminalCurrent(transistor->GetComponent(), 1, currents) == 1e-5);
    // The emitter current is missing from these results, and ground never has one
    CHECK_FALSE(GUI::GetTerminalCurrent(transistor->GetComponent(), 2, currents));
    const auto ground = Part(Core::ComponentType::Ground, {0, 0}, "");
    CHECK_FALSE(GUI::GetTerminalCurrent(ground->GetComponent(), 0, currents));
}

TEST_CASE("Wires carry the current of the parts in a series loop", "[current_flow]") {
    // A 1 mA supply at (0, 0) wired to R1 from (4, 0) to (8, 0), and R1 wired down to ground at (8, 4)
    std::vector<std::unique_ptr<GUI::UIElement>> elements;
    elements.push_back(Part(Core::ComponentType::VCC, {0, 0}, "V1"));
    elements.push_back(Part(Core::ComponentType::Resistor, {6, 0}, "R1"));
    elements.push_back(Part(Core::ComponentType::Ground, {8, 4}, ""));
    const std::vector<GUI::UIWire> wires = {{{0, 0}, {4, 0}}, {{8, 0}, {8, 4}}};
    const std::vector<Core::ComponentCurrent> currents = {{"V1", -1e-3}, {"R1", 1e-3}};

    const std::vector<GUI::WireCurrent> pieces = GUI::ComputeWireCurrents(elements, wires, currents);
    REQUIRE(pieces.size() == 2);
    CHECK_THAT(FindPiece(pieces, {0, 0}, {4, 0}).Current, Catch::Matchers::WithinAbs(1e-3, 1e-12));
    CHECK_THAT(FindPiece(pieces, {8, 0}, {8, 4}).Current, Catch::Matchers::WithinAbs(1e-3, 1e-12));
}

TEST_CASE("A ground in the middle of a wire splits it where the return currents meet", "[current_flow]") {
    // R2 returns 2 mA at (0, 10) and R3 returns 3 mA at (10, 10), both into a ground wire grounded at (5, 10)
    std::vector<std::unique_ptr<GUI::UIElement>> elements;
    elements.push_back(Part(Core::ComponentType::Resistor, {-2, 10}, "R2"));
    elements.push_back(Part(Core::ComponentType::Resistor, {12, 10}, "R3"));
    elements.push_back(Part(Core::ComponentType::Ground, {5, 10}, ""));
    const std::vector<GUI::UIWire> wires = {{{0, 10}, {10, 10}}};
    const std::vector<Core::ComponentCurrent> currents = {{"R2", 2e-3}, {"R3", -3e-3}};

    const std::vector<GUI::WireCurrent> pieces = GUI::ComputeWireCurrents(elements, wires, currents);
    REQUIRE(pieces.size() == 2);
    CHECK_THAT(FindPiece(pieces, {0, 10}, {5, 10}).Current, Catch::Matchers::WithinAbs(2e-3, 1e-12));
    // Flows from (10, 10) back toward ground, against the direction of the piece
    CHECK_THAT(FindPiece(pieces, {5, 10}, {10, 10}).Current, Catch::Matchers::WithinAbs(-3e-3, 1e-12));
}

TEST_CASE("Wires that close a loop inside a node carry the current on one path only", "[current_flow]") {
    std::vector<std::unique_ptr<GUI::UIElement>> elements;
    elements.push_back(Part(Core::ComponentType::VCC, {0, 0}, "V1"));
    elements.push_back(Part(Core::ComponentType::Resistor, {6, 0}, "R1"));
    // Two wires between the same points
    const std::vector<GUI::UIWire> wires = {{{0, 0}, {4, 0}}, {{0, 0}, {4, 0}}};
    const std::vector<Core::ComponentCurrent> currents = {{"V1", -1e-3}, {"R1", 1e-3}};

    const std::vector<GUI::WireCurrent> pieces = GUI::ComputeWireCurrents(elements, wires, currents);
    REQUIRE(pieces.size() == 2);
    CHECK_THAT(pieces[0].Current + pieces[1].Current, Catch::Matchers::WithinAbs(1e-3, 1e-12));
    CHECK((pieces[0].Current == 0.0 || pieces[1].Current == 0.0));
}
