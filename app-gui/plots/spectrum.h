/**
 * @file    spectrum.h
 * @brief   Frequency spectrum and harmonic distortion of a trace over a span of time, as a spectrum analyzer shows.
 */

#ifndef IMCSIM_SPECTRUM_H
#define IMCSIM_SPECTRUM_H

#include <cstddef>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace GUI {

/**
 * @enum    SpectrumWindow
 * @brief   Window the samples are weighed by before the transform.
 * @details Hann suits any span, at the cost of a wider peak per tone. Rectangular keeps every tone in one bin, but
 *          only when the span holds a whole number of periods; otherwise the tone leaks across the spectrum.
 */
enum class SpectrumWindow {
    HANN,
    RECTANGULAR
};

/**
 * @struct  Spectrum
 * @brief   Amplitude of the sine at each frequency of a trace, from 0 Hz to half the sampling rate.
 * @details Amplitudes are peaks, not RMS, corrected for the window, so a sine of 1 V reads 1 V at its frequency.
 *          The first bin holds the mean, with its sign.
 */
struct Spectrum {
    std::vector<double> Frequencies;
    std::vector<double> Amplitudes;
    SpectrumWindow Window = SpectrumWindow::HANN;
};

/**
 * @struct  Harmonic
 * @brief   One harmonic of the fundamental of a trace: its order, where it was found and its peak amplitude.
 */
struct Harmonic {
    int Order = 1;
    double Frequency = 0.0;
    double Amplitude = 0.0;
};

/**
 * @struct  HarmonicAnalysis
 * @brief   The mean of a trace, its fundamental and the harmonics above it, and their total distortion.
 */
struct HarmonicAnalysis {
    double Mean = 0.0;
    /// The fundamental first; empty when the trace holds no tone.
    std::vector<Harmonic> Harmonics;
    /// Root sum of squares of the harmonics over the fundamental; no value without a fundamental and a second harmonic.
    std::optional<double> TotalDistortion;
    /// Periods of the fundamental the span must hold, when it holds fewer: the peaks of the mean, the fundamental and
    /// its harmonics would overlap, so no harmonic is measured.
    std::optional<int> PeriodsNeeded;
    /// Set when the trace varies but no tone stands out of the floor around its strongest peak, as in noise: it has no
    /// fundamental or distortion to measure, and its density tells more.
    bool IsBroadband = false;
};

/**
 * @struct  NoiseBand
 * @brief   The noise of a trace over a band of frequencies.
 */
struct NoiseBand {
    /// Level of the density over the band, in the unit of the trace per square root of hertz: the floor a user sees,
    /// found by the median of the bins of the highest slice, so that a part of the band past the noise does not pull it
    /// down.
    double Density = 0.0;
    /// Root of the noise power in the band, in the unit of the trace.
    double TotalRms = 0.0;
};

std::size_t ChooseSpectrumSampleCount(std::span<const double> xs, double from, double to);
std::optional<Spectrum> ComputeSpectrum(std::span<const double> xs, std::span<const double> ys, double from, double to,
                                        std::size_t sample_count, SpectrumWindow window);
HarmonicAnalysis AnalyzeHarmonics(const Spectrum &spectrum, int highest_order);
std::vector<double> ToDecibels(std::span<const double> amplitudes);

// Noise
std::vector<double> ComputeNoiseDensities(const Spectrum &spectrum);
std::optional<NoiseBand> MeasureNoiseBand(std::span<const double> frequencies, std::span<const double> densities,
                                          double from, double to);
std::optional<std::pair<double, double>> FindDensityRange(std::span<const double> densities);

} // namespace GUI

#endif // IMCSIM_SPECTRUM_H
