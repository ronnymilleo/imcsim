/**
 * @file    spectrum_tests.cpp
 * @brief   Tests for the spectrum and harmonic distortion of traces.
 */

#include "plots/spectrum.h"

#include "simulation/monte_carlo.h"
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <functional>
#include <limits>
#include <numbers>
#include <random>
#include <span>
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

/**
 * @brief   A signal sampled unevenly, as ngspice would, alternating short and long steps.
 */
Trace Sample(const std::function<double(double)> &signal, const double stop_time, const int point_count) {
    Trace trace;
    const double average_step = stop_time / point_count;
    double time = 0.0;
    for (int index = 0; time < stop_time; ++index) {
        trace.Xs.push_back(time);
        trace.Ys.push_back(signal(time));
        time += average_step * (index % 2 == 0 ? 0.7 : 1.3);
    }
    trace.Xs.push_back(stop_time);
    trace.Ys.push_back(signal(stop_time));
    return trace;
}

double Sine(const double amplitude, const double frequency, const double time) {
    return amplitude * std::sin(2.0 * std::numbers::pi * frequency * time);
}

/// Samples per noise trace; with the 1 ms step the spectrum covers 0 to 500 Hz. One more point closes the span, so
/// the transform reads exactly the samples, without the smoothing of interpolating between them.
constexpr int NOISE_SAMPLE_COUNT = 4097;
constexpr double NOISE_STEP = 1e-3;

/**
 * @brief   Gaussian white noise on an even grid.
 */
Trace WhiteNoise(const double sigma, const unsigned seed) {
    // The draws of the Monte Carlo runs are the same on every standard library, unlike std::normal_distribution
    std::mt19937_64 generator(seed);
    Trace trace;
    for (int index = 0; index < NOISE_SAMPLE_COUNT; ++index) {
        trace.Xs.push_back(index * NOISE_STEP);
        trace.Ys.push_back(sigma * Core::DrawStandardNormal(generator));
    }
    return trace;
}

/**
 * @brief   Noise whose amplitude per bin falls as 1/f^(power/2) (power 1 is 1/f noise, 2 is brown noise): random
 *          components at the frequency of each bin below 450 Hz.
 */
Trace SlopedNoise(const double power, const unsigned seed) {
    std::mt19937_64 generator(seed);
    const auto gauss = [](std::mt19937_64 &engine) { return Core::DrawStandardNormal(engine); };
    const auto phase = [](std::mt19937_64 &engine) { return 2.0 * std::numbers::pi * Core::DrawUniform(engine); };
    const double span = (NOISE_SAMPLE_COUNT - 1) * NOISE_STEP;
    std::vector<std::array<double, 3>> components;
    for (int bin = 1; bin < 1800; ++bin) {
        const double amplitude = std::hypot(gauss(generator), gauss(generator)) / std::pow(bin, power / 2.0);
        components.push_back({bin / span, amplitude, phase(generator)});
    }
    Trace trace;
    for (int index = 0; index < NOISE_SAMPLE_COUNT; ++index) {
        const double time = index * NOISE_STEP;
        double value = 0.0;
        for (const auto &[frequency, amplitude, offset] : components) {
            value += amplitude * std::sin(2.0 * std::numbers::pi * frequency * time + offset);
        }
        trace.Xs.push_back(time);
        trace.Ys.push_back(value);
    }
    return trace;
}

GUI::HarmonicAnalysis AnalyzeTrace(const Trace &trace) {
    const auto spectrum =
        GUI::ComputeSpectrum(trace.Xs, trace.Ys, 0.0, trace.Xs.back(), 4096, GUI::SpectrumWindow::HANN);
    REQUIRE(spectrum);
    return GUI::AnalyzeHarmonics(*spectrum, 10);
}

} // namespace

TEST_CASE("ChooseSpectrumSampleCount follows the samples in the span", "[spectrum]") {
    std::vector<double> xs;
    for (int index = 0; index <= 5000; ++index) {
        xs.push_back(index * 1e-6);
    }
    CHECK(GUI::ChooseSpectrumSampleCount(xs, 0.0, 5e-3) == 8192);
    // Spans come in any order, and a short one keeps the smallest grid
    CHECK(GUI::ChooseSpectrumSampleCount(xs, 1e-4, 0.0) == 1024);
    CHECK(GUI::ChooseSpectrumSampleCount({}, 0.0, 1.0) == 1024);
    std::vector<double> long_run;
    for (int index = 0; index <= 200000; ++index) {
        long_run.push_back(index * 1e-6);
    }
    CHECK(GUI::ChooseSpectrumSampleCount(long_run, 0.0, 1.0) == 65536);
}

TEST_CASE("ComputeSpectrum reads the peak amplitude and the mean of a sine", "[spectrum]") {
    // Ten periods of 1 kHz with 1 V of offset
    const Trace trace = Sample([](const double time) { return 1.0 + Sine(2.0, 1e3, time); }, 10e-3, 4000);
    for (const GUI::SpectrumWindow window : {GUI::SpectrumWindow::RECTANGULAR, GUI::SpectrumWindow::HANN}) {
        const auto spectrum = GUI::ComputeSpectrum(trace.Xs, trace.Ys, 0.0, 10e-3, 4096, window);
        REQUIRE(spectrum);
        REQUIRE(spectrum->Frequencies.size() == 2049);
        CHECK_THAT(spectrum->Frequencies[1], WithinRel(100.0, 1e-12));
        // Bin 10 is 1 kHz, a whole number of periods, so the tone sits in it
        CHECK_THAT(spectrum->Amplitudes[10], WithinRel(2.0, 1e-3));
        CHECK_THAT(spectrum->Amplitudes[0], WithinRel(1.0, 1e-3));
    }
}

TEST_CASE("ComputeSpectrum rejects spans without a trace and odd sample counts", "[spectrum]") {
    const Trace trace = Sample([](const double time) { return Sine(1.0, 1e3, time); }, 1e-3, 100);
    CHECK_FALSE(GUI::ComputeSpectrum(trace.Xs, trace.Ys, 2e-3, 3e-3, 1024, GUI::SpectrumWindow::HANN));
    CHECK_FALSE(GUI::ComputeSpectrum(trace.Xs, trace.Ys, 0.0, 1e-3, 1000, GUI::SpectrumWindow::HANN));
    CHECK_FALSE(GUI::ComputeSpectrum({}, {}, 0.0, 1e-3, 1024, GUI::SpectrumWindow::HANN));
    // Reversed ends measure the same span
    CHECK(GUI::ComputeSpectrum(trace.Xs, trace.Ys, 1e-3, 0.0, 1024, GUI::SpectrumWindow::HANN));
}

TEST_CASE("AnalyzeHarmonics measures the distortion of a clipped-like signal", "[spectrum]") {
    // 1 V at 1 kHz with 10% of second and 5% of third harmonic: THD is sqrt(0.01 + 0.0025)
    const auto signal = [](const double time) {
        return Sine(1.0, 1e3, time) + Sine(0.1, 2e3, time) + Sine(0.05, 3e3, time);
    };
    const Trace trace = Sample(signal, 20e-3, 20000);
    const double expected = std::sqrt(0.01 + 0.0025);

    SECTION("with a whole number of periods and a rectangular window") {
        const auto spectrum =
            GUI::ComputeSpectrum(trace.Xs, trace.Ys, 0.0, 20e-3, 32768, GUI::SpectrumWindow::RECTANGULAR);
        REQUIRE(spectrum);
        const GUI::HarmonicAnalysis analysis = GUI::AnalyzeHarmonics(*spectrum, 10);
        REQUIRE(analysis.Harmonics.size() == 10);
        CHECK(analysis.Harmonics[0].Order == 1);
        CHECK_THAT(analysis.Harmonics[0].Frequency, WithinRel(1e3, 1e-6));
        CHECK_THAT(analysis.Harmonics[0].Amplitude, WithinRel(1.0, 1e-3));
        CHECK_THAT(analysis.Harmonics[1].Amplitude, WithinRel(0.1, 1e-3));
        CHECK_THAT(analysis.Harmonics[2].Amplitude, WithinRel(0.05, 1e-3));
        CHECK_THAT(analysis.Harmonics[3].Amplitude, WithinAbs(0.0, 1e-4));
        REQUIRE(analysis.TotalDistortion);
        CHECK_THAT(*analysis.TotalDistortion, WithinRel(expected, 1e-2));
    }
    SECTION("with a span that cuts a period and a Hann window") {
        const auto spectrum =
            GUI::ComputeSpectrum(trace.Xs, trace.Ys, 0.3e-3, 17.55e-3, 32768, GUI::SpectrumWindow::HANN);
        REQUIRE(spectrum);
        const GUI::HarmonicAnalysis analysis = GUI::AnalyzeHarmonics(*spectrum, 10);
        REQUIRE(analysis.Harmonics.size() >= 3);
        CHECK_THAT(analysis.Harmonics[0].Frequency, WithinRel(1e3, 1e-2));
        CHECK_THAT(analysis.Harmonics[0].Amplitude, WithinRel(1.0, 1e-2));
        CHECK_THAT(analysis.Harmonics[1].Amplitude, WithinRel(0.1, 2e-2));
        REQUIRE(analysis.TotalDistortion);
        CHECK_THAT(*analysis.TotalDistortion, WithinRel(expected, 3e-2));
    }
}

TEST_CASE("AnalyzeHarmonics finds no fundamental in a constant trace", "[spectrum]") {
    const Trace trace = Sample([](double) { return 5.0; }, 1e-3, 1000);
    const auto spectrum = GUI::ComputeSpectrum(trace.Xs, trace.Ys, 0.0, 1e-3, 1024, GUI::SpectrumWindow::HANN);
    REQUIRE(spectrum);
    const GUI::HarmonicAnalysis analysis = GUI::AnalyzeHarmonics(*spectrum, 10);
    CHECK_THAT(analysis.Mean, WithinRel(5.0, 1e-9));
    CHECK(analysis.Harmonics.empty());
    CHECK_FALSE(analysis.TotalDistortion);
}

TEST_CASE("AnalyzeHarmonics stops at half the sampling rate", "[spectrum]") {
    // 1024 samples over 1 ms put half the sampling rate at 512 kHz, past the third harmonic of 150 kHz only
    const Trace trace = Sample([](const double time) { return Sine(1.0, 150e3, time); }, 1e-3, 20000);
    const auto spectrum = GUI::ComputeSpectrum(trace.Xs, trace.Ys, 0.0, 1e-3, 1024, GUI::SpectrumWindow::HANN);
    REQUIRE(spectrum);
    const GUI::HarmonicAnalysis analysis = GUI::AnalyzeHarmonics(*spectrum, 10);
    CHECK(analysis.Harmonics.size() == 3);
}

TEST_CASE("ToDecibels floors the residue 200 dB below the strongest bin", "[spectrum]") {
    const std::vector<double> decibels = GUI::ToDecibels(std::vector<double>{1.0, 0.1, 1e-20, 0.0});
    REQUIRE(decibels.size() == 4);
    CHECK_THAT(decibels[0], WithinAbs(0.0, 1e-12));
    CHECK_THAT(decibels[1], WithinAbs(-20.0, 1e-12));
    CHECK_THAT(decibels[2], WithinAbs(-200.0, 1e-9));
    CHECK_THAT(decibels[3], WithinAbs(-200.0, 1e-9));
    // A trace of zeros plots at -300 dB, as in the AC sweep
    CHECK_THAT(GUI::ToDecibels(std::vector<double>{0.0, 0.0}).front(), WithinAbs(-300.0, 1e-9));
}

TEST_CASE("AnalyzeHarmonics asks for more periods when the peaks would overlap", "[spectrum]") {
    // Three periods: enough for the rectangular window, too few for the wider peaks of Hann
    const Trace trace = Sample([](const double time) { return 0.5 + Sine(1.0, 1e3, time); }, 3e-3, 3000);
    const auto hann = GUI::ComputeSpectrum(trace.Xs, trace.Ys, 0.0, 3e-3, 4096, GUI::SpectrumWindow::HANN);
    REQUIRE(hann);
    const GUI::HarmonicAnalysis hann_analysis = GUI::AnalyzeHarmonics(*hann, 10);
    CHECK(hann_analysis.Harmonics.empty());
    CHECK(hann_analysis.PeriodsNeeded == 5);
    CHECK_THAT(hann_analysis.Mean, WithinRel(0.5, 1e-3));

    const auto rectangular =
        GUI::ComputeSpectrum(trace.Xs, trace.Ys, 0.0, 3e-3, 4096, GUI::SpectrumWindow::RECTANGULAR);
    REQUIRE(rectangular);
    const GUI::HarmonicAnalysis rectangular_analysis = GUI::AnalyzeHarmonics(*rectangular, 10);
    CHECK_FALSE(rectangular_analysis.PeriodsNeeded);
    REQUIRE_FALSE(rectangular_analysis.Harmonics.empty());
    CHECK_THAT(rectangular_analysis.Harmonics[0].Amplitude, WithinRel(1.0, 1e-3));
    REQUIRE(rectangular_analysis.TotalDistortion);
    CHECK(*rectangular_analysis.TotalDistortion < 1e-3);
}

TEST_CASE("AnalyzeHarmonics sees a tone in a sine, with or without noise", "[spectrum]") {
    const Trace pure = Sample([](const double time) { return Sine(1.0, 100.0, time); }, 4.0, 4000);
    CHECK_FALSE(AnalyzeTrace(pure).IsBroadband);
    CHECK_FALSE(AnalyzeTrace(pure).Harmonics.empty());

    // 5% of white noise, about -26 dB below the tone and far more per bin: the tone stands well out of the floor
    for (unsigned seed = 1; seed <= 10; ++seed) {
        Trace noisy = WhiteNoise(0.05, seed);
        for (std::size_t index = 0; index < noisy.Xs.size(); ++index) {
            noisy.Ys[index] += Sine(1.0, 100.0, noisy.Xs[index]);
        }
        const GUI::HarmonicAnalysis analysis = AnalyzeTrace(noisy);
        INFO(seed);
        CHECK_FALSE(analysis.IsBroadband);
        REQUIRE_FALSE(analysis.Harmonics.empty());
        CHECK_THAT(analysis.Harmonics.front().Frequency, WithinRel(100.0, 1e-2));
    }
}

TEST_CASE("AnalyzeHarmonics sees a tone in a square wave", "[spectrum]") {
    const Trace trace =
        Sample([](const double time) { return Sine(1.0, 100.0, time) >= 0.0 ? 1.0 : -1.0; }, 4.0, 40000);
    const GUI::HarmonicAnalysis analysis = AnalyzeTrace(trace);
    CHECK_FALSE(analysis.IsBroadband);
    REQUIRE_FALSE(analysis.Harmonics.empty());
    CHECK_THAT(analysis.Harmonics.front().Frequency, WithinRel(100.0, 1e-2));
}

TEST_CASE("AnalyzeHarmonics calls white, 1/f and brown noise broadband and measures no harmonics", "[spectrum]") {
    for (unsigned seed = 1; seed <= 20; ++seed) {
        INFO(seed);
        const GUI::HarmonicAnalysis white = AnalyzeTrace(WhiteNoise(1.0, seed));
        CHECK(white.IsBroadband);
        CHECK(white.Harmonics.empty());
        CHECK_FALSE(white.PeriodsNeeded);
        CHECK_FALSE(white.TotalDistortion);
        CHECK(AnalyzeTrace(SlopedNoise(1.0, seed)).IsBroadband);
    }
    for (unsigned seed = 1; seed <= 5; ++seed) {
        INFO(seed);
        CHECK(AnalyzeTrace(SlopedNoise(2.0, seed)).IsBroadband);
    }
}

TEST_CASE("A constant trace is not broadband noise", "[spectrum]") {
    const Trace trace = Sample([](double) { return 5.0; }, 1e-3, 1000);
    const auto spectrum = GUI::ComputeSpectrum(trace.Xs, trace.Ys, 0.0, 1e-3, 1024, GUI::SpectrumWindow::HANN);
    REQUIRE(spectrum);
    CHECK_FALSE(GUI::AnalyzeHarmonics(*spectrum, 10).IsBroadband);
}

TEST_CASE("The noise density of white noise is sigma times the root of twice the step, whatever the window",
          "[spectrum]") {
    const double sigma = 0.1;
    // One-sided density: variance sigma^2 spread over half the sampling rate, 1 / (2 step)
    const double expected = sigma * std::sqrt(2.0 * NOISE_STEP);
    for (const GUI::SpectrumWindow window : {GUI::SpectrumWindow::HANN, GUI::SpectrumWindow::RECTANGULAR}) {
        const Trace trace = WhiteNoise(sigma, 7);
        const auto spectrum = GUI::ComputeSpectrum(trace.Xs, trace.Ys, 0.0, trace.Xs.back(), 4096, window);
        REQUIRE(spectrum);
        const std::vector<double> densities = GUI::ComputeNoiseDensities(*spectrum);
        REQUIRE(densities.size() == spectrum->Frequencies.size() - 1);
        const std::span<const double> frequencies = std::span(spectrum->Frequencies).subspan(1);
        const auto band = GUI::MeasureNoiseBand(frequencies, densities, 0.0, 1e6);
        REQUIRE(band);
        CHECK_THAT(band->Density, WithinRel(expected, 0.12));
        // The whole band holds the whole variance, up to the mean and the bin at 0 Hz
        CHECK_THAT(band->TotalRms, WithinRel(sigma, 0.05));
        // A narrower band has the same density but less noise
        const auto narrow = GUI::MeasureNoiseBand(frequencies, densities, 100.0, 200.0);
        REQUIRE(narrow);
        CHECK_THAT(narrow->Density, WithinRel(expected, 0.25));
        CHECK(narrow->TotalRms < band->TotalRms);
    }
}

TEST_CASE("MeasureNoiseBand has no value for a band without bins", "[spectrum]") {
    const std::vector<double> frequencies{1.0, 2.0, 3.0, 4.0, 5.0};
    const std::vector<double> densities(5, 1.0);
    CHECK_FALSE(GUI::MeasureNoiseBand(frequencies, densities, 10.0, 20.0));
    CHECK_FALSE(GUI::MeasureNoiseBand({}, {}, 0.0, 1.0));
    // The ends come in any order, and the two bins next to 0 Hz are left out
    const auto band = GUI::MeasureNoiseBand(frequencies, densities, 5.0, 1.0);
    REQUIRE(band);
    // The median of three equal bins is the bin, which white noise would read sqrt(ln 2) lower
    CHECK_THAT(band->Density, WithinRel(1.0 / std::sqrt(std::numbers::ln2), 1e-12));
    CHECK_THAT(band->TotalRms, WithinAbs(std::sqrt(3.0), 1e-12));
}

TEST_CASE("The density level is the floor even when part of the band lies past the noise", "[spectrum]") {
    // White noise to 400 Hz, then a roll-off to a thousandth of it, as sampled noise shows past its own bandwidth
    const Trace trace = WhiteNoise(0.1, 3);
    const auto spectrum =
        GUI::ComputeSpectrum(trace.Xs, trace.Ys, 0.0, trace.Xs.back(), 4096, GUI::SpectrumWindow::HANN);
    REQUIRE(spectrum);
    std::vector<double> densities = GUI::ComputeNoiseDensities(*spectrum);
    const std::span<const double> frequencies = std::span(spectrum->Frequencies).subspan(1);
    const double expected = 0.1 * std::sqrt(2.0 * NOISE_STEP);
    for (std::size_t index = 0; index < densities.size(); ++index) {
        if (frequencies[index] > 400.0) {
            densities[index] *= 1e-3;
        }
    }
    const auto band = GUI::MeasureNoiseBand(frequencies, densities, 0.0, 500.0);
    REQUIRE(band);
    CHECK_THAT(band->Density, WithinRel(expected, 0.12));
}

TEST_CASE("FindDensityRange leaves out the mean skirt, empty bins and the decades below the curve", "[spectrum]") {
    const double floor = 1e-18;
    // The first two bins are the skirt of the mean, then a curve from 1e-3 to 1e-4 with a bin at the floor and one
    // that is not finite
    const std::vector<double> densities{1.0, 1.0, 1e-3, 5e-4, floor, 1e-4, std::numeric_limits<double>::quiet_NaN()};
    const auto range = GUI::FindDensityRange(densities);
    REQUIRE(range);
    CHECK_THAT(range->first, WithinRel(1e-4, 1e-12));
    CHECK_THAT(range->second, WithinRel(1e-3, 1e-12));
    // A bin far below the others is cut at 6 decades under the highest
    const std::vector<double> wide{0.0, 0.0, 1.0, 1e-9, 1e-2};
    const auto clamped = GUI::FindDensityRange(wide);
    REQUIRE(clamped);
    CHECK_THAT(clamped->first, WithinRel(1e-6, 1e-12));
    CHECK_THAT(clamped->second, WithinRel(1.0, 1e-12));
    CHECK_FALSE(GUI::FindDensityRange(std::vector<double>{1.0, 1.0, floor, 0.0}));
    CHECK_FALSE(GUI::FindDensityRange({}));
}
