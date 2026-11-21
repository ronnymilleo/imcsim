/**
 * @file    color_legend_tests.cpp
 * @brief   Tests for what the legend of the heat colors reads.
 */

#include "overlays/color_legend.h"

#include "schematic/connectivity.h"
#include <catch2/catch_test_macros.hpp>
#include <optional>

TEST_CASE("Only the colorings by voltage, current, power and sensitivity have a legend", "[color_legend]") {
    CHECK_FALSE(GUI::HasLegend(GUI::WireColoring::PLAIN));
    CHECK_FALSE(GUI::HasLegend(GUI::WireColoring::NODES));
    CHECK(GUI::HasLegend(GUI::WireColoring::VOLTAGE));
    CHECK(GUI::HasLegend(GUI::WireColoring::CURRENT));
    CHECK(GUI::HasLegend(GUI::WireColoring::POWER));
    CHECK(GUI::HasLegend(GUI::WireColoring::SENSITIVITY));
}

TEST_CASE("The heat legend reads the voltage range, or the largest current or power and the decades below it",
          "[color_legend]") {
    const GUI::HeatScale scale = {.LowestVoltage = -1.0,
                                  .HighestVoltage = 5.0,
                                  .LargestCurrent = 0.01,
                                  .LargestPower = 2.0};
    CHECK(GUI::GetHeatLegend(GUI::WireColoring::VOLTAGE, scale) == GUI::LegendLabels{.Low = "-1V", .High = "5V"});
    // Four decades under 10 mA
    CHECK(GUI::GetHeatLegend(GUI::WireColoring::CURRENT, scale) == GUI::LegendLabels{.Low = "<1uA", .High = "10mA"});
    CHECK(GUI::GetHeatLegend(GUI::WireColoring::POWER, scale) == GUI::LegendLabels{.Low = "<200uW", .High = "2W"});
}

TEST_CASE("The sensitivity legend reads the change of the output for 1 % of a part, once there is a result",
          "[color_legend]") {
    const GUI::Connectivity connectivity({}, {});
    GUI::ResultStore results;
    CHECK_FALSE(GUI::GetSensitivityLegend(results, connectivity).has_value());

    Core::Sensitivity sensitivity;
    sensitivity.OutputNode = 2;
    sensitivity.Parts = {{.Name = "R1", .Normalized = -1.0}, {.Name = "R2", .Normalized = 0.1}};
    results.SetSensitivity(sensitivity);
    // The largest sensitivity is 1 V for 100 %, so 10 mV for 1 %, and the scale shows four decades under it
    CHECK(GUI::GetSensitivityLegend(results, connectivity)
          == GUI::LegendLabels{.Low = "<1uV", .High = "10mV", .Title = "Change of V(2) per 1 %"});
}
