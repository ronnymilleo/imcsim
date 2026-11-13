/**
 * @file    check_dc_sweep_tests.cpp
 * @brief   Tests for the checks of the settings of a DC sweep.
 */

#include "check_fixtures.h"
#include "simulation/check_dc_sweep.h"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("A DC sweep needs real sources, a step that heads for the stop value and a bounded number of points",
          "[check_dc_sweep]") {
    const SourceAndResistor setup;
    const Core::SweepRange swept{.Source = "Vin1", .Start = 0.0, .Stop = 5.0, .Step = 0.5};
    CHECK_FALSE(Core::CheckDCSweepSettings(setup.Circuit, {.Swept = swept}).has_value());
    CHECK(Core::CheckDCSweepSettings(setup.Circuit, {.Swept = {.Source = "V9", .Stop = 5.0}}).has_value());
    CHECK(
        Core::CheckDCSweepSettings(setup.Circuit, {.Swept = {.Source = "Vin1", .Start = 0.0, .Stop = 5.0, .Step = -1}})
            .value_or("")
            .contains("toward"));
    CHECK(Core::CheckDCSweepSettings(setup.Circuit, {.Swept = {.Source = "Vin1", .Start = 0.0, .Stop = 1e6, .Step = 1}})
              .value_or("")
              .contains("too small"));
    // The stepped source must differ from the swept one
    CHECK(
        Core::CheckDCSweepSettings(setup.Circuit, {.Swept = swept, .Stepped = swept}).value_or("").contains("differ"));
}
