/**
 * @file    block_definitions_tests.cpp
 * @brief   Tests for the checks, names and comparisons that making and editing a block depend on.
 */

#include "circuit/components/net_label.h"
#include "circuit/components/subcircuit.h"
#include "schematic/block_definitions.h"
#include "schematic/element_factory.h"
#include "test_printers.h"
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace {

/**
 * @brief   A pin marker named net at the point, as a block being drawn has.
 */
std::unique_ptr<GUI::UIElement> MakeMarker(const GUI::GridPoint point, const std::string &net) {
    auto marker = GUI::CreateElement(Core::ComponentType::PIN, point, GUI::Rotation::R0);
    static_cast<Core::NetLabel &>(marker->GetComponent()).SetNet(net);
    return marker;
}

GUI::SavedSubcircuit MakeDefinition(const std::string &name) {
    GUI::SavedSubcircuit definition;
    definition.Name = name;
    return definition;
}

} // namespace

TEST_CASE("Pin and parameter names of a block must be unique identifiers", "[block_definitions]") {
    const std::vector<GUI::SavedSubcircuitPin> pins = {{.Name = "IN"}, {.Name = "OUT"}};
    const std::vector<Core::SubcircuitParameter> parameters = {{"GAIN", 1.0}, {"BIAS", 0.0}};
    CHECK(GUI::ValidateMetadata(pins, parameters).has_value());

    CHECK_FALSE(GUI::ValidateMetadata({{.Name = "IN"}, {.Name = "IN"}}, parameters).has_value());
    CHECK_FALSE(GUI::ValidateMetadata({{.Name = "bad name"}}, parameters).has_value());
    CHECK_FALSE(GUI::ValidateMetadata(pins, {{"GAIN", 1.0}, {"GAIN", 2.0}}).has_value());
}

TEST_CASE("Every marker offered needs exactly one pin of the block", "[block_definitions]") {
    const std::vector<GUI::SubcircuitPinCandidate> candidates = {{{0, 0}, "A"}, {{4, 0}, "B"}};
    CHECK(
        GUI::CheckPinChoice(candidates, {{.Name = "B", .Point = {4, 0}}, {.Name = "A", .Point = {0, 0}}}).has_value());
    CHECK_FALSE(GUI::CheckPinChoice(candidates, {{.Name = "A", .Point = {0, 0}}}).has_value());
    CHECK_FALSE(
        GUI::CheckPinChoice(candidates, {{.Name = "A", .Point = {0, 0}}, {.Name = "B", .Point = {9, 9}}}).has_value());
    CHECK_FALSE(
        GUI::CheckPinChoice(candidates, {{.Name = "A", .Point = {0, 0}}, {.Name = "B", .Point = {0, 0}}}).has_value());
}

TEST_CASE("A parameter a part follows cannot be removed from the block", "[block_definitions]") {
    std::vector<std::unique_ptr<GUI::UIElement>> elements;
    elements.push_back(GUI::CreateElement(Core::ComponentType::RESISTOR, {0, 0}, GUI::Rotation::R0));
    elements.back()->GetComponent().SetName("R1");
    elements.back()->GetComponent().SetValueExpression("{RES}");

    CHECK(GUI::ValidateExpressions(elements, {{"RES", 1000.0}}).has_value());
    const auto missing = GUI::ValidateExpressions(elements, {{"OTHER", 1.0}});
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().starts_with("R1 follows {RES}"));
}

TEST_CASE("Pin markers are listed with the names they suggest", "[block_definitions]") {
    std::vector<std::unique_ptr<GUI::UIElement>> elements;
    elements.push_back(MakeMarker({0, 0}, "IN"));
    elements.push_back(GUI::CreateElement(Core::ComponentType::RESISTOR, {10, 10}, GUI::Rotation::R0));
    elements.push_back(MakeMarker({20, 0}, ""));

    const auto all = GUI::CollectSubcircuitPinMarkers(elements, {}, nullptr);
    REQUIRE(all.has_value());
    REQUIRE(all->size() == 2);
    CHECK(all->at(0).SuggestedName == "IN");
    // An unnamed marker is named by its place among the unnamed ones
    CHECK(all->at(1).SuggestedName == "P1");

    const std::set<std::size_t> selected = {2};
    const auto only_selected = GUI::CollectSubcircuitPinMarkers(elements, {}, &selected);
    REQUIRE(only_selected.has_value());
    REQUIRE(only_selected->size() == 1);
    CHECK(only_selected->front().Point == elements[2]->GetTerminals().front());
}

TEST_CASE("Two pin markers with one name are refused", "[block_definitions]") {
    std::vector<std::unique_ptr<GUI::UIElement>> elements;
    elements.push_back(MakeMarker({0, 0}, "IN"));
    elements.push_back(MakeMarker({20, 0}, "IN"));
    CHECK_FALSE(GUI::CollectSubcircuitPinMarkers(elements, {}, nullptr).has_value());
}

TEST_CASE("Two pin markers on one node are refused", "[block_definitions]") {
    std::vector<std::unique_ptr<GUI::UIElement>> elements;
    elements.push_back(MakeMarker({0, 0}, "A"));
    elements.push_back(MakeMarker({8, 0}, "B"));
    std::vector<GUI::UIWire> wires;
    wires.emplace_back(GUI::GridPoint{0, 0}, GUI::GridPoint{8, 0});
    CHECK_FALSE(GUI::CollectSubcircuitPinMarkers(elements, wires, nullptr).has_value());
}

TEST_CASE("A new pin continues the old one at its point, or else the one of its name", "[block_definitions]") {
    const std::vector<GUI::SavedSubcircuitPin> old_pins = {
        {.Name = "IN", .Point = {0, 0}}, {.Name = "OUT", .Point = {4, 0}}, {.Name = "GND", .Point = {8, 0}}};
    // IN was renamed at the same point, OUT moved but kept its name, and the last pin is new
    const std::vector<GUI::SavedSubcircuitPin> new_pins = {
        {.Name = "INPUT", .Point = {0, 0}}, {.Name = "OUT", .Point = {12, 0}}, {.Name = "EXTRA", .Point = {16, 0}}};

    const auto matches = GUI::MatchPins(old_pins, new_pins);

    REQUIRE(matches.size() == 3);
    CHECK(matches[0] == 0);
    CHECK(matches[1] == 1);
    CHECK_FALSE(matches[2].has_value());
}

TEST_CASE("An old pin continues only one new pin", "[block_definitions]") {
    const std::vector<GUI::SavedSubcircuitPin> old_pins = {{.Name = "A", .Point = {0, 0}}};
    const std::vector<GUI::SavedSubcircuitPin> new_pins = {{.Name = "A", .Point = {0, 0}},
                                                           {.Name = "A2", .Point = {0, 0}}};

    const auto matches = GUI::MatchPins(old_pins, new_pins);

    CHECK(matches[0] == 0);
    CHECK_FALSE(matches[1].has_value());
}

TEST_CASE("A free name takes the first suffix no definition has", "[block_definitions]") {
    const std::vector<GUI::SavedSubcircuit> none;
    CHECK(GUI::FindFreeName("amp", none) == "amp_2");

    std::vector<GUI::SavedSubcircuit> definitions;
    definitions.push_back(MakeDefinition("amp"));
    definitions.push_back(MakeDefinition("amp_2"));
    definitions.push_back(MakeDefinition("amp_3"));
    CHECK(GUI::FindFreeName("amp", definitions) == "amp_4");
}

TEST_CASE("Definitions with the same contents describe alike and others do not", "[block_definitions]") {
    GUI::SavedSubcircuit first = MakeDefinition("amp");
    first.Elements.push_back(GUI::CreateElement(Core::ComponentType::RESISTOR, {0, 0}, GUI::Rotation::R0));
    GUI::SavedSubcircuit same = MakeDefinition("amp");
    same.Elements.push_back(GUI::CreateElement(Core::ComponentType::RESISTOR, {0, 0}, GUI::Rotation::R0));
    GUI::SavedSubcircuit other = MakeDefinition("amp");
    other.Elements.push_back(GUI::CreateElement(Core::ComponentType::RESISTOR, {4, 0}, GUI::Rotation::R0));

    CHECK(GUI::DescribeDefinition(first) == GUI::DescribeDefinition(same));
    CHECK(GUI::DescribeDefinition(first) != GUI::DescribeDefinition(other));
}

TEST_CASE("Copied definitions are equal to the originals and share nothing with them", "[block_definitions]") {
    std::vector<GUI::SavedSubcircuit> definitions;
    definitions.push_back(MakeDefinition("amp"));
    definitions.back().Elements.push_back(GUI::CreateElement(Core::ComponentType::RESISTOR, {0, 0}, GUI::Rotation::R0));
    // Reading a file names the parts that have no name, so the original is named too
    definitions.back().Elements.back()->GetComponent().SetName("R1");

    const std::vector<GUI::SavedSubcircuit> copies = GUI::CopyDefinitions(definitions);

    REQUIRE(copies.size() == 1);
    CHECK(GUI::DescribeDefinition(copies.front()) == GUI::DescribeDefinition(definitions.front()));
    CHECK(copies.front().Elements.front().get() != definitions.front().Elements.front().get());
}
