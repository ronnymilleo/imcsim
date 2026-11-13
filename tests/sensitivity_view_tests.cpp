/**
 * @file    sensitivity_view_tests.cpp
 * @brief   Tests of what the editor and the Output window show of a sensitivity: heat levels and table rows.
 */

#include "results/sensitivity_view.h"

#include "test_printers.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>

namespace {

/// A resistor, a capacitor four decades under it, a source with no influence and a block, in the order of a result.
Core::Sensitivity MakeSensitivity() {
    Core::Sensitivity sensitivity;
    sensitivity.OutputNode = 2;
    sensitivity.Parts = {
        {.Name = "R1", .Element = {}, .Unit = "Ohm", .Value = 4.7e3, .Absolute = -2e-4, .Normalized = -1.0},
        {.Name = "X2", .Element = "R1", .Unit = "Ohm", .Value = std::nullopt, .Absolute = 1e-4, .Normalized = 0.1},
        {.Name = "C1", .Element = {}, .Unit = "F", .Value = 1e-6, .Absolute = 1e2, .Normalized = 1e-4},
        {.Name = "V2", .Element = {}, .Unit = "V", .Value = 0.0, .Absolute = 0.5, .Normalized = 0.0},
        {.Name = "V1",
         .Type = Core::ComponentType::VCC,
         .Element = {},
         .Unit = "V",
         .Value = 12.0,
         .Absolute = 0.5,
         .Normalized = 0.5},
    };
    return sensitivity;
}

} // namespace

TEST_CASE("The heat level follows the size of the sensitivity over four decades", "[sensitivity_view]") {
    const Core::Sensitivity sensitivity = MakeSensitivity();
    CHECK(GUI::GetLargestSensitivity(sensitivity) == 1.0);
    // The sign does not matter, only the size: the largest is the hottest
    CHECK(GUI::FindSensitivityLevel(sensitivity, "R1") == 1.0f);
    // One decade under the largest sits at three quarters of the scale
    REQUIRE(GUI::FindSensitivityLevel(sensitivity, "X2").has_value());
    CHECK_THAT(*GUI::FindSensitivityLevel(sensitivity, "X2"), Catch::Matchers::WithinAbs(0.75, 1e-6));
    // Four decades under is the bottom of the scale, and so is no influence at all
    CHECK(GUI::FindSensitivityLevel(sensitivity, "C1") == 0.0f);
    CHECK(GUI::FindSensitivityLevel(sensitivity, "V2") == 0.0f);
    // A part the result does not list has no level, so it keeps the color of the theme
    CHECK_FALSE(GUI::FindSensitivityLevel(sensitivity, "D1").has_value());
}

TEST_CASE("A result with no influence at all colors every part at the bottom of the scale", "[sensitivity_view]") {
    Core::Sensitivity sensitivity;
    sensitivity.Parts = {
        {.Name = "R1", .Element = {}, .Unit = "Ohm", .Value = 1e3, .Absolute = 0.0, .Normalized = 0.0}};
    CHECK(GUI::GetLargestSensitivity(sensitivity) == 0.0);
    CHECK(GUI::FindSensitivityLevel(sensitivity, "R1") == 0.0f);
    CHECK(GUI::MakeSensitivityRows(sensitivity).front().Share == 0.0);
}

TEST_CASE("The table gives each part its value, its two sensitivities and its share of the largest",
          "[sensitivity_view]") {
    const std::vector<GUI::SensitivityRow> rows = GUI::MakeSensitivityRows(MakeSensitivity());
    REQUIRE(rows.size() == 5);
    CHECK(rows[0].Part == "R1");
    CHECK(rows[0].PartName == "R1");
    CHECK(rows[0].Value == "4.7kOhm");
    CHECK(rows[0].Absolute == "-200uV/Ohm");
    // Per 1 %, signed: -1 V for 100 % is -10 mV
    CHECK(rows[0].Normalized == "-10mV");
    CHECK(rows[0].Share == 1.0);
    // A block names its element, selects the block and has no value of its own to show
    CHECK(rows[1].Part == "X2.R1");
    CHECK(rows[1].PartName == "X2");
    CHECK(rows[1].Value.empty());
    CHECK_THAT(rows[1].Share, Catch::Matchers::WithinAbs(0.1, 1e-12));
    CHECK(rows[2].Absolute == "100V/F");
    // A supply rail has no name on the schematic, only "VCC 12V", and the table names it the same, whatever the
    // netlist calls it; a click still selects it by its name
    CHECK(rows[4].Part == "VCC 12V");
    CHECK(rows[4].PartName == "V1");
    CHECK(rows[4].Value == "12V");
}
