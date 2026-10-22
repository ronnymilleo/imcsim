/**
 * @file    heat_glow_tests.cpp
 * @brief   Tests for the brightness of resistors that dissipate power.
 */

#include "element_factory.h"
#include "heat_glow.h"
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

} // namespace

TEST_CASE("Heat brightness grows on a logarithmic scale up to the largest power", "[heat_glow]") {
    CHECK(GUI::HeatBrightness(0.0, 1.0) == 0.0f);
    CHECK(GUI::HeatBrightness(-1.0, 1.0) == 0.0f);
    CHECK(GUI::HeatBrightness(1.0, 1.0) == 1.0f);
    CHECK(GUI::HeatBrightness(5.0, 1.0) == 1.0f);
    CHECK_THAT(GUI::HeatBrightness(0.1, 1.0), Catch::Matchers::WithinAbs(0.5, 1e-6));
    CHECK(GUI::HeatBrightness(0.001, 1.0) == 0.0f);
    CHECK(GUI::HeatBrightness(1.0, 0.0) == 0.0f);
}

TEST_CASE("Only resistors that dissipate power glow", "[heat_glow]") {
    std::vector<std::unique_ptr<GUI::UIElement>> elements;
    elements.push_back(Part(Core::ComponentType::RESISTOR, {0, 0}, "R1"));
    elements.push_back(Part(Core::ComponentType::RESISTOR, {10, 0}, "R2"));
    elements.push_back(Part(Core::ComponentType::RESISTOR, {20, 0}, "R3"));
    elements.push_back(Part(Core::ComponentType::RESISTOR, {30, 0}, "R4"));
    elements.push_back(Part(Core::ComponentType::VCC, {40, 0}, "V1"));
    // R3 is nearly cold and R4 has no power in the results; the supply delivers power and is no resistor
    const std::vector<GUI::ComponentPower> powers = {{"R1", 1.0}, {"R2", 0.1}, {"R3", 1e-4}, {"V1", -1.0}};
    const GUI::HeatScale scale = {.LargestPower = 1.0};

    const std::vector<GUI::HeatGlow> glows = GUI::ComputeHeatGlows(elements, powers, scale);
    REQUIRE(glows.size() == 2);
    CHECK(glows[0].Center == GUI::GridPoint{0, 0});
    CHECK(glows[0].Brightness == 1.0f);
    CHECK(glows[1].Center == GUI::GridPoint{10, 0});
    CHECK(glows[1].Brightness < glows[0].Brightness);
}
