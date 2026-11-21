/**
 * @file    part_heat_tests.cpp
 * @brief   Tests for what the heat color of a part reads: its largest terminal current or the power it absorbs.
 */

#include "overlays/part_heat.h"

#include "schematic/element_factory.h"
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <optional>
#include <vector>

TEST_CASE("Ground, labels, markers and logic displays have no current or power to color them by", "[part_heat]") {
    CHECK(GUI::CarriesCurrentOrPower(Core::ComponentType::RESISTOR));
    CHECK(GUI::CarriesCurrentOrPower(Core::ComponentType::NPN));
    CHECK_FALSE(GUI::CarriesCurrentOrPower(Core::ComponentType::GROUND));
    CHECK_FALSE(GUI::CarriesCurrentOrPower(Core::ComponentType::NET_LABEL));
    CHECK_FALSE(GUI::CarriesCurrentOrPower(Core::ComponentType::INITIAL_VOLTAGE));
    CHECK_FALSE(GUI::CarriesCurrentOrPower(Core::ComponentType::NODESET));
    CHECK_FALSE(GUI::CarriesCurrentOrPower(Core::ComponentType::LOGIC_OUTPUT));
    CHECK_FALSE(GUI::CarriesCurrentOrPower(Core::ComponentType::PIN));
}

TEST_CASE("A part is heated by the largest current through any of its terminals, whatever its sign", "[part_heat]") {
    const std::unique_ptr<GUI::UIElement> resistor =
        GUI::CreateElement(Core::ComponentType::RESISTOR, {0, 0}, GUI::Rotation::R0);
    resistor->GetComponent().SetName("R1");
    CHECK(GUI::GetLargestTerminalCurrent(*resistor, {{"R1", -3e-3}}) == 3e-3);
    // A part the result has no current for reads as none
    CHECK(GUI::GetLargestTerminalCurrent(*resistor, {{"R2", 1.0}}) == 0.0);
}

TEST_CASE("The power of a part is found by its name or by the name of one of its currents", "[part_heat]") {
    const std::vector<GUI::ComponentPower> powers = {{.Name = "R1", .Power = 0.5}, {.Name = "Q1", .Power = 0.2}};
    const std::optional<GUI::ComponentPower> resistor = GUI::FindPower(powers, "R1");
    REQUIRE(resistor.has_value());
    CHECK(resistor->Power == 0.5);
    const std::optional<GUI::ComponentPower> transistor = GUI::FindPower(powers, "Q1.C");
    REQUIRE(transistor.has_value());
    CHECK(transistor->Name == "Q1");
    CHECK_FALSE(GUI::FindPower(powers, "C1").has_value());
}
