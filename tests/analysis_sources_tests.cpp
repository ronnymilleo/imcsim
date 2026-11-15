/**
 * @file    analysis_sources_tests.cpp
 * @brief   Tests for the lists of parts an analysis can take as its source or input.
 */

#include "check_fixtures.h"
#include "circuit/components/clock.h"
#include "circuit/components/logic_input.h"
#include "circuit/components/voltage_source.h"
#include "simulation/analysis_sources.h"
#include "simulation/check_dc_sweep.h"
#include "simulation/check_transfer_function.h"
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

TEST_CASE("The lists of sources hold the parts an analysis can use", "[analysis_sources]") {
    SourceAndResistor setup;
    CHECK(Core::GetSweepableSources(setup.Circuit) == std::vector<std::string>{"Vin1"});
    CHECK(Core::GetTransferFunctionInputSources(setup.Circuit) == std::vector<std::string>{"Vin1"});
    // A noise input must be set to AC, and a truth table varies logic inputs only
    CHECK(Core::GetNoiseInputSources(setup.Circuit).empty());
    CHECK(Core::GetTruthTableInputs(setup.Circuit).empty());
    setup.Source.SetSourceType(Core::VoltageSource::SourceType::AC);
    CHECK(Core::GetNoiseInputSources(setup.Circuit) == std::vector<std::string>{"Vin1"});

    CHECK(Core::FindSweepableSource(setup.Circuit, "Vin1") == &setup.Source);
    CHECK(Core::FindSweepableSource(setup.Circuit, "R1") == nullptr);
    CHECK(Core::FindLogicInput(setup.Circuit, "Vin1") == nullptr);
    CHECK(std::string(Core::SourceUnit(setup.Source)) == "V");
}

TEST_CASE("A clock or a logic input can be swept or be the input of a transfer function", "[analysis_sources]") {
    // Both are voltage sources in the netlist, whose DC value the analysis replaces
    SourceAndResistor setup;
    Core::Clock clock;
    clock.SetName("Vclk1");
    Core::LogicInput input;
    input.SetName("Vlogic1");
    setup.Circuit.Add(clock, {2});
    setup.Circuit.Add(input, {3});
    const std::vector<std::string> sources = {"Vin1", "Vclk1", "Vlogic1"};
    CHECK(Core::GetSweepableSources(setup.Circuit) == sources);
    CHECK(Core::GetTransferFunctionInputSources(setup.Circuit) == sources);
    CHECK(Core::FindTransferFunctionInput(setup.Circuit, "Vclk1") == &clock);
    CHECK(std::string(Core::SourceUnit(input)) == "V");
    CHECK_FALSE(Core::CheckDCSweepSettings(setup.Circuit,
                                           {.Swept = {.Source = "Vlogic1", .Start = 0.0, .Stop = 5.0, .Step = 0.5}})
                    .has_value());
    // A part that is no source is refused with the list of what can be
    CHECK(Core::CheckTransferFunctionSettings(setup.Circuit, {.OutputNode = 1, .InputSource = "R1"})
              .value_or("")
              .contains(Core::SWEEPABLE_SOURCES));
}
