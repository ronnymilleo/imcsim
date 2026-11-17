/**
 * @file    signal_measures_tests.cpp
 * @brief   Tests for the waveform and frequency-response figures of the MCP answers.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "mcp/signal_measures.h"

#include <cmath>
#include <numbers>
#include <vector>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

TEST_CASE("A waveform is measured with its extremes, final value and time-weighted mean", "[signal_measures]") {
    // A ramp from 0 to 2 V over 2 s with an uneven step: the mean is 1 V whatever the spacing of the samples
    const std::vector<double> times = {0.0, 0.1, 0.2, 1.5, 2.0};
    const std::vector<double> values = {0.0, 0.1, 0.2, 1.5, 2.0};

    const auto figures = Nodalys::MeasureWaveform(times, values);

    REQUIRE(figures.has_value());
    CHECK(figures->Minimum == 0.0);
    CHECK(figures->Maximum == 2.0);
    CHECK(figures->Final == 2.0);
    CHECK_THAT(figures->Mean, WithinAbs(1.0, 1e-12));
    // The RMS of a ramp 0..2 is 2 / sqrt(3)
    CHECK_THAT(figures->Rms, WithinRel(2.0 / std::sqrt(3.0), 1e-9));
}

TEST_CASE("A waveform of one point has the value of that point as its mean", "[signal_measures]") {
    const auto figures = Nodalys::MeasureWaveform(std::vector<double>{0.0}, std::vector<double>{-3.0});

    REQUIRE(figures.has_value());
    CHECK(figures->Mean == -3.0);
    CHECK(figures->Rms == 3.0);
}

TEST_CASE("A waveform with no samples or mismatched lists has no figures", "[signal_measures]") {
    CHECK_FALSE(Nodalys::MeasureWaveform(std::vector<double>{}, std::vector<double>{}).has_value());
    CHECK_FALSE(Nodalys::MeasureWaveform(std::vector<double>{0.0, 1.0}, std::vector<double>{1.0}).has_value());
}

TEST_CASE("A low-pass response has an upper cutoff 3 dB under its peak and no lower one", "[signal_measures]") {
    // One pole at 1 kHz, sampled 20 points per decade
    constexpr double POLE_HERTZ = 1e3;
    std::vector<double> frequencies;
    std::vector<double> gains;
    std::vector<double> phases;
    for (int step = 0; step <= 80; ++step) {
        const double frequency = std::pow(10.0, step / 20.0);
        const double ratio = frequency / POLE_HERTZ;
        frequencies.push_back(frequency);
        gains.push_back(-10.0 * std::log10(1.0 + ratio * ratio));
        phases.push_back(-std::atan(ratio) * 180.0 / std::numbers::pi);
    }

    const auto figures = Nodalys::MeasureResponse(frequencies, gains, phases);

    REQUIRE(figures.has_value());
    CHECK_THAT(figures->PeakFrequency, WithinAbs(1.0, 1e-12));
    CHECK_FALSE(figures->LowerCutoffFrequency.has_value());
    REQUIRE(figures->UpperCutoffFrequency.has_value());
    CHECK_THAT(*figures->UpperCutoffFrequency, WithinRel(POLE_HERTZ, 0.02));
    // The gain never rises above 0 dB, so it has no unity-gain crossing
    CHECK_FALSE(figures->UnityGainFrequency.has_value());
}

TEST_CASE("The unity-gain frequency and the phase there come from the first fall through 0 dB", "[signal_measures]") {
    const std::vector<double> frequencies = {10.0, 100.0, 1000.0};
    const std::vector<double> gains = {20.0, 0.0 + 10.0, -10.0};
    const std::vector<double> phases = {-90.0, -100.0, -140.0};

    const auto figures = Nodalys::MeasureResponse(frequencies, gains, phases);

    REQUIRE(figures.has_value());
    REQUIRE(figures->UnityGainFrequency.has_value());
    // 0 dB lies half way between +10 and -10 dB, so half a decade above 100 Hz
    CHECK_THAT(*figures->UnityGainFrequency, WithinRel(std::pow(10.0, 2.5), 1e-9));
    REQUIRE(figures->PhaseAtUnityGainDegrees.has_value());
    CHECK_THAT(*figures->PhaseAtUnityGainDegrees, WithinAbs(-120.0, 1e-9));
}

TEST_CASE("A band-pass response has both cutoffs around its peak", "[signal_measures]") {
    const std::vector<double> frequencies = {1.0, 10.0, 100.0, 1000.0, 10000.0};
    const std::vector<double> gains = {-30.0, -10.0, 0.0, -10.0, -30.0};
    const std::vector<double> phases = {90.0, 60.0, 0.0, -60.0, -90.0};

    const auto figures = Nodalys::MeasureResponse(frequencies, gains, phases);

    REQUIRE(figures.has_value());
    CHECK_THAT(figures->PeakFrequency, WithinAbs(100.0, 1e-12));
    REQUIRE(figures->LowerCutoffFrequency.has_value());
    REQUIRE(figures->UpperCutoffFrequency.has_value());
    CHECK(*figures->LowerCutoffFrequency < 100.0);
    CHECK(*figures->LowerCutoffFrequency > 10.0);
    CHECK(*figures->UpperCutoffFrequency > 100.0);
    CHECK(*figures->UpperCutoffFrequency < 1000.0);
}
