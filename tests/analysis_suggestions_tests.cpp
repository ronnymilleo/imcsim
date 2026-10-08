/**
 * @file    analysis_suggestions_tests.cpp
 * @brief   Tests for the transient and AC sweep settings suggested from a circuit.
 */

#include "analysis_suggestions.h"
#include "components/capacitor.h"
#include "components/current_source.h"
#include "components/op_amp.h"
#include "components/resistor.h"
#include "components/voltage_source.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("SuggestTransientSettings needs a sine or pulse source", "[suggestions]") {
    const Core::VoltageSource source;
    const Core::Resistor resistor;
    Core::Circuit circuit;
    circuit.Add(source, {1, 0});
    circuit.Add(resistor, {1, 0});
    CHECK_FALSE(Core::SuggestTransientSettings(circuit).has_value());
}

TEST_CASE("SuggestTransientSettings shows five periods of the slowest source", "[suggestions]") {
    Core::VoltageSource sine;
    sine.SetSourceType(Core::Source::SourceType::AC);
    sine.SetAC({.Amplitude = 1.0, .Frequency = 1e3});
    Core::Circuit circuit;
    circuit.Add(sine, {1, 0});

    SECTION("one sine") {
        const auto settings = Core::SuggestTransientSettings(circuit);
        REQUIRE(settings.has_value());
        CHECK(settings->StopTime == Catch::Approx(5e-3));
        CHECK(settings->TimeStep == Catch::Approx(5e-6));
    }

    SECTION("a slower pulse with a delay sets the stop time, the faster sine the step") {
        Core::CurrentSource pulse;
        pulse.SetSourceType(Core::Source::SourceType::Pulse);
        Core::PulseParameters parameters;
        parameters.Period = 16e-3;
        parameters.Width = 8e-3;
        parameters.Delay = 2e-3;
        pulse.SetPulse(parameters);
        circuit.Add(pulse, {1, 0});

        const auto settings = Core::SuggestTransientSettings(circuit);
        REQUIRE(settings.has_value());
        // 2 ms + 5 x 16 ms = 82 ms, rounded up to 100 ms
        CHECK(settings->StopTime == Catch::Approx(0.1));
        CHECK(settings->TimeStep == Catch::Approx(5e-6));
    }
}

TEST_CASE("SuggestTransientSettings rounds a 60 Hz period to readable times", "[suggestions]") {
    Core::VoltageSource sine;
    sine.SetSourceType(Core::Source::SourceType::AC);
    sine.SetAC({.Amplitude = 17.0, .Frequency = 60.0});
    Core::Circuit circuit;
    circuit.Add(sine, {1, 0});

    const auto settings = Core::SuggestTransientSettings(circuit);
    REQUIRE(settings.has_value());
    // 83.3 ms up to 100 ms, and 83.3 us down to 50 us
    CHECK(settings->StopTime == Catch::Approx(0.1));
    CHECK(settings->TimeStep == Catch::Approx(50e-6));
}

TEST_CASE("SuggestACSweepSettings spans two decades around the corners", "[suggestions]") {
    Core::Resistor resistor;
    resistor.SetValue(1e3);
    Core::Capacitor capacitor;
    capacitor.SetValue(100e-9);
    Core::Circuit circuit;

    SECTION("no reactive part and no op-amp") {
        circuit.Add(resistor, {1, 0});
        CHECK_FALSE(Core::SuggestACSweepSettings(circuit).has_value());
    }

    SECTION("an RC low-pass with its corner at 1.6 kHz") {
        circuit.Add(resistor, {1, 2});
        circuit.Add(capacitor, {2, 0});
        const auto settings = Core::SuggestACSweepSettings(circuit);
        REQUIRE(settings.has_value());
        CHECK(settings->StartFrequency == Catch::Approx(10.0));
        CHECK(settings->StopFrequency == Catch::Approx(1e6));
        CHECK(settings->PointsPerDecade == Core::ACSweepSettings{}.PointsPerDecade);
    }

    SECTION("an op-amp adds its gain-bandwidth product") {
        const Core::OpAmp op_amp;
        circuit.Add(resistor, {1, 2});
        circuit.Add(capacitor, {2, 0});
        circuit.Add(op_amp, {3, 2, 3, 4, 5});
        const auto settings = Core::SuggestACSweepSettings(circuit);
        REQUIRE(settings.has_value());
        CHECK(settings->StartFrequency == Catch::Approx(10.0));
        CHECK(settings->StopFrequency == Catch::Approx(1e8));
    }
}
