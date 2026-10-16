/**
 * @file    spectrum.cpp
 * @brief   Frequency spectrum and harmonic distortion of a trace over a span of time, as a spectrum analyzer shows.
 */

#include "spectrum.h"

#include "trace_statistics.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <complex>
#include <numbers>
#include <utility>

namespace GUI {

namespace {

// Powers of two, so the transform halves at every stage; the upper bound keeps it well under a millisecond per trace
constexpr std::size_t MinSampleCount = 1024;
constexpr std::size_t MaxSampleCount = 65536;
// -200 dB next to the strongest bin, and 1e-15 (-300 dB) for a trace of zeros, as in the AC sweep
constexpr double DecibelFloor = 1e-10;
constexpr double MinAmplitude = 1e-15;

// Bins on each side of a peak that still belong to its tone: the main lobe of the Hann window spans two bins each
// way, while a rectangular window keeps a coherent tone in one bin and spreads an incoherent one too widely for a
// lobe to hold it
std::size_t LobeHalfWidth(const SpectrumWindow window) {
    return window == SpectrumWindow::Hann ? 2 : 1;
}

// Equivalent noise bandwidth of the window, in bins: the power of a tone summed over its lobe, against the square of
// its amplitude
double NoiseBandwidth(const SpectrumWindow window) {
    return window == SpectrumWindow::Hann ? 1.5 : 1.0;
}

// The periodic form, which leaves the sample after the span out, as the transform repeats the span
double WindowWeight(const SpectrumWindow window, const std::size_t index, const std::size_t count) {
    if (window == SpectrumWindow::Rectangular) {
        return 1.0;
    }
    return 0.5 * (1.0 - std::cos(2.0 * std::numbers::pi * static_cast<double>(index) / static_cast<double>(count)));
}

// Iterative radix-2 Cooley-Tukey transform, in place; the size must be a power of two
void Transform(std::vector<std::complex<double>> &values) {
    const std::size_t count = values.size();
    for (std::size_t index = 1, reversed = 0; index < count; ++index) {
        std::size_t bit = count >> 1;
        for (; (reversed & bit) != 0; bit >>= 1) {
            reversed ^= bit;
        }
        reversed ^= bit;
        if (index < reversed) {
            std::swap(values[index], values[reversed]);
        }
    }
    for (std::size_t length = 2; length <= count; length <<= 1) {
        const double angle = -2.0 * std::numbers::pi / static_cast<double>(length);
        const std::complex<double> step(std::cos(angle), std::sin(angle));
        for (std::size_t start = 0; start < count; start += length) {
            std::complex<double> twiddle(1.0, 0.0);
            for (std::size_t offset = 0; offset < length / 2; ++offset) {
                const std::complex<double> even = values[start + offset];
                const std::complex<double> odd = values[start + offset + length / 2] * twiddle;
                values[start + offset] = even + odd;
                values[start + offset + length / 2] = even - odd;
                twiddle *= step;
            }
        }
    }
}

// Power of the bins around center that belong to its tone, clipped to the spectrum
double LobePower(const std::vector<double> &amplitudes, const std::size_t center, const std::size_t half_width) {
    const std::size_t first = center > half_width ? center - half_width : 0;
    const std::size_t last = std::min(center + half_width, amplitudes.size() - 1);
    double power = 0.0;
    for (std::size_t bin = first; bin <= last; ++bin) {
        power += amplitudes[bin] * amplitudes[bin];
    }
    return power;
}

} // namespace

/**
 * @brief   Picks how many evenly spaced samples the spectrum of a span takes.
 * @param[in] xs    Times of the samples of the trace, rising; ngspice spaces them unevenly.
 * @param[in] from  One end of the span; the ends may come in any order.
 * @param[in] to    The other end.
 * @return  The power of two at or above the number of samples in the span, from 1024 to 65536, so the even grid is
 *          at least as fine as the simulation and every trace of one result shares it.
 */
std::size_t ChooseSpectrumSampleCount(const std::span<const double> xs, double from, double to) {
    if (from > to) {
        std::swap(from, to);
    }
    const auto first = std::ranges::lower_bound(xs, from);
    const auto last = std::ranges::upper_bound(xs, to);
    const auto inside = static_cast<std::size_t>(std::max<std::ptrdiff_t>(last - first, 0));
    return std::clamp(std::bit_ceil(std::max<std::size_t>(inside, 1)), MinSampleCount, MaxSampleCount);
}

/**
 * @brief   Computes the spectrum of a trace between two times.
 * @param[in] xs            Times of the samples, rising.
 * @param[in] ys            Values of the samples.
 * @param[in] from          One end of the span; the ends may come in any order and are clipped to the trace.
 * @param[in] to            The other end.
 * @param[in] sample_count  Evenly spaced samples the trace is read at, a power of two of at least 2, such as
 *                          ChooseSpectrumSampleCount() gives.
 * @param[in] window        Window the samples are weighed by.
 * @return  Amplitudes from 0 Hz to half the sampling rate, spaced by one over the span, or no value when the span
 *          holds no length of the trace, the trace is undefined somewhere in it (as a math channel dividing by
 *          zero) or the sample count is not a power of two.
 * @note    The trace is read between its samples by straight lines, as the plots draw it, which leaves a floor of
 *          distortion that a smaller time step lowers.
 */
std::optional<Spectrum> ComputeSpectrum(const std::span<const double> xs, const std::span<const double> ys, double from,
                                        double to, const std::size_t sample_count, const SpectrumWindow window) {
    const std::size_t count = std::min(xs.size(), ys.size());
    if (count < 2 || sample_count < 2 || !std::has_single_bit(sample_count)) {
        return std::nullopt;
    }
    if (from > to) {
        std::swap(from, to);
    }
    from = std::max(from, xs.front());
    to = std::min(to, xs[count - 1]);
    if (!(from < to)) {
        return std::nullopt;
    }

    const double interval = (to - from) / static_cast<double>(sample_count);
    std::vector<std::complex<double>> values(sample_count);
    double weight_sum = 0.0;
    for (std::size_t index = 0; index < sample_count; ++index) {
        const double weight = WindowWeight(window, index, sample_count);
        const double time = from + static_cast<double>(index) * interval;
        const double value = InterpolateAt(xs.first(count), ys.first(count), time, false);
        if (!std::isfinite(value)) {
            return std::nullopt;
        }
        values[index] = weight * value;
        weight_sum += weight;
    }
    Transform(values);

    Spectrum spectrum;
    spectrum.Window = window;
    const std::size_t bin_count = sample_count / 2 + 1;
    const double resolution = 1.0 / (to - from);
    spectrum.Frequencies.resize(bin_count);
    spectrum.Amplitudes.resize(bin_count);
    for (std::size_t bin = 0; bin < bin_count; ++bin) {
        spectrum.Frequencies[bin] = static_cast<double>(bin) * resolution;
        // Every bin but the mean and the last one also stands for its negative frequency, which doubles it
        const bool single = bin == 0 || bin == bin_count - 1;
        spectrum.Amplitudes[bin] = std::abs(values[bin]) / weight_sum * (single ? 1.0 : 2.0);
    }
    spectrum.Amplitudes[0] = values[0].real() / weight_sum;
    return spectrum;
}

/**
 * @brief   Finds the fundamental of a spectrum, its harmonics and their total distortion.
 * @param[in] spectrum       Spectrum of a trace, as ComputeSpectrum() gives.
 * @param[in] highest_order  Last harmonic measured, such as 10; harmonics past half the sampling rate are left out.
 * @return  The mean, then the fundamental (the strongest tone above 0 Hz) and each harmonic up to the highest order;
 *          only the mean and the periods needed when the span holds too few periods of the fundamental (5 with the
 *          Hann window, 3 with the rectangular one).
 * @note    Each tone is measured as the power of the bins of its lobe, so it reads its amplitude even between two
 *          bins. The fundamental frequency is the center of the power of its lobe.
 */
HarmonicAnalysis AnalyzeHarmonics(const Spectrum &spectrum, const int highest_order) {
    HarmonicAnalysis analysis;
    const std::vector<double> &amplitudes = spectrum.Amplitudes;
    if (amplitudes.empty()) {
        return analysis;
    }
    analysis.Mean = amplitudes[0];
    const std::size_t half_width = LobeHalfWidth(spectrum.Window);
    // The lobe of the mean covers the first bins, so the fundamental is looked for past it
    const std::size_t first_bin = half_width + 1;
    if (amplitudes.size() <= first_bin + half_width) {
        return analysis;
    }
    const auto strongest = std::max_element(amplitudes.begin() + static_cast<std::ptrdiff_t>(first_bin),
                                            amplitudes.end() - static_cast<std::ptrdiff_t>(half_width));
    const auto peak_bin = static_cast<std::size_t>(strongest - amplitudes.begin());
    const double largest = std::max(std::abs(analysis.Mean), *strongest);
    if (*strongest <= 0.0 || IsNegligible(*strongest, largest)) {
        return analysis;
    }
    // Bin k holds k periods; the lobes of the mean, the fundamental and each harmonic stay apart past 2 half widths
    if (peak_bin <= 2 * half_width) {
        analysis.PeriodsNeeded = static_cast<int>(2 * half_width + 1);
        return analysis;
    }

    double power = 0.0;
    double weighted_bin = 0.0;
    for (std::size_t bin = peak_bin - half_width; bin <= peak_bin + half_width; ++bin) {
        const double bin_power = amplitudes[bin] * amplitudes[bin];
        power += bin_power;
        weighted_bin += static_cast<double>(bin) * bin_power;
    }
    const double fundamental_bin = weighted_bin / power;
    const double resolution = spectrum.Frequencies.size() > 1 ? spectrum.Frequencies[1] : 0.0;
    const double bandwidth = NoiseBandwidth(spectrum.Window);
    double distortion_power = 0.0;
    for (int order = 1; order <= highest_order; ++order) {
        const double center = fundamental_bin * static_cast<double>(order);
        const auto bin = static_cast<std::size_t>(std::lround(center));
        if (bin + half_width >= amplitudes.size()) {
            break;
        }
        const double amplitude = std::sqrt(LobePower(amplitudes, bin, half_width) / bandwidth);
        analysis.Harmonics.push_back({.Order = order, .Frequency = center * resolution, .Amplitude = amplitude});
        if (order > 1) {
            distortion_power += amplitude * amplitude;
        }
    }
    if (analysis.Harmonics.size() > 1) {
        analysis.TotalDistortion = std::sqrt(distortion_power) / analysis.Harmonics.front().Amplitude;
    }
    return analysis;
}

/**
 * @brief   Converts the amplitudes of a spectrum to decibels, as a spectrum analyzer plots them.
 * @param[in] amplitudes  Peak amplitudes, such as those of a Spectrum past its first bin.
 * @return  20 log10 of each amplitude, relative to 1 of its unit (dBV for volts), floored 200 dB below the largest,
 *          so the rounding residue of the transform, and a trace of zeros, plot at a finite level.
 */
std::vector<double> ToDecibels(const std::span<const double> amplitudes) {
    double largest = 0.0;
    for (const double amplitude : amplitudes) {
        largest = std::max(largest, std::abs(amplitude));
    }
    const double floor = std::max(largest * DecibelFloor, MinAmplitude);
    std::vector<double> decibels;
    decibels.reserve(amplitudes.size());
    for (const double amplitude : amplitudes) {
        decibels.push_back(20.0 * std::log10(std::max(std::abs(amplitude), floor)));
    }
    return decibels;
}

} // namespace GUI
