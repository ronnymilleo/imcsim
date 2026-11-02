/**
 * @file    element_factory_tests.cpp
 * @brief   Tests for the part names and the part search.
 */

#include "element_factory.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <string>

TEST_CASE("GetPartName names parts as the user reads them", "[element_factory]") {
    CHECK(std::string(GUI::GetPartName(Core::ComponentType::VOLTAGE_SOURCE)) == "Voltage source");
    CHECK(std::string(GUI::GetPartName(Core::ComponentType::NMOS)) == "N-channel MOSFET");
    CHECK(std::string(GUI::GetPartName(Core::ComponentType::OP_AMP)) == "Op-amp");
    CHECK(std::string(GUI::GetPartName(Core::ComponentType::PUSH_BUTTON)) == "Push button");
    CHECK(std::string(GUI::GetPartName(Core::ComponentType::V_SWITCH)) == "Voltage-controlled switch");
    CHECK(std::string(GUI::GetPartName(Core::ComponentType::RELAY)) == "Relay");
    CHECK(std::string(GUI::GetPartName(Core::ComponentType::NAND)) == "NAND gate");
    CHECK(std::string(GUI::GetPartName(Core::ComponentType::JK_FLIP_FLOP)) == "JK flip-flop");
    CHECK(std::string(GUI::GetPartName(Core::ComponentType::CLOCK)) == "Clock");
    CHECK(std::string(GUI::GetPartName(Core::ComponentType::LOGIC_INPUT)) == "Logic input");
}

TEST_CASE("FindParts ignores case, spaces and punctuation", "[element_factory]") {
    using Core::ComponentType;
    CHECK(GUI::FindParts("mos") == std::vector{ComponentType::NMOS, ComponentType::PMOS});
    CHECK(GUI::FindParts("OPAMP") == std::vector{ComponentType::OP_AMP});
    CHECK(GUI::FindParts("zener diode") == std::vector{ComponentType::ZENER_DIODE});
    CHECK(GUI::FindParts("relay") == std::vector{ComponentType::RELAY});
    CHECK(GUI::FindParts("controlled switch") == std::vector{ComponentType::V_SWITCH});
    CHECK(GUI::FindParts("and") == std::vector{ComponentType::AND, ComponentType::NAND});
    CHECK(GUI::FindParts("inverter") == std::vector{ComponentType::NOT});
    CHECK(GUI::FindParts("clock") == std::vector{ComponentType::CLOCK});
    CHECK(GUI::FindParts("logic input") == std::vector{ComponentType::LOGIC_INPUT});
    CHECK(GUI::FindParts("flip flop") == std::vector{ComponentType::D_FLIP_FLOP, ComponentType::JK_FLIP_FLOP,
                                                     ComponentType::T_FLIP_FLOP, ComponentType::SR_FLIP_FLOP});
    CHECK(GUI::FindParts("xyz").empty());

    SECTION("type names count too") {
        CHECK(GUI::FindParts("nmos") == std::vector{ComponentType::NMOS});
    }
    SECTION("an empty query lists every part") {
        const std::vector<ComponentType> all = GUI::FindParts("");
        CHECK(all.size() == 40);
        CHECK(std::ranges::contains(all, ComponentType::RESISTOR));
        CHECK(std::ranges::contains(all, ComponentType::CLOCK));
        CHECK(std::ranges::contains(all, ComponentType::LOGIC_INPUT));
    }
}
