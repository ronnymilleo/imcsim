/**
 * @file    check_ac_sweep_tests.cpp
 * @brief   Tests for the checks of the settings of an AC sweep.
 */

#include "check_fixtures.h"
#include "circuit/components/voltage_source.h"
#include "simulation/check_ac_sweep.h"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("An AC sweep needs a frequency range and an AC source", "[check_ac_sweep]") {
    SourceAndResistor setup;
    const Core::ACSweepSettings range{.StartFrequency = 10.0, .StopFrequency = 1e5, .PointsPerDecade = 10};
    // The source is DC, so there is nothing to drive the sweep
    CHECK(Core::CheckACSweepSettings(setup.Circuit, range).value_or("").contains("AC source"));
    setup.Source.SetSourceType(Core::VoltageSource::SourceType::AC);
    CHECK_FALSE(Core::CheckACSweepSettings(setup.Circuit, range).has_value());
    CHECK(Core::CheckACSweepSettings(setup.Circuit, {.StartFrequency = 0.0}).has_value());
    CHECK(Core::CheckACSweepSettings(setup.Circuit, {.StartFrequency = 100.0, .StopFrequency = 10.0}).has_value());
}
