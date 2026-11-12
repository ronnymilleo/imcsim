/**
 * @file    harmonic_balance_view.h
 * @brief   Derived views of a harmonic balance result: scaled spectra, tiled waveforms, harmonic tables and CSV/figure
 *          exports.
 */

#ifndef IMCSIM_HARMONIC_BALANCE_VIEW_H
#define IMCSIM_HARMONIC_BALANCE_VIEW_H

#include "plots/plot_export.h"
#include "schematic/preferences.h"
#include "schematic/schematic.h"
#include "simulation/simulation_results.h"
#include <complex>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace GUI {

/// Floor used when converting near-zero harmonic magnitudes to decibels.
inline constexpr double HARMONIC_DECIBEL_FLOOR = -160.0;
/// Maximum number of fundamental periods shown in the reconstructed waveform plot.
inline constexpr int MAX_HARMONIC_WAVEFORM_CYCLES = 3;

/**
 * @enum    HarmonicScale
 * @brief   Vertical scale of the harmonic spectrum plot.
 */
enum class HarmonicScale {
    LINEAR,
    DECIBEL,
    DBC
};

/**
 * @struct  HarmonicTableRow
 * @brief   One harmonic entry of a signal's spectrum, with magnitude in linear units, dB, dBc and phase in degrees.
 */
struct HarmonicTableRow {
    std::size_t Order = 0;
    double FrequencyHertz = 0.0;
    /// Peak or RMS magnitude for harmonics >= 1 (by preference), or absolute DC level for order 0.
    double MagnitudeLinear = 0.0;
    double MagnitudeDecibels = HARMONIC_DECIBEL_FLOOR;
    double MagnitudeDbc = HARMONIC_DECIBEL_FLOOR;
    double PhaseDegrees = 0.0;
};

/**
 * @struct  HarmonicTraceSummary
 * @brief   Summary and full harmonic table of one measured voltage or current.
 */
struct HarmonicTraceSummary {
    std::string Label;
    std::string Unit;
    bool IsCurrent = false;
    /// Signed DC component X_0.
    double DcValue = 0.0;
    /// Total harmonic distortion in percent across harmonics >= 2 relative to the fundamental.
    double ThdPercent = 0.0;
    std::vector<HarmonicTableRow> Rows;
};

/**
 * @struct  HarmonicWaveformTrace
 * @brief   One tiled periodic waveform trace prepared for plotting or figure export.
 */
struct HarmonicWaveformTrace {
    std::string Label;
    bool IsCurrent = false;
    /// Node index for voltages, or index into HarmonicBalance::CurrentWaveforms for currents (preserves color order).
    std::size_t SourceIndex = 0;
    std::vector<double> Values;
};

/**
 * @struct  HarmonicWaveformSet
 * @brief   Shared periodic time axis and all measured voltage and current waveform traces.
 */
struct HarmonicWaveformSet {
    std::vector<double> Times;
    std::vector<HarmonicWaveformTrace> Traces;
};

// Spectrum conversions
double ScaleHarmonicMagnitude(std::complex<double> phasor, std::size_t order, HarmonicAmplitudeUnit unit);
double ToHarmonicDecibels(double scaled_magnitude);
double ToHarmonicDbc(std::complex<double> phasor, double fundamental_magnitude);
std::vector<double> ListSpectrumValues(std::span<const std::complex<double>> phasors, HarmonicAmplitudeUnit unit,
                                       HarmonicScale scale);

// Waveform tiling across periods
std::vector<double> TilePeriodicTimes(std::span<const double> times, double fundamental_frequency, int cycles);
std::vector<double> TilePeriodicValues(std::span<const double> values, int cycles);
HarmonicWaveformSet BuildHarmonicWaveforms(const Core::HarmonicBalance &result, Schematic &schematic, int cycles);

// Summaries and exports
HarmonicTraceSummary BuildHarmonicTraceSummary(std::string label, std::string unit, bool is_current,
                                               std::span<const std::complex<double>> phasors,
                                               std::span<const double> frequencies, HarmonicAmplitudeUnit unit_kind);
std::vector<HarmonicTraceSummary> BuildHarmonicSummaries(const Core::HarmonicBalance &result, Schematic &schematic,
                                                         HarmonicAmplitudeUnit unit_kind);
std::string BuildHarmonicBalanceCsv(const Core::HarmonicBalance &result, Schematic &schematic,
                                    HarmonicAmplitudeUnit unit_kind);
ExportFigure BuildHarmonicBalanceFigure(const Core::HarmonicBalance &result, Schematic &schematic, int cycles,
                                        const std::optional<ExportRange> &view);

} // namespace GUI

#endif // IMCSIM_HARMONIC_BALANCE_VIEW_H
