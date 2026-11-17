/**
 * @file    signal_measures.h
 * @brief   The few figures the compact answers give for a waveform or a frequency response.
 */

#ifndef NODALYS_SIGNAL_MEASURES_H
#define NODALYS_SIGNAL_MEASURES_H

#include <optional>
#include <span>

namespace Nodalys {

/**
 * @struct  WaveformFigures
 * @brief   Extremes, final value and time-weighted mean and RMS of a waveform.
 */
struct WaveformFigures {
    double Minimum = 0.0;
    double Maximum = 0.0;
    double Final = 0.0;
    /// Mean over the run, weighting each sample by the time step around it, so a variable step does not skew it.
    double Mean = 0.0;
    double Rms = 0.0;
};

/**
 * @struct  ResponseFigures
 * @brief   What matters of a gain-and-phase response: the peak, the -3 dB points and the unity-gain crossing.
 * @details A figure that the sweep does not reach is none: a low-pass has no lower -3 dB point, and a response that
 *          never crosses 0 dB has no unity-gain frequency.
 */
struct ResponseFigures {
    double PeakGainDecibels = 0.0;
    double PeakFrequency = 0.0;
    /// Frequency below the peak where the gain first rises to 3 dB under it.
    std::optional<double> LowerCutoffFrequency;
    /// Frequency above the peak where the gain falls to 3 dB under it.
    std::optional<double> UpperCutoffFrequency;
    /// First frequency where the gain falls through 0 dB.
    std::optional<double> UnityGainFrequency;
    std::optional<double> PhaseAtUnityGainDegrees;
};

// Measuring
std::optional<WaveformFigures> MeasureWaveform(std::span<const double> times, std::span<const double> values);
std::optional<ResponseFigures> MeasureResponse(std::span<const double> frequencies,
                                               std::span<const double> gains_decibels,
                                               std::span<const double> phases_degrees);

} // namespace Nodalys

#endif // NODALYS_SIGNAL_MEASURES_H
