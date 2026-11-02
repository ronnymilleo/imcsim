/**
 * @file    live_change_tests.cpp
 * @brief   Tests for what a live transient changes in the circuit it runs when the user flips a switch, presses a
 *          button, sets a logic input or turns a potentiometer.
 */

#include "circuit.h"
#include "components/logic_input.h"
#include "components/potentiometer.h"
#include "components/resistor.h"
#include "components/switch.h"
#include "live_change.h"
#include "spice_value.h"
#include "test_printers.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

namespace {

// Whether the netlist writes the element of a change with the value the change gives it
bool NetlistAgrees(const Core::Circuit &circuit, const Core::LiveChange &change) {
    const std::string netlist = circuit.ToSpiceNetlist();
    const std::string value = Core::FormatSpiceValue(change.Value);
    std::size_t line_start = 0;
    while (line_start < netlist.size()) {
        const std::size_t line_end = netlist.find('\n', line_start);
        const std::string line = netlist.substr(line_start, line_end - line_start);
        if (line.starts_with(change.Element + " ")) {
            return line.ends_with(" " + value);
        }
        line_start = line_end == std::string::npos ? netlist.size() : line_end + 1;
    }
    return false;
}

} // namespace

TEST_CASE("Switches, push buttons, logic inputs and potentiometers change live, and other parts do not",
          "[live_change]") {
    CHECK(Core::IsLiveChangeable(Core::ComponentType::SWITCH));
    CHECK(Core::IsLiveChangeable(Core::ComponentType::PUSH_BUTTON));
    CHECK(Core::IsLiveChangeable(Core::ComponentType::LOGIC_INPUT));
    CHECK(Core::IsLiveChangeable(Core::ComponentType::POTENTIOMETER));
    CHECK_FALSE(Core::IsLiveChangeable(Core::ComponentType::RESISTOR));
    CHECK_FALSE(Core::IsLiveChangeable(Core::ComponentType::V_SWITCH));
    CHECK_FALSE(Core::IsLiveChangeable(Core::ComponentType::CLOCK));

    const Core::Resistor resistor;
    CHECK(Core::DescribeLiveChange(resistor).empty());
}

TEST_CASE("A switch is described by the resistance its netlist line has, open and closed", "[live_change]") {
    for (const Core::ComponentType type : {Core::ComponentType::SWITCH, Core::ComponentType::PUSH_BUTTON}) {
        Core::Switch contact(type);
        contact.SetName("SW1");
        Core::Circuit circuit;
        circuit.Add(contact, {1, 2});
        for (const bool closed : {false, true}) {
            contact.SetClosed(closed);
            const std::vector<Core::LiveChange> changes = Core::DescribeLiveChange(contact);
            REQUIRE(changes.size() == 1);
            CHECK(changes.front().Element == "R-SW1");
            CHECK(changes.front().Quantity == Core::LiveQuantity::RESISTANCE);
            CHECK(changes.front().Value == contact.GetResistance());
            CHECK(NetlistAgrees(circuit, changes.front()));
        }
    }
}

TEST_CASE("A logic input is described by the voltage it drives, high and low", "[live_change]") {
    Core::LogicInput input;
    input.SetName("V1");
    input.SetParameters({.LogicLevel = 3.3});
    Core::Circuit circuit;
    circuit.Add(input, {1});
    for (const bool high : {false, true}) {
        input.SetHigh(high);
        const std::vector<Core::LiveChange> changes = Core::DescribeLiveChange(input);
        REQUIRE(changes.size() == 1);
        CHECK(changes.front().Element == "V1");
        CHECK(changes.front().Quantity == Core::LiveQuantity::DC_VOLTAGE);
        CHECK(changes.front().Value == (high ? 3.3 : 0.0));
        CHECK(NetlistAgrees(circuit, changes.front()));
    }
}

TEST_CASE("A potentiometer is described by both halves of its track, which never reach 0 Ohm", "[live_change]") {
    Core::Potentiometer pot;
    pot.SetName("RV1");
    pot.SetValue(10e3);
    Core::Circuit circuit;
    circuit.Add(pot, {1, 2, 3});
    for (const double wiper : {0.0, 0.3, 1.0}) {
        pot.SetWiper(wiper);
        const std::vector<Core::LiveChange> changes = Core::DescribeLiveChange(pot);
        REQUIRE(changes.size() == 2);
        CHECK(changes[0].Element == "R-RV1a");
        CHECK(changes[1].Element == "R-RV1b");
        CHECK(changes[0].Value == pot.GetFirstTrackResistance());
        CHECK(changes[1].Value == pot.GetSecondTrackResistance());
        CHECK(changes[0].Value > 0.0);
        CHECK(changes[1].Value > 0.0);
        CHECK(NetlistAgrees(circuit, changes[0]));
        CHECK(NetlistAgrees(circuit, changes[1]));
    }
    // 30% of 10k up to the wiper, the rest past it
    pot.SetWiper(0.3);
    CHECK(Core::DescribeLiveChange(pot)[0].Value == Catch::Approx(3e3));
    CHECK(Core::DescribeLiveChange(pot)[1].Value == Catch::Approx(7e3));
}
