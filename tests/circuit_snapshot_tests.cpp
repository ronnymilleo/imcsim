/**
 * @file    circuit_snapshot_tests.cpp
 * @brief   Tests for reading a transient at an instant, the heat scales and the power of each part.
 */

#include "circuit_snapshot.h"
#include "current_flow.h"
#include "element_factory.h"
#include "test_printers.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <memory>
#include <string>
#include <vector>

namespace {

using Catch::Matchers::WithinAbs;

std::unique_ptr<GUI::UIElement> Part(const Core::ComponentType type, const GUI::GridPoint position,
                                     const std::string &name) {
    auto element = GUI::CreateElement(type, position, GUI::Rotation::R0);
    element->GetComponent().SetName(name);
    return element;
}

// A source V1 at (0, 0) wired to R1 from (4, 0) to (8, 0), and R1 wired down to ground at (8, 4): node 1 is the
// source and R1 top, ground is node 0
struct SeriesLoop {
    std::vector<std::unique_ptr<GUI::UIElement>> Elements;
    std::vector<GUI::UIWire> Wires = {{{0, 0}, {4, 0}}, {{8, 0}, {8, 4}}};

    SeriesLoop() {
        Elements.push_back(Part(Core::ComponentType::VCC, {0, 0}, "V1"));
        Elements.push_back(Part(Core::ComponentType::RESISTOR, {6, 0}, "R1"));
        Elements.push_back(Part(Core::ComponentType::GROUND, {8, 4}, ""));
    }
};

// Two samples, 0 s and 2 s: the source goes from 0 V to 4 V and delivers 0 to 2 mA
Core::Transient RampingLoop() {
    Core::Transient transient;
    transient.Times = {0.0, 2.0};
    transient.NodeVoltages = {{0.0, 0.0}, {0.0, 4.0}};
    transient.Currents = {{"V1", {0.0, -2e-3}}, {"R1", {0.0, 2e-3}}};
    return transient;
}

} // namespace

TEST_CASE("A transient is read at an instant by interpolating its samples", "[circuit_snapshot]") {
    const Core::Transient transient = RampingLoop();

    const Core::OperatingPoint middle = GUI::SampleTransient(transient, 1.0);
    REQUIRE(middle.NodeVoltages.size() == 2);
    CHECK_THAT(middle.NodeVoltages[1], WithinAbs(2.0, 1e-12));
    REQUIRE(middle.Currents.size() == 2);
    CHECK(middle.Currents[0].Name == "V1");
    CHECK_THAT(middle.Currents[0].Current, WithinAbs(-1e-3, 1e-12));
    CHECK_THAT(GUI::SampleTransient(transient, 0.5).NodeVoltages[1], WithinAbs(1.0, 1e-12));
}

TEST_CASE("An instant outside the transient takes its first or last sample", "[circuit_snapshot]") {
    const Core::Transient transient = RampingLoop();
    CHECK(GUI::SampleTransient(transient, -1.0).NodeVoltages[1] == 0.0);
    CHECK(GUI::SampleTransient(transient, 0.0).NodeVoltages[1] == 0.0);
    CHECK(GUI::SampleTransient(transient, 2.0).NodeVoltages[1] == 4.0);
    CHECK(GUI::SampleTransient(transient, 9.0).NodeVoltages[1] == 4.0);
}

TEST_CASE("A transient with one sample or none can be read", "[circuit_snapshot]") {
    Core::Transient transient;
    CHECK(GUI::SampleTransient(transient, 1.0).NodeVoltages.empty());
    transient.Times = {0.0};
    transient.NodeVoltages = {{0.0}, {3.0}};
    transient.Currents = {{"R1", {1e-3}}};
    const Core::OperatingPoint point = GUI::SampleTransient(transient, 5.0);
    CHECK(point.NodeVoltages[1] == 3.0);
    CHECK(point.Currents[0].Current == 1e-3);
}

TEST_CASE("Parts absorb the voltage of their terminals times the current into them", "[circuit_snapshot]") {
    const SeriesLoop loop;
    const GUI::Connectivity connectivity(loop.Elements, loop.Wires);
    const Core::OperatingPoint point = GUI::SampleTransient(RampingLoop(), 2.0);

    const std::vector<GUI::ComponentPower> powers = GUI::ComputePowers(loop.Elements, connectivity, point);
    // Ground has no current, so only the source and the resistor are listed
    REQUIRE(powers.size() == 2);
    CHECK(powers[0].Name == "V1");
    CHECK_THAT(powers[0].Power, WithinAbs(-8e-3, 1e-12));
    CHECK(powers[1].Name == "R1");
    CHECK_THAT(powers[1].Power, WithinAbs(8e-3, 1e-12));
}

TEST_CASE("The scale of a transient covers every sample", "[circuit_snapshot]") {
    const SeriesLoop loop;
    const GUI::Connectivity connectivity(loop.Elements, loop.Wires);

    const GUI::HeatScale scale = GUI::ScaleOf(RampingLoop(), loop.Elements, connectivity);
    CHECK(scale.LowestVoltage == 0.0);
    CHECK(scale.HighestVoltage == 4.0);
    CHECK_THAT(scale.LargestCurrent, WithinAbs(2e-3, 1e-12));
    CHECK_THAT(scale.LargestPower, WithinAbs(8e-3, 1e-12));
    // An instant alone only knows its own sample
    const GUI::HeatScale instant = GUI::ScaleOf(GUI::SampleTransient(RampingLoop(), 1.0), loop.Elements, connectivity);
    CHECK(instant.HighestVoltage == 2.0);
}

TEST_CASE("Levels place voltage linearly and power logarithmically", "[circuit_snapshot]") {
    const GUI::HeatScale scale = {
        .LowestVoltage = -2.0, .HighestVoltage = 2.0, .LargestCurrent = 1e-3, .LargestPower = 1.0};
    CHECK(GUI::VoltageLevel(-2.0, scale) == 0.0f);
    CHECK(GUI::VoltageLevel(0.0, scale) == 0.5f);
    CHECK(GUI::VoltageLevel(2.0, scale) == 1.0f);
    CHECK(GUI::VoltageLevel(1.0, GUI::HeatScale{}) == 0.5f);
    CHECK(GUI::PowerLevel(1.0, scale) == 1.0f);
    CHECK(GUI::PowerLevel(-1.0, scale) == 1.0f);
    CHECK_THAT(GUI::PowerLevel(1e-2, scale), WithinAbs(0.5, 1e-6));
    CHECK(GUI::PowerLevel(1e-6, scale) < 0.0f);
    CHECK(GUI::PowerLevel(0.0, scale) == 0.0f);
}

TEST_CASE("The scale of a long transient keeps its exact voltage and current extremes and samples its power",
          "[circuit_snapshot]") {
    const SeriesLoop loop;
    const GUI::Connectivity connectivity(loop.Elements, loop.Wires);

    // A ramp of many more samples than the scale reads the power of: the source goes from 0 V to 4 V and the current
    // from 0 to 2 mA, so the largest power is at the last sample, which is always read
    const std::size_t count = GUI::MAX_POWER_SAMPLES * 5 + 3;
    Core::Transient transient;
    transient.NodeVoltages = {std::vector<double>(count, 0.0), {}};
    transient.Currents = {{"V1", {}}, {"R1", {}}};
    for (std::size_t index = 0; index < count; ++index) {
        const double fraction = static_cast<double>(index) / static_cast<double>(count - 1);
        transient.Times.push_back(fraction);
        transient.NodeVoltages[1].push_back(4.0 * fraction);
        transient.Currents[0].Values.push_back(-2e-3 * fraction);
        transient.Currents[1].Values.push_back(2e-3 * fraction);
    }
    // A spike at a sample the power is not read at still sets the voltage and current extremes
    transient.NodeVoltages[1][1] = 9.0;
    transient.Currents[1].Values[1] = 5e-3;

    const GUI::HeatScale scale = GUI::ScaleOf(transient, loop.Elements, connectivity);
    CHECK(scale.LowestVoltage == 0.0);
    CHECK(scale.HighestVoltage == 9.0);
    CHECK_THAT(scale.LargestCurrent, WithinAbs(5e-3, 1e-12));
    CHECK_THAT(scale.LargestPower, WithinAbs(8e-3, 1e-12));
}
