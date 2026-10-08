/**
 * @file    trace_statistics_tests.cpp
 * @brief   Tests for the oscilloscope and Bode measurements of traces.
 */

#include "trace_statistics.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

namespace {

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

/**
 * @struct  Trace
 * @brief   Samples of a test signal.
 */
struct Trace {
    std::vector<double> Xs;
    std::vector<double> Ys;
};

// A sine with an offset, sampled unevenly, as ngspice would: dense in the first half of each period
Trace Sine(const double amplitude, const double offset, const double frequency, const int periods) {
    Trace trace;
    const int samples_per_period = 200;
    for (int period = 0; period < periods; ++period) {
        for (int index = 0; index < samples_per_period; ++index) {
            const double fraction = static_cast<double>(index) / samples_per_period;
            const double phase = fraction < 0.5 ? fraction * 0.8 : 0.4 + (fraction - 0.5) * 1.2;
            const double time = (period + phase) / frequency;
            trace.Xs.push_back(time);
            trace.Ys.push_back(offset + amplitude * std::sin(2.0 * std::numbers::pi * frequency * time));
        }
    }
    trace.Xs.push_back(periods / frequency);
    trace.Ys.push_back(offset);
    return trace;
}

// Bode response of a first order low-pass with its corner at corner_frequency
void LowPass(const double corner_frequency, std::vector<double> &frequencies, std::vector<double> &magnitudes,
             std::vector<double> &phases) {
    for (double decade = 0.0; decade <= 6.0; decade += 0.01) {
        const double frequency = std::pow(10.0, decade);
        const std::complex<double> response = 1.0 / std::complex<double>(1.0, frequency / corner_frequency);
        frequencies.push_back(frequency);
        magnitudes.push_back(20.0 * std::log10(std::abs(response)));
        phases.push_back(std::arg(response) * 180.0 / std::numbers::pi);
    }
}

} // namespace

TEST_CASE("InterpolateAt reads between samples and holds the ends", "[trace_statistics]") {
    const std::vector<double> xs = {0.0, 1.0, 3.0};
    const std::vector<double> ys = {0.0, 10.0, 30.0};
    CHECK_THAT(GUI::InterpolateAt(xs, ys, 0.5, false), WithinAbs(5.0, 1e-12));
    CHECK_THAT(GUI::InterpolateAt(xs, ys, 2.0, false), WithinAbs(20.0, 1e-12));
    CHECK(GUI::InterpolateAt(xs, ys, -1.0, false) == 0.0);
    CHECK(GUI::InterpolateAt(xs, ys, 9.0, false) == 30.0);
    CHECK(GUI::InterpolateAt({}, {}, 1.0, false) == 0.0);

    // Halfway in decades between 10 and 1000 is 100
    const std::vector<double> frequencies = {10.0, 1000.0};
    const std::vector<double> gains = {0.0, -40.0};
    CHECK_THAT(GUI::InterpolateAt(frequencies, gains, 100.0, true), WithinAbs(-20.0, 1e-9));
}

TEST_CASE("A sine measures its peak to peak, mean, RMS and frequency", "[trace_statistics]") {
    const Trace sine = Sine(2.0, 1.0, 1e3, 5);
    const auto statistics = GUI::ComputeTraceStatistics(sine.Xs, sine.Ys, 0.0, 5e-3);
    REQUIRE(statistics);
    CHECK_THAT(statistics->PeakToPeak, WithinRel(4.0, 1e-3));
    CHECK_THAT(statistics->Minimum, WithinRel(-1.0, 1e-3));
    CHECK_THAT(statistics->Maximum, WithinRel(3.0, 1e-3));
    CHECK_THAT(statistics->Mean, WithinAbs(1.0, 1e-3));
    // RMS of a sine with an offset: sqrt(offset^2 + amplitude^2 / 2)
    CHECK_THAT(statistics->RMS, WithinRel(std::sqrt(1.0 + 2.0), 1e-3));
    REQUIRE(statistics->Frequency);
    CHECK_THAT(*statistics->Frequency, WithinRel(1e3, 1e-3));
}

TEST_CASE("Statistics cover only the span, in either order", "[trace_statistics]") {
    // A ramp from 0 to 10 over 10 s: between 2 s and 4 s its mean is 3
    const std::vector<double> xs = {0.0, 10.0};
    const std::vector<double> ys = {0.0, 10.0};
    const auto statistics = GUI::ComputeTraceStatistics(xs, ys, 4.0, 2.0);
    REQUIRE(statistics);
    CHECK_THAT(statistics->Mean, WithinAbs(3.0, 1e-12));
    CHECK_THAT(statistics->PeakToPeak, WithinAbs(2.0, 1e-12));
    CHECK_FALSE(statistics->Frequency);

    CHECK_FALSE(GUI::ComputeTraceStatistics(xs, ys, 20.0, 30.0));
    CHECK_FALSE(GUI::ComputeTraceStatistics(xs, ys, 5.0, 5.0));
}

TEST_CASE("Noise around the mean does not add cycles", "[trace_statistics]") {
    // A 100 Hz sine with fast noise, which crosses the mean several times on each slow crossing
    Trace noisy;
    for (int index = 0; index <= 10000; ++index) {
        const double time = index * 1e-5;
        const double noise = 0.02 * ((index % 2 == 0) ? 1.0 : -1.0);
        noisy.Xs.push_back(time);
        noisy.Ys.push_back(std::sin(2.0 * std::numbers::pi * 100.0 * time) + noise);
    }
    const auto statistics = GUI::ComputeTraceStatistics(noisy.Xs, noisy.Ys, 0.0, 0.1);
    REQUIRE(statistics);
    REQUIRE(statistics->Frequency);
    CHECK_THAT(*statistics->Frequency, WithinRel(100.0, 1e-2));
}

TEST_CASE("A first order low-pass shows its corner and no unity gain crossing", "[trace_statistics]") {
    std::vector<double> frequencies;
    std::vector<double> magnitudes;
    std::vector<double> phases;
    LowPass(1e3, frequencies, magnitudes, phases);
    const auto statistics = GUI::ComputeBodeStatistics(frequencies, magnitudes, phases, 1.0, 1e6);
    REQUIRE(statistics);
    CHECK_THAT(statistics->PeakGain, WithinAbs(0.0, 1e-4));
    REQUIRE(statistics->UpperCutoff);
    CHECK_THAT(*statistics->UpperCutoff, WithinRel(1e3, 1e-2));
    CHECK_FALSE(statistics->LowerCutoff);
    CHECK_FALSE(statistics->UnityGainFrequency);
}

TEST_CASE("A loop gain shows its unity gain frequency and phase margin", "[trace_statistics]") {
    // 40 dB of gain with a pole at 100 Hz crosses 0 dB near 10 kHz, where the phase is near -90 degrees
    std::vector<double> frequencies;
    std::vector<double> magnitudes;
    std::vector<double> phases;
    LowPass(100.0, frequencies, magnitudes, phases);
    for (double &magnitude : magnitudes) {
        magnitude += 40.0;
    }
    const auto statistics = GUI::ComputeBodeStatistics(frequencies, magnitudes, phases, 1.0, 1e6);
    REQUIRE(statistics);
    REQUIRE(statistics->UnityGainFrequency);
    CHECK_THAT(*statistics->UnityGainFrequency, WithinRel(1e4, 1e-2));
    REQUIRE(statistics->PhaseMargin);
    CHECK_THAT(*statistics->PhaseMargin, WithinAbs(90.6, 0.2));
}

TEST_CASE("A band-pass shows both cutoffs around its peak", "[trace_statistics]") {
    // Gain rises 20 dB per decade up to 100 Hz, stays flat, and falls 20 dB per decade from 10 kHz
    std::vector<double> frequencies;
    std::vector<double> magnitudes;
    for (double decade = 0.0; decade <= 6.0; decade += 0.01) {
        const double frequency = std::pow(10.0, decade);
        const std::complex<double> high_pass =
            std::complex<double>(0.0, frequency / 100.0) / std::complex<double>(1.0, frequency / 100.0);
        const std::complex<double> low_pass = 1.0 / std::complex<double>(1.0, frequency / 1e4);
        frequencies.push_back(frequency);
        magnitudes.push_back(20.0 * std::log10(std::abs(high_pass * low_pass)));
    }
    const std::vector<double> phases(frequencies.size(), 0.0);
    const auto statistics = GUI::ComputeBodeStatistics(frequencies, magnitudes, phases, 1.0, 1e6);
    REQUIRE(statistics);
    REQUIRE(statistics->LowerCutoff);
    REQUIRE(statistics->UpperCutoff);
    CHECK_THAT(*statistics->LowerCutoff, WithinRel(100.0, 3e-2));
    CHECK_THAT(*statistics->UpperCutoff, WithinRel(1e4, 3e-2));
}

TEST_CASE("Floating point residue is negligible, real small values and exact zeros are not", "[trace_statistics]") {
    CHECK(GUI::IsNegligible(-1.778e-46, 0.242));
    CHECK(GUI::IsNegligible(4.216e-22, 15.56));
    CHECK_FALSE(GUI::IsNegligible(1e-6, 1e-3));
    CHECK_FALSE(GUI::IsNegligible(-0.5, 0.5));
    CHECK_FALSE(GUI::IsNegligible(0.0, 1.0));
    // A trace that is all tiny keeps its values, since nothing larger shows they are residue
    CHECK_FALSE(GUI::IsNegligible(1e-15, 1e-15));
    CHECK_FALSE(GUI::IsNegligible(1e-15, 0.0));
}
