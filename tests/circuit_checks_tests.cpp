/**
 * @file    circuit_checks_tests.cpp
 * @brief   Tests for the checks of a circuit and of the settings of an analysis, which no backend takes part in.
 */

#include "simulation/circuit_checks.h"

#include "components/clock.h"
#include "components/ground.h"
#include "components/logic_input.h"
#include "components/logic_part.h"
#include "components/resistor.h"
#include "components/subcircuit.h"
#include "components/voltage_source.h"
#include "model/subcircuit.h"
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {

/// A 5 V source across a 1k resistor to ground: node 1 is the source, ground is node 0.
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

    // A block holding only logic parts counts as digital too, but not one with a resistor inside
    auto logic_block = std::make_shared<Core::SubcircuitDefinition>();
    logic_block->Name = "logic";
    logic_block->Pins = {{"A", 1}, {"Y", 3}};
    logic_block->Body.Add(gate, {1, 2, 3});
    Core::SubcircuitInstance instance("logic", {"A", "Y"});
    instance.SetName("X1");
    Core::Circuit with_block;
    with_block.Add(input, {1});
    with_block.Add(instance, {1, 2});
    with_block.AddSubcircuit(logic_block);
    CHECK_FALSE(Core::CheckCircuit(with_block).has_value());
    logic_block->Body.Add(valid.Load, {3, 0});
    CHECK(Core::CheckCircuit(with_block).value_or("").contains("no ground"));
}

TEST_CASE("Transient settings need a positive span and step, and not too many points", "[circuit_checks]") {
    CHECK_FALSE(Core::CheckTransientSettings({.StopTime = 1e-3, .TimeStep = 1e-6}).has_value());
    CHECK(Core::CheckTransientSettings({.StopTime = 0.0, .TimeStep = 1e-6}).has_value());
    CHECK(Core::CheckTransientSettings({.StopTime = 1e-3, .TimeStep = 0.0}).has_value());
    CHECK(Core::CheckTransientSettings({.StopTime = 1e-3, .TimeStep = 2e-3}).value_or("").contains("longer"));
    CHECK(Core::CheckTransientSettings({.StopTime = 1.0, .TimeStep = 1e-9}).value_or("").contains("too small"));
}

TEST_CASE("A noise with a negative step is refused before any backend sees it", "[circuit_checks]") {
    SourceAndResistor setup;
    setup.Source.SetSourceType(Core::VoltageSource::SourceType::NOISE);
    setup.Source.SetNoise({.WhiteAmplitude = 1e-3, .TimeStep = 10e-6});
    CHECK_FALSE(Core::CheckCircuit(setup.Circuit).has_value());

    // ngspice never returns from a negative noise step, and it runs inside this process
    Core::NoiseParameters negative = setup.Source.GetNoise();
    negative.TimeStep = -10e-6;
    setup.Source.SetNoise(negative);
    CHECK(Core::CheckCircuit(setup.Circuit).value_or("").contains("Vin1"));

    // The same holds for a source inside a block
    Core::VoltageSource inside = setup.Source;
    auto block = std::make_shared<Core::SubcircuitDefinition>();
    block->Name = "noisy";
    block->Pins = {{"A", 1}};
    block->Body.Add(inside, {1, 0});
    Core::SubcircuitInstance instance("noisy", {"A"});
    instance.SetName("X1");
    Core::Circuit with_block;
    with_block.Add(setup.Load, {1, 0});
    with_block.Add(setup.Reference, {0});
    with_block.Add(instance, {1});
    with_block.AddSubcircuit(block);
    CHECK(Core::CheckCircuit(with_block).value_or("").contains("noise"));
}

TEST_CASE("Random values with a step that is not positive are refused", "[circuit_checks]") {
    SourceAndResistor setup;
    setup.Source.SetSourceType(Core::VoltageSource::SourceType::RANDOM);
    setup.Source.SetRandom({.TimeStep = 0.0});
    CHECK(Core::CheckCircuit(setup.Circuit).value_or("").contains("random"));
    setup.Source.SetRandom({.TimeStep = 1e-6});
    CHECK_FALSE(Core::CheckCircuit(setup.Circuit).has_value());
}

TEST_CASE("The finest step of the noise and random sources counts in the points of a transient", "[circuit_checks]") {
    SourceAndResistor setup;
    const Core::TransientSettings settings{.StopTime = 1.0, .TimeStep = 1e-3};
    CHECK_FALSE(Core::FindFinestSignalStep(setup.Circuit));
    CHECK_FALSE(Core::CheckSourceSteps(setup.Circuit, settings).has_value());

    // A million points over the stop time is the limit; the time step alone is far from it
    setup.Source.SetSourceType(Core::VoltageSource::SourceType::NOISE);
    setup.Source.SetNoise({.WhiteAmplitude = 1e-3, .TimeStep = 1e-6});
    CHECK(Core::FindFinestSignalStep(setup.Circuit) == 1e-6);
    CHECK_FALSE(Core::CheckSourceSteps(setup.Circuit, settings).has_value());
    setup.Source.SetNoise({.WhiteAmplitude = 1e-3, .TimeStep = 0.5e-6});
    CHECK(Core::CheckSourceSteps(setup.Circuit, settings).value_or("").contains("too small"));

    // A second source with a coarser step does not hide the finer one
    Core::VoltageSource coarse;
    coarse.SetName("Vin2");
    coarse.SetSourceType(Core::VoltageSource::SourceType::RANDOM);
    coarse.SetRandom({.TimeStep = 1e-3});
    setup.Circuit.Add(coarse, {2, 0});
    CHECK(Core::FindFinestSignalStep(setup.Circuit) == 0.5e-6);
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

TEST_CASE("Harmonic balance validates frequency, harmonics, samples and periodic sources", "[circuit_checks][hb]") {
    SourceAndResistor setup;
    setup.Source.SetSourceType(Core::Source::SourceType::AC);
    setup.Source.SetAC({.Amplitude = 2.0, .Frequency = 1e3});

    const Core::HarmonicBalanceSettings valid{.FundamentalFrequency = 1e3, .Harmonics = 8, .Samples = 0};
    CHECK_FALSE(Core::CheckHarmonicBalanceSettings(setup.Circuit, valid).has_value());
    CHECK_FALSE(
        Core::CheckHarmonicBalanceSettings(setup.Circuit, {.FundamentalFrequency = 1e3, .Harmonics = 8, .Samples = 32})
            .has_value());

    // Invalid global numeric settings
    CHECK(Core::CheckHarmonicBalanceSettings(setup.Circuit, {.FundamentalFrequency = 0.0}).has_value());
    CHECK(Core::CheckHarmonicBalanceSettings(setup.Circuit,
                                             {.FundamentalFrequency = std::numeric_limits<double>::quiet_NaN()})
              .has_value());
    CHECK(Core::CheckHarmonicBalanceSettings(setup.Circuit, {.FundamentalFrequency = 1e3, .Harmonics = 0}).has_value());
    CHECK(Core::CheckHarmonicBalanceSettings(
              setup.Circuit, {.FundamentalFrequency = 1e3, .Harmonics = Core::MAX_HARMONIC_BALANCE_HARMONICS + 1})
              .has_value());
    CHECK(
        Core::CheckHarmonicBalanceSettings(setup.Circuit, {.FundamentalFrequency = 1e3, .Harmonics = 8, .Samples = 16})
            .has_value());
    CHECK(
        Core::CheckHarmonicBalanceSettings(setup.Circuit, {.FundamentalFrequency = 1e3, .Harmonics = 8, .Samples = 30})
            .has_value());
    CHECK(Core::CheckHarmonicBalanceSettings(setup.Circuit,
                                             {.FundamentalFrequency = 1e3, .Harmonics = 8, .Samples = 131072})
              .has_value());

    // Frequency must be an integer harmonic within 1..Harmonics
    setup.Source.SetAC({.Amplitude = 2.0, .Frequency = 1500.0});
    CHECK(Core::CheckHarmonicBalanceSettings(setup.Circuit, valid).value_or("").contains("integer multiple"));
    setup.Source.SetAC({.Amplitude = 2.0, .Frequency = 10e3});
    CHECK(Core::CheckHarmonicBalanceSettings(setup.Circuit, valid).value_or("").contains("exceeds"));

    // An AC source with zero time-varying amplitude acts as DC and is accepted at any nominal frequency
    setup.Source.SetAC({.Amplitude = 0.0, .Frequency = 1234.5});
    CHECK_FALSE(Core::CheckHarmonicBalanceSettings(setup.Circuit, valid).has_value());

    // Pulse and clock sources must also fall on harmonics of f0
    Core::Clock clock;
    clock.SetName("Vclk1");
    clock.SetParameters({.Frequency = 2e3});
    setup.Circuit.Add(clock, {2});
    CHECK_FALSE(Core::CheckHarmonicBalanceSettings(setup.Circuit, valid).has_value());

    clock.SetParameters({.Frequency = 2.5e3});
    CHECK(Core::CheckHarmonicBalanceSettings(setup.Circuit, valid).has_value());
    clock.SetParameters({.Frequency = 2e3});

    // Non-periodic waveforms are refused
    setup.Source.SetSourceType(Core::Source::SourceType::PWL);
    CHECK(Core::CheckHarmonicBalanceSettings(setup.Circuit, valid).value_or("").contains("harmonic balance"));

    // Sources inside subcircuit blocks are validated too
    setup.Source.SetSourceType(Core::Source::SourceType::DC);
    auto definition = std::make_shared<Core::SubcircuitDefinition>();
    definition->Name = "BlockOsc";
    definition->Pins = {{.Name = "OUT", .Node = 1}};
    Core::VoltageSource inside_source;
    inside_source.SetName("Vsub");
    inside_source.SetSourceType(Core::Source::SourceType::EXP);
    definition->Body.Add(inside_source, {1, 0});
    setup.Circuit.AddSubcircuit(definition);
    Core::SubcircuitInstance instance("BlockOsc");
    instance.SetName("X1");
    setup.Circuit.Add(instance, {3});
    CHECK(Core::CheckHarmonicBalanceSettings(setup.Circuit, valid).value_or("").contains("Vsub"));
}

TEST_CASE("A frequency is a harmonic of the fundamental only near an integer multiple of it", "[circuit_checks][hb]") {
    CHECK(Core::FindHarmonicNumber(3e3, 1e3) == 3);
    // Within the relative tolerance, a rounding error in a period still counts
    CHECK(Core::FindHarmonicNumber(3e3 * (1.0 + 1e-9), 1e3) == 3);
    CHECK_FALSE(Core::FindHarmonicNumber(3.1e3, 1e3).has_value());
    // Below the fundamental there is no harmonic
    CHECK_FALSE(Core::FindHarmonicNumber(400.0, 1e3).has_value());
}
