/**
 * @file    element_factory_tests.cpp
 * @brief   Tests for the part names and the part search.
 */

#include "element_factory.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <string>

TEST_CASE("GetPartName names parts as the user reads them", "[element_factory]") {
    CHECK(std::string(GUI::GetPartName(Core::ComponentType::VoltageSource)) == "Voltage source");
    CHECK(std::string(GUI::GetPartName(Core::ComponentType::NMOS)) == "N-channel MOSFET");
    CHECK(std::string(GUI::GetPartName(Core::ComponentType::OpAmp)) == "Op-amp");
}

TEST_CASE("FindParts ignores case, spaces and punctuation", "[element_factory]") {
    using Core::ComponentType;
    CHECK(GUI::FindParts("mos") == std::vector{ComponentType::NMOS, ComponentType::PMOS});
    CHECK(GUI::FindParts("OPAMP") == std::vector{ComponentType::OpAmp});
    CHECK(GUI::FindParts("zener diode") == std::vector{ComponentType::ZenerDiode});
    CHECK(GUI::FindParts("xyz").empty());

    SECTION("type names count too") {
        CHECK(GUI::FindParts("nmos") == std::vector{ComponentType::NMOS});
    }
    SECTION("an empty query lists every part") {
        const std::vector<ComponentType> all = GUI::FindParts("");
        CHECK(all.size() == 19);
        CHECK(std::ranges::contains(all, ComponentType::Resistor));
    }
}
