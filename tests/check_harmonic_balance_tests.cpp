/**
 * @file    check_harmonic_balance_tests.cpp
 * @brief   Tests for the checks of the settings and of the frequency grid of a harmonic balance.
 */

#include "check_fixtures.h"
#include "circuit/components/clock.h"
#include "circuit/components/logic_input.h"
#include "circuit/components/logic_part.h"
#include "circuit/components/subcircuit.h"
#include "circuit/components/voltage_source.h"
#include "circuit/subcircuit.h"
#include "simulation/check_harmonic_balance.h"
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <memory>

TEST_CASE("Harmonic balance validates frequency, harmonics, samples and periodic sources",
          "[check_harmonic_balance][hb]") {
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

TEST_CASE("A frequency is a harmonic of the fundamental only near an integer multiple of it",
          "[check_harmonic_balance][hb]") {
    CHECK(Core::FindHarmonicNumber(3e3, 1e3) == 3);
    // Within the relative tolerance, a rounding error in a period still counts
    CHECK(Core::FindHarmonicNumber(3e3 * (1.0 + 1e-9), 1e3) == 3);
    CHECK_FALSE(Core::FindHarmonicNumber(3.1e3, 1e3).has_value());
    // Below the fundamental there is no harmonic
    CHECK_FALSE(Core::FindHarmonicNumber(400.0, 1e3).has_value());
}
