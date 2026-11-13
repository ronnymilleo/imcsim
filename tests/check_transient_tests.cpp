/**
 * @file    check_transient_tests.cpp
 * @brief   Tests for the checks of the settings of a transient, with the steps its noise and random sources add.
 */

#include "check_fixtures.h"
#include "circuit/components/subcircuit.h"
#include "circuit/components/voltage_source.h"
#include "circuit/subcircuit.h"
#include "simulation/check_transient.h"
#include "simulation/circuit_checks.h"
#include <catch2/catch_test_macros.hpp>
#include <memory>

TEST_CASE("Transient settings need a positive span and step, and not too many points", "[check_transient]") {
    CHECK_FALSE(Core::CheckTransientSettings({.StopTime = 1e-3, .TimeStep = 1e-6}).has_value());
    CHECK(Core::CheckTransientSettings({.StopTime = 0.0, .TimeStep = 1e-6}).has_value());
    CHECK(Core::CheckTransientSettings({.StopTime = 1e-3, .TimeStep = 0.0}).has_value());
    CHECK(Core::CheckTransientSettings({.StopTime = 1e-3, .TimeStep = 2e-3}).value_or("").contains("longer"));
    CHECK(Core::CheckTransientSettings({.StopTime = 1.0, .TimeStep = 1e-9}).value_or("").contains("too small"));
}

TEST_CASE("A noise with a negative step is refused before any backend sees it", "[check_transient]") {
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

TEST_CASE("Random values with a step that is not positive are refused", "[check_transient]") {
    SourceAndResistor setup;
    setup.Source.SetSourceType(Core::VoltageSource::SourceType::RANDOM);
    setup.Source.SetRandom({.TimeStep = 0.0});
    CHECK(Core::CheckCircuit(setup.Circuit).value_or("").contains("random"));
    setup.Source.SetRandom({.TimeStep = 1e-6});
    CHECK_FALSE(Core::CheckCircuit(setup.Circuit).has_value());
}

TEST_CASE("The finest step of the noise and random sources counts in the points of a transient", "[check_transient]") {
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
