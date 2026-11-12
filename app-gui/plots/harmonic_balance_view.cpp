/**
 * @file    harmonic_balance_view.cpp
 * @brief   Derived views of a harmonic balance result: scaled spectra, tiled waveforms, harmonic tables and CSV/figure
 *          exports.
 */

#include "plots/harmonic_balance_view.h"

#include "base/node_colors.h"
#include "imgui.h"
#include "implot.h"
#include "schematic/probing.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <numbers>
#include <sstream>
#include <utility>

namespace GUI {

namespace {

/// Floor amplitude matching HARMONIC_DECIBEL_FLOOR (10^(-160/20)).
constexpr double HARMONIC_MAGNITUDE_FLOOR = 1e-8;
/// Magnitude below which phase is solver noise and displays as 0 degrees.
constexpr double PHASE_NOISE_FLOOR = 1e-12;

int ClampCycles(const int cycles) {
    return std::clamp(cycles, 1, MAX_HARMONIC_WAVEFORM_CYCLES);
}

ExportColor ToExportColor(const ImU32 color) {
    return {static_cast<std::uint8_t>((color >> IM_COL32_R_SHIFT) & 0xFF),
            static_cast<std::uint8_t>((color >> IM_COL32_G_SHIFT) & 0xFF),
            static_cast<std::uint8_t>((color >> IM_COL32_B_SHIFT) & 0xFF)};
}

/// Fallback current colors when building figures headless without an active ImPlot context.
constexpr auto FALLBACK_CURRENT_COLORS = std::to_array<ExportColor>({
    {31, 119, 180},
    {255, 127, 14},
    {44, 160, 44},
    {214, 39, 40},
    {148, 103, 189},
    {140, 86, 75},
});

ExportColor ResolveCurrentExportColor(const std::size_t index) {
    if (ImPlot::GetCurrentContext() != nullptr) {
        return ToExportColor(GetCurrentColor(index));
    }
    return FALLBACK_CURRENT_COLORS[index % FALLBACK_CURRENT_COLORS.size()];
}

double ComputePhaseDegrees(const std::complex<double> phasor) {
    if (std::abs(phasor) <= PHASE_NOISE_FLOOR) {
        return 0.0;
    }
    const double degrees = std::arg(phasor) * (180.0 / std::numbers::pi);
    return std::abs(degrees) < PHASE_NOISE_FLOOR ? 0.0 : degrees;
}

std::string EscapeCsvField(const std::string_view text) {
    if (text.find_first_of(",\"\r\n") == std::string_view::npos) {
        return std::string(text);
    }
    std::string escaped = "\"";
    for (const char ch : text) {
        if (ch == '"') {
            escaped += "\"\"";
        } else {
            escaped.push_back(ch);
        }
    }
    escaped.push_back('"');
    return escaped;
}

} // namespace

/**
 * @brief   Converts one complex phasor to its displayed scalar magnitude.
 * @param[in] phasor  Peak complex phasor.
 * @param[in] order   Harmonic index (0 for DC).
 * @param[in] unit    Display convention: Peak leaves harmonics unscaled; RMS divides AC harmonics by sqrt(2).
 * @return  Scalar non-negative magnitude.
 */
double ScaleHarmonicMagnitude(const std::complex<double> phasor, const std::size_t order,
                              const HarmonicAmplitudeUnit unit) {
    const double magnitude = std::abs(phasor);
    if (order == 0 || unit == HarmonicAmplitudeUnit::PEAK) {
        return magnitude;
    }
    return magnitude / std::numbers::sqrt2;
}

/**
 * @brief   Converts a scaled linear magnitude to decibels (dBV or dBA), floored at HARMONIC_DECIBEL_FLOOR.
 * @param[in] scaled_magnitude  Linear magnitude.
 * @return  20 * log10(scaled_magnitude), clamped below at HARMONIC_DECIBEL_FLOOR.
 */
double ToHarmonicDecibels(const double scaled_magnitude) {
    const double safe = std::max(scaled_magnitude, HARMONIC_MAGNITUDE_FLOOR);
    return std::max(HARMONIC_DECIBEL_FLOOR, 20.0 * std::log10(safe));
}

/**
 * @brief   Converts one phasor to decibels relative to the fundamental (dBc).
 * @param[in] phasor                 Complex harmonic phasor.
 * @param[in] fundamental_magnitude  Unscaled magnitude |X_1| of the fundamental.
 * @return  20 * log10(|phasor| / |X_1|), or HARMONIC_DECIBEL_FLOOR when the fundamental is zero.
 */
double ToHarmonicDbc(const std::complex<double> phasor, const double fundamental_magnitude) {
    if (fundamental_magnitude <= 0.0) {
        return HARMONIC_DECIBEL_FLOOR;
    }
    const double ratio = std::max(std::abs(phasor) / fundamental_magnitude, HARMONIC_MAGNITUDE_FLOOR);
    return std::max(HARMONIC_DECIBEL_FLOOR, 20.0 * std::log10(ratio));
}

/**
 * @brief   Extracts the spectrum values of one trace in the chosen amplitude convention and scale.
 * @param[in] phasors  Complex phasors indexed from DC (0) to H.
 * @param[in] unit     Peak or RMS convention for AC harmonics.
 * @param[in] scale    Linear, decibels (dBV/dBA) or relative decibels (dBc).
 * @return  One scalar value per harmonic bin.
 */
std::vector<double> ListSpectrumValues(const std::span<const std::complex<double>> phasors,
                                       const HarmonicAmplitudeUnit unit, const HarmonicScale scale) {
    std::vector<double> values;
    values.reserve(phasors.size());
    const double fundamental = phasors.size() > 1 ? std::abs(phasors[1]) : 0.0;
    for (std::size_t order = 0; order < phasors.size(); ++order) {
        switch (scale) {
        case HarmonicScale::LINEAR:
            values.push_back(ScaleHarmonicMagnitude(phasors[order], order, unit));
            break;
        case HarmonicScale::DECIBEL:
            values.push_back(ToHarmonicDecibels(ScaleHarmonicMagnitude(phasors[order], order, unit)));
            break;
        case HarmonicScale::DBC:
            values.push_back(ToHarmonicDbc(phasors[order], fundamental));
            break;
        default:
            values.push_back(0.0);
            break;
        }
    }
    return values;
}

/**
 * @brief   Repeats the one-period time grid across the requested number of cycles, closing at cycle * T0.
 * @param[in] times                  Time instants across [0, T0).
 * @param[in] fundamental_frequency  Fundamental frequency in Hz.
 * @param[in] cycles                 Number of periods to show (clamped to 1..MAX_HARMONIC_WAVEFORM_CYCLES).
 * @return  Monotonic time vector ending at cycles / fundamental_frequency.
 */
std::vector<double> TilePeriodicTimes(const std::span<const double> times, const double fundamental_frequency,
                                      const int cycles) {
    if (times.empty() || !std::isfinite(fundamental_frequency) || fundamental_frequency <= 0.0) {
        return {};
    }
    const int clamped_cycles = ClampCycles(cycles);
    const double period = 1.0 / fundamental_frequency;
    std::vector<double> tiled;
    tiled.reserve(times.size() * static_cast<std::size_t>(clamped_cycles) + 1);
    for (int cycle = 0; cycle < clamped_cycles; ++cycle) {
        const double offset = static_cast<double>(cycle) * period;
        for (const double time : times) {
            tiled.push_back(time + offset);
        }
    }
    tiled.push_back(static_cast<double>(clamped_cycles) * period);
    return tiled;
}

/**
 * @brief   Repeats one period of waveform samples to match TilePeriodicTimes().
 * @param[in] values  Samples across [0, T0).
 * @param[in] cycles  Number of periods to repeat (clamped to 1..MAX_HARMONIC_WAVEFORM_CYCLES).
 * @return  Repeated sample values, plus the periodic endpoint values[0].
 */
std::vector<double> TilePeriodicValues(const std::span<const double> values, const int cycles) {
    if (values.empty()) {
        return {};
    }
    const int clamped_cycles = ClampCycles(cycles);
    std::vector<double> tiled;
    tiled.reserve(values.size() * static_cast<std::size_t>(clamped_cycles) + 1);
    for (int cycle = 0; cycle < clamped_cycles; ++cycle) {
        tiled.insert(tiled.end(), values.begin(), values.end());
    }
    tiled.push_back(values.front());
    return tiled;
}

/**
 * @brief   Tiles the time axis and all measured voltage and current waveforms whose length matches it.
 * @param[in] result     Harmonic balance simulation result.
 * @param[in] schematic  Schematic naming which traces are measured and holding net labels.
 * @param[in] cycles     Number of fundamental periods to show (clamped to 1..MAX_HARMONIC_WAVEFORM_CYCLES).
 * @return  Tiled times and all complete measured traces, voltages first then currents.
 */
HarmonicWaveformSet BuildHarmonicWaveforms(const Core::HarmonicBalance &result, Schematic &schematic,
                                           const int cycles) {
    HarmonicWaveformSet waveforms;
    waveforms.Times = TilePeriodicTimes(result.Times, result.FundamentalFrequency, cycles);
    if (waveforms.Times.empty()) {
        return waveforms;
    }

    for (std::size_t node = 1; node < result.NodeWaveforms.size(); ++node) {
        if (!schematic.IsVoltageMeasured(static_cast<int>(node))) {
            continue;
        }
        std::vector<double> values = TilePeriodicValues(result.NodeWaveforms[node], cycles);
        if (values.size() != waveforms.Times.size()) {
            continue;
        }
        waveforms.Traces.push_back({.Label = GetVoltageLabel(schematic.GetConnectivity(), static_cast<int>(node)),
                                    .IsCurrent = false,
                                    .SourceIndex = node,
                                    .Values = std::move(values)});
    }
    for (std::size_t index = 0; index < result.CurrentWaveforms.size(); ++index) {
        const Core::ComponentTrace &current = result.CurrentWaveforms[index];
        if (!schematic.IsCurrentMeasured(current.Name)) {
            continue;
        }
        std::vector<double> values = TilePeriodicValues(current.Values, cycles);
        if (values.size() != waveforms.Times.size()) {
            continue;
        }
        waveforms.Traces.push_back({.Label = std::format("I({})", current.Name),
                                    .IsCurrent = true,
                                    .SourceIndex = index,
                                    .Values = std::move(values)});
    }
    return waveforms;
}

/**
 * @brief   Builds the table rows, signed DC level and THD% of a single trace.
 * @param[in] label        Trace label such as "V(OUT)" or "I(D1)".
 * @param[in] unit         Base unit ("V" or "A").
 * @param[in] is_current   True for component currents, false for node voltages.
 * @param[in] phasors      Harmonic phasors indexed from DC (0).
 * @param[in] frequencies  Frequency in Hz for each harmonic index.
 * @param[in] unit_kind    Peak or RMS display convention.
 * @return  Summary metrics and per-harmonic table rows.
 */
HarmonicTraceSummary BuildHarmonicTraceSummary(std::string label, std::string unit, const bool is_current,
                                               const std::span<const std::complex<double>> phasors,
                                               const std::span<const double> frequencies,
                                               const HarmonicAmplitudeUnit unit_kind) {
    HarmonicTraceSummary summary;
    summary.Label = std::move(label);
    summary.Unit = std::move(unit);
    summary.IsCurrent = is_current;
    summary.DcValue = phasors.empty() ? 0.0 : phasors[0].real();
    summary.ThdPercent = Core::ComputeHarmonicBalanceThd(phasors) * 100.0;
    const std::size_t count = std::min(phasors.size(), frequencies.size());
    const double fundamental = phasors.size() > 1 ? std::abs(phasors[1]) : 0.0;
    summary.Rows.reserve(count);
    for (std::size_t order = 0; order < count; ++order) {
        const double scaled = ScaleHarmonicMagnitude(phasors[order], order, unit_kind);
        summary.Rows.push_back({.Order = order,
                                .FrequencyHertz = frequencies[order],
                                .MagnitudeLinear = scaled,
                                .MagnitudeDecibels = ToHarmonicDecibels(scaled),
                                .MagnitudeDbc = ToHarmonicDbc(phasors[order], fundamental),
                                .PhaseDegrees = ComputePhaseDegrees(phasors[order])});
    }
    return summary;
}

/**
 * @brief   Collects harmonic tables and THD summaries for all measured nodes and currents in the schematic.
 * @param[in] result     Harmonic balance simulation result.
 * @param[in] schematic  Schematic naming which traces are measured and holding net labels.
 * @param[in] unit_kind  Peak or RMS display convention.
 * @return  One summary per measured voltage (first) and measured current.
 */
std::vector<HarmonicTraceSummary> BuildHarmonicSummaries(const Core::HarmonicBalance &result, Schematic &schematic,
                                                         const HarmonicAmplitudeUnit unit_kind) {
    std::vector<HarmonicTraceSummary> summaries;
    for (std::size_t node = 1; node < result.NodePhasors.size(); ++node) {
        if (!schematic.IsVoltageMeasured(static_cast<int>(node))) {
            continue;
        }
        summaries.push_back(
            BuildHarmonicTraceSummary(GetVoltageLabel(schematic.GetConnectivity(), static_cast<int>(node)), "V", false,
                                      result.NodePhasors[node], result.Frequencies, unit_kind));
    }
    for (const Core::ComponentPhasors &current : result.CurrentPhasors) {
        if (!schematic.IsCurrentMeasured(current.Name)) {
            continue;
        }
        summaries.push_back(BuildHarmonicTraceSummary(std::format("I({})", current.Name), "A", true, current.Phasors,
                                                      result.Frequencies, unit_kind));
    }
    return summaries;
}

/**
 * @brief   Formats measured spectra, DC/THD summaries and one-cycle waveforms as a CSV document.
 * @param[in] result     Harmonic balance simulation result.
 * @param[in] schematic  Schematic naming which traces are measured and holding net labels.
 * @param[in] unit_kind  Peak or RMS display convention.
 * @return  CSV text ready to write to a file.
 */
std::string BuildHarmonicBalanceCsv(const Core::HarmonicBalance &result, Schematic &schematic,
                                    const HarmonicAmplitudeUnit unit_kind) {
    const std::vector<HarmonicTraceSummary> summaries = BuildHarmonicSummaries(result, schematic, unit_kind);
    const char *amp_name = GetHarmonicAmplitudeUnitName(unit_kind);
    std::ostringstream out;
    out << std::format("# Fundamental Frequency (Hz),{:.9g},Harmonics,{},Samples,{},Strategy,{},Iterations,{}\n",
                       result.FundamentalFrequency, result.Frequencies.empty() ? 0 : result.Frequencies.size() - 1,
                       result.Times.size(), EscapeCsvField(result.Strategy), result.Iterations);
    out << "\nSignal,Unit,DC,THD (%)\n";
    for (const HarmonicTraceSummary &summary : summaries) {
        out << std::format("{},{},{:.9g},{:.6g}\n", EscapeCsvField(summary.Label), summary.Unit, summary.DcValue,
                           summary.ThdPercent);
    }
    out << std::format("\nSignal,Unit,Order,Frequency (Hz),Magnitude ({}),Magnitude (dB),Magnitude (dBc),Phase (deg)\n",
                       amp_name);
    for (const HarmonicTraceSummary &summary : summaries) {
        for (const HarmonicTableRow &row : summary.Rows) {
            out << std::format("{},{},{},{:.9g},{:.9g},{:.6g},{:.6g},{:.6g}\n", EscapeCsvField(summary.Label),
                               summary.Unit, row.Order, row.FrequencyHertz, row.MagnitudeLinear, row.MagnitudeDecibels,
                               row.MagnitudeDbc, row.PhaseDegrees);
        }
    }

    // One-cycle periodic waveform table for the measured signals
    struct WaveformColumn {
        std::string Label;
        const std::vector<double> *Values = nullptr;
    };
    std::vector<WaveformColumn> columns;
    for (std::size_t node = 1; node < result.NodeWaveforms.size(); ++node) {
        if (schematic.IsVoltageMeasured(static_cast<int>(node))) {
            columns.push_back(
                {GetVoltageLabel(schematic.GetConnectivity(), static_cast<int>(node)), &result.NodeWaveforms[node]});
        }
    }
    for (const Core::ComponentTrace &current : result.CurrentWaveforms) {
        if (schematic.IsCurrentMeasured(current.Name)) {
            columns.push_back({std::format("I({})", current.Name), &current.Values});
        }
    }
    if (!result.Times.empty() && !columns.empty()) {
        out << "\nTime (s)";
        for (const WaveformColumn &column : columns) {
            out << ',' << EscapeCsvField(column.Label);
        }
        out << '\n';
        for (std::size_t sample = 0; sample < result.Times.size(); ++sample) {
            out << std::format("{:.9g}", result.Times[sample]);
            for (const WaveformColumn &column : columns) {
                const double value = sample < column.Values->size() ? (*column.Values)[sample] : 0.0;
                out << std::format(",{:.9g}", value);
            }
            out << '\n';
        }
    }
    return out.str();
}

/**
 * @brief   Builds an exportable figure of the reconstructed periodic waveforms for measured voltages and currents.
 * @param[in] result     Harmonic balance simulation result.
 * @param[in] schematic  Schematic naming which traces are measured and holding net labels.
 * @param[in] cycles     Number of fundamental periods to show.
 * @param[in] view       Visible time range, or none for the full tiled window.
 * @return  ExportFigure holding measured voltage and current waveforms.
 */
ExportFigure BuildHarmonicBalanceFigure(const Core::HarmonicBalance &result, Schematic &schematic, const int cycles,
                                        const std::optional<ExportRange> &view) {
    HarmonicWaveformSet waveforms = BuildHarmonicWaveforms(result, schematic, cycles);
    ExportFigure figure;
    figure.XAxis = {.Name = "Time", .Unit = "s"};
    figure.Xs = std::move(waveforms.Times);
    figure.XRange = view;
    if (figure.Xs.empty()) {
        return figure;
    }

    ExportPanel panel{.Title = "Reconstructed waveforms"};
    bool has_voltages = false;
    bool has_currents = false;
    std::size_t current_axis_index = 0;
    for (HarmonicWaveformTrace &trace : waveforms.Traces) {
        if (!trace.IsCurrent) {
            if (!has_voltages) {
                panel.YAxes.push_back({.Name = "Voltage", .Unit = "V"});
                has_voltages = true;
                current_axis_index = 1;
            }
            panel.Series.push_back({.Name = trace.Label,
                                    .LegendLabel = trace.Label,
                                    .Values = std::move(trace.Values),
                                    .Axis = 0,
                                    .Color = ToExportColor(GetNodeColor(static_cast<int>(trace.SourceIndex)))});
        } else {
            if (!has_currents) {
                panel.YAxes.push_back({.Name = "Current", .Unit = "A"});
                has_currents = true;
            }
            panel.Series.push_back({.Name = trace.Label,
                                    .LegendLabel = trace.Label,
                                    .Values = std::move(trace.Values),
                                    .Axis = current_axis_index,
                                    .Color = ResolveCurrentExportColor(trace.SourceIndex),
                                    .Dashed = true});
        }
    }
    figure.Panels.push_back(std::move(panel));
    return figure;
}

} // namespace GUI
