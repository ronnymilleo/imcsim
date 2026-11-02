/**
 * @file    spectrum_tests.cpp
 * @brief   Tests for the spectrum and harmonic distortion of traces.
 */

#include "spectrum.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <functional>
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

// A signal sampled unevenly, as ngspice would, alternating short and long steps
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
