/**
 * @file    analysis_suggestions_tests.cpp
 * @brief   Tests for the transient and AC sweep settings suggested from a circuit.
 */

#include "analysis_suggestions.h"
#include "components/capacitor.h"
#include "components/clock.h"
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
        pulse.SetSourceType(Core::Source::SourceType::PULSE);
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

TEST_CASE("SuggestTransientSettings reads a clock as a periodic source", "[suggestions]") {
    Core::Clock clock;
    clock.SetParameters({.Frequency = 1e3, .DutyCycle = 0.5, .LogicLevel = 5.0, .StartDelay = 2e-3});
    Core::Circuit circuit;
    circuit.Add(clock, {1});

    const auto settings = Core::SuggestTransientSettings(circuit);
    REQUIRE(settings.has_value());
    // The 2 ms start delay plus five 1 ms periods is 7 ms, rounded up to 10 ms, and a 200th of the period is the step
    CHECK(settings->StopTime == Catch::Approx(10e-3));
    CHECK(settings->TimeStep == Catch::Approx(5e-6));
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

TEST_CASE("SuggestTransientSettings reads the piecewise linear and single-frequency FM waveforms", "[suggestions]") {
    SECTION("a piecewise linear span sets the stop time") {
        Core::VoltageSource source;
        source.SetSourceType(Core::Source::SourceType::PWL);
        source.SetPWL({.Points = {{0.0, 0.0}, {1e-3, 5.0}, {2e-3, 0.0}}});
        Core::Circuit circuit;
        circuit.Add(source, {1, 0});

        const auto settings = Core::SuggestTransientSettings(circuit);
        REQUIRE(settings.has_value());
        // The 2 ms span with some room after it, rounded up to 5 ms, and a 200th of the span as the step
        CHECK(settings->StopTime == Catch::Approx(5e-3));
        CHECK(settings->TimeStep == Catch::Approx(1e-5));
    }

    SECTION("an exponential covers its decay once instead of repeating") {
        Core::VoltageSource source;
        source.SetSourceType(Core::Source::SourceType::EXP);
        // The decay starts at 5 ms and lasts five time constants of 1 ms, so the wave ends at 10 ms
        source.SetEXP({.InitialValue = 0.0,
                       .PeakValue = 5.0,
                       .RiseDelay = 1e-3,
                       .RiseTimeConstant = 0.5e-3,
                       .FallDelay = 5e-3,
                       .FallTimeConstant = 1e-3});
        Core::Circuit circuit;
        circuit.Add(source, {1, 0});

        const auto settings = Core::SuggestTransientSettings(circuit);
        REQUIRE(settings.has_value());
        CHECK(settings->StopTime == Catch::Approx(20e-3));
        CHECK(settings->TimeStep == Catch::Approx(50e-6));
    }

    SECTION("a repeating source next to a one-shot one keeps the longer of the two views") {
        Core::VoltageSource sine;
        sine.SetSourceType(Core::Source::SourceType::AC);
        sine.SetAC({.Amplitude = 1.0, .Frequency = 1e3, .Offset = 0.0, .Magnitude = 1.0});
        Core::VoltageSource ramp;
        ramp.SetSourceType(Core::Source::SourceType::PWL);
        ramp.SetPWL({.Points = {{0.0, 0.0}, {20e-3, 5.0}}});
        Core::Circuit circuit;
        circuit.Add(sine, {1, 0});
        circuit.Add(ramp, {2, 0});

        const auto settings = Core::SuggestTransientSettings(circuit);
        REQUIRE(settings.has_value());
        // Five sine periods are 5 ms and the ramp with room is 24 ms, rounded up to 50 ms; the sine sets the step
        CHECK(settings->StopTime == Catch::Approx(50e-3));
        CHECK(settings->TimeStep == Catch::Approx(5e-6));
    }

    SECTION("an SFFM carrier and signal set the stop and the step") {
        Core::VoltageSource source;
        source.SetSourceType(Core::Source::SourceType::SFFM);
        source.SetSFFM({.Offset = 0.0,
                        .Amplitude = 1.0,
                        .CarrierFrequency = 1e3,
                        .ModulationIndex = 5.0,
                        .SignalFrequency = 100.0});
        Core::Circuit circuit;
        circuit.Add(source, {1, 0});

        const auto settings = Core::SuggestTransientSettings(circuit);
        REQUIRE(settings.has_value());
        // Five 10 ms signal periods, and a 200th of the 1 ms carrier period
        CHECK(settings->StopTime == Catch::Approx(0.05));
        CHECK(settings->TimeStep == Catch::Approx(5e-6));
    }
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
