/**
 * @file    element_factory_tests.cpp
 * @brief   Tests for the part names and the part search.
 */

#include "schematic/element_factory.h"

#include "ui_elements/ui_flip_flop.h"
#include "ui_elements/ui_gate.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <string>

TEST_CASE("GetPartName names parts as the user reads them", "[element_factory]") {
    CHECK(std::string(GUI::GetPartName(Core::ComponentType::VOLTAGE_SOURCE)) == "Voltage source");
    CHECK(std::string(GUI::GetPartName(Core::ComponentType::NMOS)) == "N-channel MOSFET");
    CHECK(std::string(GUI::GetPartName(Core::ComponentType::OP_AMP)) == "Op-amp");
    CHECK(std::string(GUI::GetPartName(Core::ComponentType::PUSH_BUTTON)) == "Push button");
    CHECK(std::string(GUI::GetPartName(Core::ComponentType::V_SWITCH)) == "Voltage-controlled switch");
    CHECK(std::string(GUI::GetPartName(Core::ComponentType::RELAY)) == "Relay");
    CHECK(std::string(GUI::GetPartName(Core::ComponentType::FUSE)) == "Fuse");
    CHECK(std::string(GUI::GetPartName(Core::ComponentType::TRANSMISSION_LINE)) == "Transmission line");
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
    CHECK(GUI::FindParts("fuse") == std::vector{ComponentType::FUSE});
    CHECK(GUI::FindParts("thyristor") == std::vector{ComponentType::THYRISTOR});
    CHECK(GUI::FindParts("scr") == std::vector{ComponentType::THYRISTOR});
    CHECK(GUI::FindParts("triac") == std::vector{ComponentType::TRIAC});
    CHECK(GUI::FindParts("transmission line") == std::vector{ComponentType::TRANSMISSION_LINE});
    CHECK(GUI::FindParts("controlled switch") == std::vector{ComponentType::V_SWITCH});
    CHECK(GUI::FindParts("and") == std::vector{ComponentType::AND, ComponentType::NAND});
    CHECK(GUI::FindParts("inverter") == std::vector{ComponentType::NOT});
    CHECK(GUI::FindParts("clock") == std::vector{ComponentType::CLOCK});
    CHECK(GUI::FindParts("logic input") == std::vector{ComponentType::LOGIC_INPUT});
    CHECK(GUI::FindParts("block pin") == std::vector{ComponentType::PIN});
    CHECK(GUI::FindParts("flip flop")
          == std::vector{ComponentType::D_FLIP_FLOP,
                         ComponentType::JK_FLIP_FLOP,
                         ComponentType::T_FLIP_FLOP,
                         ComponentType::SR_FLIP_FLOP});
    CHECK(GUI::FindParts("behavioral")
          == std::vector{ComponentType::BEHAVIORAL_VOLTAGE, ComponentType::BEHAVIORAL_CURRENT});
    CHECK(GUI::FindParts("xyz").empty());

    SECTION("type names count too") {
        CHECK(GUI::FindParts("nmos") == std::vector{ComponentType::NMOS});
    }
    SECTION("an empty query lists every part") {
        const std::vector<ComponentType> all = GUI::FindParts("");
        CHECK(all.size() == 51);
        CHECK(std::ranges::contains(all, ComponentType::RESISTOR));
        CHECK(std::ranges::contains(all, ComponentType::TRANSMISSION_LINE));
        CHECK(std::ranges::contains(all, ComponentType::THYRISTOR));
        CHECK(std::ranges::contains(all, ComponentType::TRIAC));
        CHECK(std::ranges::contains(all, ComponentType::CLOCK));
        CHECK(std::ranges::contains(all, ComponentType::LOGIC_INPUT));
    }
}

TEST_CASE("Gates and flip-flops are drawn by symbols of their own", "[element_factory]") {
    for (const Core::ComponentType type : {Core::ComponentType::AND,
                                           Core::ComponentType::OR,
                                           Core::ComponentType::NOT,
                                           Core::ComponentType::NAND,
                                           Core::ComponentType::NOR,
                                           Core::ComponentType::XOR,
                                           Core::ComponentType::XNOR,
                                           Core::ComponentType::BUFFER}) {
        const std::unique_ptr<GUI::UIElement> gate = GUI::CreateElement(type, {0, 0}, GUI::Rotation::R0);
        CHECK(dynamic_cast<const GUI::UIGate *>(gate.get()) != nullptr);
        CHECK(gate->GetComponent().GetType() == type);
    }
    for (const Core::ComponentType type : {Core::ComponentType::D_FLIP_FLOP,
                                           Core::ComponentType::JK_FLIP_FLOP,
                                           Core::ComponentType::T_FLIP_FLOP,
                                           Core::ComponentType::SR_FLIP_FLOP}) {
        const std::unique_ptr<GUI::UIElement> flip_flop = GUI::CreateElement(type, {0, 0}, GUI::Rotation::R0);
        CHECK(dynamic_cast<const GUI::UIFlipFlop *>(flip_flop.get()) != nullptr);
        CHECK(flip_flop->GetComponent().GetType() == type);
    }
}
