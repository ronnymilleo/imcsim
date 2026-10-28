/**
 * @file    circuit_checks_tests.cpp
 * @brief   Tests for the checks of a circuit and of the settings of an analysis, which no backend takes part in.
 */

#include "circuit_checks.h"

#include "components/clock.h"
#include "components/ground.h"
#include "components/logic_input.h"
#include "components/logic_part.h"
#include "components/resistor.h"
#include "components/voltage_source.h"
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

namespace {

// A 5 V source across a 1k resistor to ground: node 1 is the source, ground is node 0
struct SourceAndResistor {
    Core::VoltageSource Source;
    Core::Resistor Load;
    Core::Ground Reference;
    Core::Circuit Circuit;

    SourceAndResistor() {
        Source.SetName("Vin1");
        Load.SetName("R1");
        Load.SetValue(1e3);
        Circuit.Add(Source, {1, 0});
        Circuit.Add(Load, {1, 0});
        Circuit.Add(Reference, {0});
    }
};

} // namespace

TEST_CASE("A circuit needs parts, and a ground unless it is only digital", "[circuit_checks]") {
    CHECK(Core::CheckCircuit(Core::Circuit()).value_or("").contains("empty"));

    const SourceAndResistor valid;
    CHECK_FALSE(Core::CheckCircuit(valid.Circuit).has_value());

    Core::Circuit without_ground;
    without_ground.Add(valid.Load, {1, 2});
    CHECK(Core::CheckCircuit(without_ground).value_or("").contains("no ground"));

    // Logic inputs and logic parts refer to node 0 themselves, so a circuit of only those needs no Ground part
    Core::LogicInput input;
    input.SetName("V1");
    Core::LogicPart gate(Core::ComponentType::AND);
    gate.SetName("U1");
    Core::Circuit digital;
    digital.Add(input, {1});
    digital.Add(gate, {1, 2, 3});
    CHECK_FALSE(Core::CheckCircuit(digital).has_value());
}

TEST_CASE("Transient settings need a positive span and step, and not too many points", "[circuit_checks]") {
    CHECK_FALSE(Core::CheckTransientSettings({.StopTime = 1e-3, .TimeStep = 1e-6}).has_value());
    CHECK(Core::CheckTransientSettings({.StopTime = 0.0, .TimeStep = 1e-6}).has_value());
    CHECK(Core::CheckTransientSettings({.StopTime = 1e-3, .TimeStep = 0.0}).has_value());
    CHECK(Core::CheckTransientSettings({.StopTime = 1e-3, .TimeStep = 2e-3}).value_or("").contains("longer"));
    CHECK(Core::CheckTransientSettings({.StopTime = 1.0, .TimeStep = 1e-9}).value_or("").contains("too small"));
}

TEST_CASE("An AC sweep needs a frequency range and an AC source", "[circuit_checks]") {
    SourceAndResistor setup;
    const Core::ACSweepSettings range{.StartFrequency = 10.0, .StopFrequency = 1e5, .PointsPerDecade = 10};
    // The source is DC, so there is nothing to drive the sweep
    CHECK(Core::CheckACSweepSettings(setup.Circuit, range).value_or("").contains("AC source"));
    setup.Source.SetSourceType(Core::VoltageSource::SourceType::AC);
    CHECK_FALSE(Core::CheckACSweepSettings(setup.Circuit, range).has_value());
    CHECK(Core::CheckACSweepSettings(setup.Circuit, {.StartFrequency = 0.0}).has_value());
    CHECK(Core::CheckACSweepSettings(setup.Circuit, {.StartFrequency = 100.0, .StopFrequency = 10.0}).has_value());
}

TEST_CASE("A DC sweep needs real sources, a step that heads for the stop value and a bounded number of points",
          "[circuit_checks]") {
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

TEST_CASE("The lists of sources hold the parts an analysis can use", "[circuit_checks]") {
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

TEST_CASE("A clock or a logic input can be swept or be the input of a transfer function", "[circuit_checks]") {
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
