/**
 * @file    led_glow_tests.cpp
 * @brief   Tests for the brightness and color of lit LEDs.
 */

#include "element_factory.h"
#include "led_glow.h"
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

TEST_CASE("LED brightness grows on a logarithmic scale up to the rated current", "[led_glow]") {
    CHECK(GUI::LEDBrightness(-5e-3) == 0.0f);
    CHECK(GUI::LEDBrightness(0.0) == 0.0f);
    CHECK(GUI::LEDBrightness(50e-6) == 0.0f);
    CHECK_THAT(GUI::LEDBrightness(1e-3), Catch::Matchers::WithinAbs(0.5, 0.01));
    CHECK(GUI::LEDBrightness(20e-3) == 1.0f);
    CHECK(GUI::LEDBrightness(100e-3) == 1.0f);
    CHECK(GUI::LEDBrightness(2e-3) > GUI::LEDBrightness(1e-3));
}

TEST_CASE("Only LEDs with forward current glow, in the color of their model", "[led_glow]") {
    std::vector<std::unique_ptr<GUI::UIElement>> elements;
    elements.push_back(Part(Core::ComponentType::LED, {0, 0}, "D1"));
    elements.push_back(Part(Core::ComponentType::LED, {10, 0}, "D2"));
    elements.push_back(Part(Core::ComponentType::LED, {20, 0}, "D3"));
    elements.push_back(Part(Core::ComponentType::LED, {30, 0}, "D4"));
    elements.push_back(Part(Core::ComponentType::DIODE, {40, 0}, "D5"));
    auto &green = static_cast<Core::Diode &>(elements[1]->GetComponent());
    green.SetModel(*Core::FindDiodeModel(Core::ComponentType::LED, "Green"));
    static_cast<Core::Diode &>(elements[2]->GetComponent()).SetCustom();
    // D4 is reverse biased and D5 is no LED
    const std::vector<Core::ComponentCurrent> currents = {
        {"D1", 20e-3}, {"D2", 1e-3}, {"D3", 5e-3}, {"D4", -1e-3}, {"D5", 20e-3}};

    const std::vector<GUI::LEDGlow> glows = GUI::ComputeLEDGlows(elements, currents);
    REQUIRE(glows.size() == 3);
    CHECK(glows[0].Center == GUI::GridPoint{0, 0});
    CHECK(glows[0].Color == Core::LEDColor::RED);
    CHECK(glows[0].Brightness == 1.0f);
    CHECK(glows[1].Center == GUI::GridPoint{10, 0});
    CHECK(glows[1].Color == Core::LEDColor::GREEN);
    CHECK(glows[1].Brightness < 1.0f);
    CHECK(glows[2].Color == Core::LEDColor::NONE);
}

TEST_CASE("LEDs missing from the results stay dark", "[led_glow]") {
    std::vector<std::unique_ptr<GUI::UIElement>> elements;
    elements.push_back(Part(Core::ComponentType::LED, {0, 0}, "D1"));
    CHECK(GUI::ComputeLEDGlows(elements, {}).empty());
}
