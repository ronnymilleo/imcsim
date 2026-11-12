/**
 * @file    spectrum_tab.cpp
 * @brief   The Spectrum tab of the Output window: the measured traces of the transient in frequency, and their
 *          harmonics.
 */

#include "windows/output_tabs/spectrum_tab.h"

#include "base/theme.h"
#include "base/value_field.h"
#include "implot.h"
#include "plots/plot_figures.h"
#include "spice/spice_value.h"
#include "windows/output_tabs/plot_widgets.h"
#include <algorithm>
#include <cmath>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace GUI {

namespace {

/// Width of the window choice, in font sizes.
constexpr float WINDOW_FIELD_WIDTH = 8.0f;
/// Width of the harmonics table of one trace, in font sizes, so the tables of several traces sit side by side.
constexpr float HARMONICS_TABLE_WIDTH = 14.0f;

/// The harmonics the spectrum measures, and how many the plot shows when it fits: a linear axis spans them, plus one
/// harmonic of room, from 0 Hz.
constexpr int HIGHEST_HARMONIC = 10;
/// Room around the densities a density axis fits to, as a factor.
constexpr double DENSITY_AXIS_MARGIN = 2.0;
constexpr float HARMONIC_MARKER_SIZE = 3.0f;
constexpr double MIN_SHOWN_DISTORTION_PERCENT = 0.001;
/// The first bins above 0 Hz, where the Hann window spreads the mean.
constexpr std::ptrdiff_t SPECTRUM_SKIRT_BINS = 2;

bool IsBroadband(const SpectrumTrace &trace) {
    return trace.Decibels && trace.Harmonics.IsBroadband;
}

bool HasBroadband(const TransientSpectra &spectra) {
    return std::ranges::any_of(spectra.Traces, IsBroadband);
}

/**
 * @brief   The density view draws only noise, since a tone has no density; the decibel view draws every spectrum.
 */
bool IsShown(const SpectrumTrace &trace, const bool is_density) {
    return trace.Decibels && (!is_density || IsBroadband(trace));
}

/**
 * @brief   Fits a density axis to the densities of the noise traces on it, a few decades at most, since a bin at zero
 *          would otherwise stretch a logarithmic axis down to nothing.
 * @return  False when no trace on the axis has a density to fit.
 */
bool SetNextDensityLimits(const TransientSpectra &spectra, const ImAxis axis) {
    std::optional<std::pair<double, double>> range;
    for (const SpectrumTrace &trace : spectra.Traces) {
        const std::optional<std::pair<double, double>> own =
            IsShown(trace, true) && trace.Axis == axis ? FindDensityRange(trace.Densities) : std::nullopt;
        if (own) {
            range = range ? std::pair{std::min(range->first, own->first), std::max(range->second, own->second)} : own;
        }
    }
    if (!range) {
        return false;
    }
    ImPlot::SetNextAxisLimits(axis, range->first / DENSITY_AXIS_MARGIN, range->second * DENSITY_AXIS_MARGIN,
                              ImPlotCond_Always);
    return true;
}

/**
 * @brief   True when every trace that has a spectrum is broadband, and at least one has.
 */
bool AreAllBroadband(const TransientSpectra &spectra) {
    const auto has_spectrum = [](const SpectrumTrace &trace) { return trace.Decibels.has_value(); };
    return std::ranges::any_of(spectra.Traces, has_spectrum) &&
           std::ranges::all_of(spectra.Traces, [&has_spectrum](const SpectrumTrace &trace) {
               return !has_spectrum(trace) || IsBroadband(trace);
           });
}

/**
 * @brief   Decibels relative to 1 of the unit of the trace, as spectrum analyzers write dBV.
 */
std::string GetDecibelUnit(const SpectrumTrace &trace) {
    return trace.Unit.empty() ? " dB" : std::format(" dB{}", trace.Unit);
}

/**
 * @brief   Distortion reads as a percentage with three significant digits, down to the floor the interpolation of the
 *          transient leaves; the decibels give the exact figure below it.
 */
std::string FormatDistortionPercent(const double ratio) {
    const double percent = ratio * 100.0;
    return percent < MIN_SHOWN_DISTORTION_PERCENT ? std::format("<{}%", MIN_SHOWN_DISTORTION_PERCENT)
                                                  : std::format("{:.3g}%", percent);
}

/**
 * @brief   Every trace of a spectrum shares the span and the window, so one warning covers them all when the span holds
 *          too few periods to tell the harmonics apart.
 */
void DrawPeriodsWarning(const TransientSpectra &spectra) {
    std::optional<int> periods_needed;
    for (const SpectrumTrace &trace : spectra.Traces) {
        if (trace.Harmonics.PeriodsNeeded) {
            periods_needed = trace.Harmonics.PeriodsNeeded;
            break;
        }
    }
    if (!periods_needed) {
        return;
    }
    const int periods = *periods_needed;
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(GetWarningTextColor(), "%s",
                       std::format("The stretch needs {} periods to tell the harmonics apart: widen it, or pick the "
                                   "Rectangular window over whole periods",
                                   periods)
                           .c_str());
    ImGui::PopTextWrapPos();
}

/**
 * @brief   The rows of a broadband trace: the density and the total noise over the band the plot shows, which has no
 *          fundamental or harmonics.
 */
void DrawNoise(const SpectrumTrace &trace, const std::vector<double> &frequencies, const PlotSpan &band) {
    const std::string density_unit = trace.Unit + "/√Hz";
    DrawStatistic("Tone", "none, noise");
    ImGui::SetItemTooltip("No tone stands out of the floor, so the trace is read as noise");
    const double from = band.To > band.From ? band.From : frequencies.front();
    const double to = band.To > band.From ? band.To : frequencies.back();
    const std::optional<NoiseBand> noise = MeasureNoiseBand(frequencies, trace.Densities, from, to);
    if (!noise) {
        DrawStatistic("Density", "-");
        return;
    }
    DrawStatistic("Density", FormatQuantity(noise->Density, density_unit.c_str()));
    ImGui::SetItemTooltip(
        "%s", std::format("Level of the density (the median of its plateau) from {}Hz to {}Hz, the part of the "
                          "spectrum the plot shows",
                          Core::FormatValue(from), Core::FormatValue(to))
                  .c_str());
    DrawStatistic("Noise", FormatQuantity(noise->TotalRms, trace.Unit.c_str()));
    ImGui::SetItemTooltip("Root mean square of the noise over the same band");
}

/**
 * @brief   The rows of one trace in its statistics table: its mean, its fundamental and THD, then each harmonic in dBc;
 *          the noise over the band for a broadband trace.
 */
void DrawHarmonics(const SpectrumTrace &trace, const std::vector<double> &frequencies, const PlotSpan &band) {
    const HarmonicAnalysis &analysis = trace.Harmonics;
    const char *unit = trace.Unit.c_str();
    DrawMeasuredValue("Mean", analysis.Mean, trace.LargestAmplitude, unit);
    if (analysis.IsBroadband) {
        DrawNoise(trace, frequencies, band);
        return;
    }
    if (analysis.Harmonics.empty()) {
        DrawStatistic("Freq", "-");
        return;
    }
    const Harmonic &fundamental = analysis.Harmonics.front();
    DrawStatistic("Freq", FormatQuantity(fundamental.Frequency, "Hz"));
    ImGui::SetItemTooltip("Frequency of the fundamental, the strongest tone");
    DrawStatistic("Amplitude", FormatQuantity(fundamental.Amplitude, unit));
    ImGui::SetItemTooltip("Peak amplitude of the fundamental");
    if (analysis.TotalDistortion) {
        DrawStatistic("THD", FormatDistortionPercent(*analysis.TotalDistortion));
        DrawStatistic("THD dB", FormatFixed(20.0 * std::log10(std::max(*analysis.TotalDistortion, 1e-15)), " dB"));
    } else {
        DrawStatistic("THD", "-");
    }
    for (std::size_t index = 1; index < analysis.Harmonics.size(); ++index) {
        const Harmonic &harmonic = analysis.Harmonics[index];
        const std::string name = std::format("H{}", harmonic.Order);
        const double relative = harmonic.Amplitude / fundamental.Amplitude;
        DrawStatistic(name.c_str(), FormatFixed(20.0 * std::log10(std::max(relative, 1e-15)), " dBc"));
        ImGui::SetItemTooltip("%s", std::format("{}Hz, {}{}", Core::FormatValue(harmonic.Frequency),
                                                Core::FormatValue(harmonic.Amplitude), unit)
                                        .c_str());
    }
}

/**
 * @brief   The frequency range a new spectrum opens on: the fundamental and its harmonics on a linear axis, so they
 *          spread across the plot, and every frequency on a logarithmic one.
 */
PlotSpan GetSpectrumFitRange(const TransientSpectra &spectra, const bool logarithmic) {
    const PlotSpan whole{.From = spectra.Frequencies.front(), .To = spectra.Frequencies.back()};
    if (logarithmic) {
        return whole;
    }
    for (const SpectrumTrace &trace : spectra.Traces) {
        if (!trace.Harmonics.Harmonics.empty()) {
            const double fundamental = trace.Harmonics.Harmonics.front().Frequency;
            return {.From = 0.0, .To = std::min(fundamental * (HIGHEST_HARMONIC + 1), whole.To)};
        }
    }
    // Without a measured fundamental, as in a stretch of too few periods, the strongest bin past the skirt of the
    // mean stands in for it
    for (const SpectrumTrace &trace : spectra.Traces) {
        if (trace.Decibels && !IsBroadband(trace) &&
            trace.Decibels->size() > static_cast<std::size_t>(SPECTRUM_SKIRT_BINS)) {
            const auto strongest =
                std::max_element(trace.Decibels->begin() + SPECTRUM_SKIRT_BINS, trace.Decibels->end());
            const double frequency = spectra.Frequencies[static_cast<std::size_t>(strongest - trace.Decibels->begin())];
            return {.From = 0.0, .To = std::min(frequency * (HIGHEST_HARMONIC + 1), whole.To)};
        }
    }
    return {.From = 0.0, .To = whole.To};
}

/**
 * @brief   Sets the axes of the decibel view: voltage on the left, currents and math channels on the others.
 */
void SetupDecibelAxes(const bool has_voltages, const bool has_currents, const bool has_math) {
    ImPlot::SetupAxes("Frequency", "Voltage (dBV)", ImPlotAxisFlags_None,
                      VoltageAxisFlags(has_voltages, has_currents || has_math));
    if (has_currents) {
        ImPlot::SetupAxis(ImAxis_Y2, "Current (dBA)", ImPlotAxisFlags_AuxDefault);
    }
    if (has_math) {
        ImPlot::SetupAxis(ImAxis_Y3, "Math (dB)", ImPlotAxisFlags_AuxDefault);
    }
}

/**
 * @brief   Sets the axes of the density view, all logarithmic since a floor spans decades.
 */
void SetupDensityAxes(const bool has_voltages, const bool has_currents, const bool has_math) {
    ImPlot::SetupAxes("Frequency", "Voltage density (V/√Hz)", ImPlotAxisFlags_None,
                      VoltageAxisFlags(has_voltages, has_currents || has_math));
    ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);
    if (has_currents) {
        ImPlot::SetupAxis(ImAxis_Y2, "Current density (A/√Hz)", ImPlotAxisFlags_AuxDefault);
        ImPlot::SetupAxisScale(ImAxis_Y2, ImPlotScale_Log10);
    }
    if (has_math) {
        ImPlot::SetupAxis(ImAxis_Y3, "Math density", ImPlotAxisFlags_AuxDefault);
        ImPlot::SetupAxisScale(ImAxis_Y3, ImPlotScale_Log10);
    }
}

/**
 * @brief   The markers of a trace's harmonics in decibels.
 */
void PlotHarmonicMarkers(const SpectrumTrace &trace) {
    std::vector<double> harmonic_frequencies;
    std::vector<double> harmonic_levels;
    for (const Harmonic &harmonic : trace.Harmonics.Harmonics) {
        harmonic_frequencies.push_back(harmonic.Frequency);
        harmonic_levels.push_back(20.0 * std::log10(std::max(harmonic.Amplitude, 1e-15)));
    }
    ImPlotSpec marker_spec;
    marker_spec.LineColor = ImGui::ColorConvertU32ToFloat4(trace.Color);
    marker_spec.Marker = ImPlotMarker_Circle;
    marker_spec.MarkerSize = HARMONIC_MARKER_SIZE;
    ImPlot::PlotScatter(std::format("##{} harmonics", trace.Label).c_str(), harmonic_frequencies.data(),
                        harmonic_levels.data(), static_cast<int>(harmonic_frequencies.size()), marker_spec);
}

/**
 * @brief   One spectrum trace per measured voltage or current and per math channel, in the order of the sources.
 * @details The math channels are appended to the sources, so both lists keep the same order.
 */
std::vector<SpectrumTrace> MakeSpectrumTraces(std::vector<PlottedTrace> &sources, const std::vector<MathTrace> &math) {
    std::vector<SpectrumTrace> traces;
    for (const PlottedTrace &source : sources) {
        const bool current = std::string_view(source.Unit) == "A";
        traces.push_back({.Label = source.Label,
                          .Unit = source.Unit,
                          .Axis = current ? ImAxis_Y2 : ImAxis_Y1,
                          .Color = source.Color,
                          .Dashed = current,
                          .Decibels = std::nullopt,
                          .LargestAmplitude = 0.0,
                          .Harmonics = {},
                          .Densities = {}});
    }
    for (const MathTrace &trace : math) {
        sources.push_back({trace.Label, &trace.Values, trace.Unit.c_str(), trace.Color});
        traces.push_back({.Label = trace.Label,
                          .Unit = trace.Unit,
                          .Axis = trace.Axis,
                          .Color = trace.Color,
                          .Dashed = false,
                          .Decibels = std::nullopt,
                          .LargestAmplitude = 0.0,
                          .Harmonics = {},
                          .Densities = {}});
    }
    return traces;
}

/**
 * @brief   Fills a trace from its spectrum: decibels, largest amplitude, harmonics and noise densities.
 */
void AnalyzeTrace(const Spectrum &spectrum, SpectrumTrace &trace) {
    // The mean has no place on a logarithmic axis; the statistics show it instead
    const std::span<const double> above_zero = std::span(spectrum.Amplitudes).subspan(1);
    trace.Decibels = ToDecibels(above_zero);
    for (const double amplitude : spectrum.Amplitudes) {
        trace.LargestAmplitude = std::max(trace.LargestAmplitude, std::abs(amplitude));
    }
    trace.Harmonics = AnalyzeHarmonics(spectrum, HIGHEST_HARMONIC);
    trace.Densities = ComputeNoiseDensities(spectrum);
}

void FitSpectrumAxes(const TransientSpectra &spectra, const bool is_density, const bool logarithmic) {
    const PlotSpan range = GetSpectrumFitRange(spectra, logarithmic);
    ImPlot::SetNextAxisLimits(ImAxis_X1, range.From, range.To, ImPlotCond_Always);
    for (const ImAxis axis : {ImAxis_Y1, ImAxis_Y2, ImAxis_Y3}) {
        if (!is_density || !SetNextDensityLimits(spectra, axis)) {
            ImPlot::SetNextAxisToFit(axis);
        }
    }
}

void SetupSpectrumAxes(const TransientSpectra &spectra, const bool is_density, const bool logarithmic) {
    const auto uses_axis = [&spectra, is_density](const ImAxis axis) {
        return std::ranges::any_of(spectra.Traces, [axis, is_density](const SpectrumTrace &trace) {
            return IsShown(trace, is_density) && trace.Axis == axis;
        });
    };
    if (is_density) {
        SetupDensityAxes(uses_axis(ImAxis_Y1), uses_axis(ImAxis_Y2), uses_axis(ImAxis_Y3));
    } else {
        SetupDecibelAxes(uses_axis(ImAxis_Y1), uses_axis(ImAxis_Y2), uses_axis(ImAxis_Y3));
    }
    ImPlot::SetupAxisFormat(ImAxis_X1, FormatAxisValue, const_cast<char *>("Hz"));
    if (logarithmic) {
        ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
    }
}

void PlotSpectrumLine(const SpectrumTrace &trace, const std::vector<double> &frequencies,
                      const std::vector<double> &values) {
    if (trace.Dashed) {
        PlotDashedLine(trace.Label, frequencies, values, trace.Color);
        return;
    }
    ImPlotSpec spec;
    spec.LineColor = ImGui::ColorConvertU32ToFloat4(trace.Color);
    ImPlot::PlotLine(trace.Label.c_str(), frequencies.data(), values.data(), static_cast<int>(frequencies.size()),
                     spec);
}

} // namespace

/**
 * @brief   Creates the tab.
 * @param[in] schematic  Schematic that keeps the measurements and the names of nets; it must outlive the tab.
 * @param[in] results    Results whose transient is shown; they must outlive the tab.
 */
SpectrumTab::SpectrumTab(Schematic &schematic, const ResultStore &results)
    : m_Schematic(schematic), m_Results(results) {
}

/**
 * @brief   Draws the tab: the spectrum of every measured trace, and their harmonics on the right.
 * @param[in] frame  The math channels over the whole run, and whether any trace is measured.
 * @param[in] span   The stretch of the transient to transform, the one its statistics measure.
 */
void SpectrumTab::Draw(const FrameResults &frame, const PlotSpan span) {
    const auto &transient = m_Results.GetTransient();
    if (!transient) {
        ImGui::TextDisabled("No transient for the current circuit; the spectrum transforms its measured traces");
        return;
    }
    // The spectrum goes over the whole run, so a run that is still growing has no math channels in it yet
    static const std::vector<MathTrace> no_math;
    static const std::vector<std::string> no_errors;
    const std::vector<MathTrace> &math = frame.WholeRunMath != nullptr ? frame.WholeRunMath->Traces : no_math;
    Update(*transient, math, span);
    DrawWithStatistics(
        [&] {
            if (!frame.AnythingMeasured && math.empty()) {
                ImGui::TextDisabled("%s", NOTHING_MEASURED);
            }
            DrawMathErrors(frame.WholeRunMath != nullptr ? frame.WholeRunMath->Errors : no_errors);
            DrawPlot();
        },
        [&] { DrawStatistics(); });
}

/**
 * @brief   Tells whether the tab has something to export.
 * @return  True with a transient and the spectrum of at least one trace.
 */
bool SpectrumTab::CanExport() const {
    return m_Results.GetTransient() && m_Spectra &&
           std::ranges::any_of(m_Spectra->Traces,
                               [](const SpectrumTrace &trace) { return trace.Decibels.has_value(); });
}

/**
 * @brief   The figure of the spectrum for the export, over the visible frequency range.
 * @return  The figure, or an empty one before any spectrum.
 */
ExportFigure SpectrumTab::BuildFigure() const {
    if (!m_Spectra) {
        return {};
    }
    return BuildSpectrumFigure(*m_Spectra, m_IsLogarithmic, IsDensityShown(), m_View);
}

/**
 * @brief   Transforms the measured traces again only when the result, the span, the window or the traces changed.
 * @details Every trace shares one even grid, so the spectra share their frequencies.
 */
void SpectrumTab::Update(const Core::Transient &transient, const std::vector<MathTrace> &math, const PlotSpan span) {
    std::vector<PlottedTrace> sources = ListTraces(transient.NodeVoltages, transient.Currents, m_Schematic, "V", "A");
    std::vector<SpectrumTrace> traces = MakeSpectrumTraces(sources, math);
    std::vector<std::string> labels;
    for (const SpectrumTrace &trace : traces) {
        labels.push_back(trace.Label + trace.Unit);
    }
    const std::size_t version = m_Results.GetTransientDataVersion();
    if (m_Spectra && m_Spectra->TransientVersion == version && m_Spectra->Span.From == span.From &&
        m_Spectra->Span.To == span.To && m_Spectra->Window == m_Window && m_Spectra->Labels == labels) {
        return;
    }

    TransientSpectra spectra{.TransientVersion = version,
                             .Span = span,
                             .Window = m_Window,
                             .Labels = std::move(labels),
                             .Frequencies = {},
                             .Traces = {}};
    const std::size_t sample_count = ChooseSpectrumSampleCount(transient.Times, span.From, span.To);
    for (std::size_t index = 0; index < traces.size(); ++index) {
        SpectrumTrace &trace = traces[index];
        const std::optional<Spectrum> spectrum =
            ComputeSpectrum(transient.Times, *sources[index].Values, span.From, span.To, sample_count, m_Window);
        if (!spectrum) {
            continue;
        }
        AnalyzeTrace(*spectrum, trace);
        if (spectra.Frequencies.empty()) {
            spectra.Frequencies.assign(spectrum->Frequencies.begin() + 1, spectrum->Frequencies.end());
        }
    }
    spectra.Traces = std::move(traces);
    // Noise reads best as a density, so the view opens on it when the traces turn into noise
    const bool are_all_broadband = AreAllBroadband(spectra);
    if (are_all_broadband && !m_WereAllBroadband) {
        m_IsDensity = true;
        m_IsLogarithmic = true;
    }
    m_WereAllBroadband = are_all_broadband;
    m_Spectra = std::move(spectra);
    m_HasChanged = true;
}

/**
 * @brief   Voltages, currents and math channels keep the axes they have in the transient, in decibels or, as noise, in
 *          densities; each trace marks its harmonics in decibels.
 * @details A new spectrum fits its axes, and a linear one opens on the harmonics of the first trace that has some.
 */
void SpectrumTab::DrawPlot() {
    if (!m_Spectra) {
        return;
    }
    const TransientSpectra &spectra = *m_Spectra;
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", std::format("From {}s to {}s of the transient: zoom its plot or place its cursors to "
                                          "pick the stretch, such as the steady state",
                                          Core::FormatValue(spectra.Span.From), Core::FormatValue(spectra.Span.To))
                                  .c_str());
    ImGui::PopTextWrapPos();
    if (spectra.Frequencies.empty()) {
        return;
    }
    const bool is_density = IsDensityShown();
    if (std::exchange(m_HasChanged, false)) {
        FitSpectrumAxes(spectra, is_density, m_IsLogarithmic);
    }
    if (!BeginTracePlot("##spectrum", ImVec2(-1.0f, PlotHeight(1)))) {
        return;
    }
    SetupSpectrumAxes(spectra, is_density, m_IsLogarithmic);
    std::vector<PlottedTrace> plotted;
    std::vector<std::string> units;
    units.reserve(spectra.Traces.size());
    for (const SpectrumTrace &trace : spectra.Traces) {
        if (!IsShown(trace, is_density) || !trace.Decibels) {
            continue;
        }
        const std::vector<double> &values = is_density ? trace.Densities : *trace.Decibels;
        ImPlot::SetAxes(ImAxis_X1, trace.Axis);
        PlotSpectrumLine(trace, spectra.Frequencies, values);
        if (!is_density) {
            PlotHarmonicMarkers(trace);
        }
        units.push_back(is_density ? trace.Unit + "/√Hz" : GetDecibelUnit(trace));
        plotted.push_back({trace.Label, &values, units.back().c_str(), trace.Color});
    }
    ImPlot::SetAxes(ImAxis_X1, ImAxis_Y1);
    DrawCursorReadout(spectra.Frequencies, "Hz", m_IsLogarithmic, plotted);
    const ImPlotRange view = ImPlot::GetPlotLimits().X;
    m_View = {.From = view.Min, .To = view.Max};
    ImPlot::EndPlot();
}

/**
 * @brief   Like the harmonic readout of a spectrum analyzer: the mean, the fundamental and the THD of every shown
 *          trace, then each harmonic relative to the fundamental, in dBc.
 */
void SpectrumTab::DrawStatistics() {
    DrawControls();
    if (!m_Spectra) {
        return;
    }
    const TransientSpectra &spectra = *m_Spectra;
    if (spectra.Frequencies.size() > 1) {
        ImGui::SameLine(0.0f, ImGui::GetFontSize());
        ImGui::TextDisabled("%s",
                            std::format("Resolution {}Hz", Core::FormatValue(spectra.Frequencies.front())).c_str());
    }
    if (spectra.Traces.empty()) {
        ImGui::TextDisabled("Measure a trace to see its harmonics");
        return;
    }
    DrawPeriodsWarning(spectra);
    // Each trace has its own harmonics, so their tables sit side by side rather than share the row names
    const float table_width = ImGui::GetFontSize() * HARMONICS_TABLE_WIDTH;
    for (std::size_t index = 0; index < spectra.Traces.size(); ++index) {
        const SpectrumTrace &trace = spectra.Traces[index];
        if (index > 0) {
            ImGui::SameLine(0.0f, ImGui::GetFontSize());
        }
        ImGui::BeginGroup();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(trace.Color), "%s", trace.Label.c_str());
        if (!trace.Decibels) {
            ImGui::TextDisabled("Undefined in the stretch");
        } else if (ImGui::BeginTable(trace.Label.c_str(), 2, ImGuiTableFlags_SizingStretchProp, {table_width, 0.0f})) {
            DrawHarmonics(trace, spectra.Frequencies, m_View);
            ImGui::EndTable();
        }
        ImGui::EndGroup();
    }
}

/**
 * @brief   The window of the transform and the scale of the frequency axis, which refits the plot when it changes.
 */
void SpectrumTab::DrawControls() {
    DrawFieldLabel("Window");
    const char *window_name = m_Window == SpectrumWindow::HANN ? "Hann" : "Rectangular";
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * WINDOW_FIELD_WIDTH);
    if (ImGui::BeginCombo("##window", window_name)) {
        if (ImGui::Selectable("Hann", m_Window == SpectrumWindow::HANN)) {
            m_Window = SpectrumWindow::HANN;
        }
        ImGui::SetItemTooltip("Suits any stretch, with wider peaks");
        if (ImGui::Selectable("Rectangular", m_Window == SpectrumWindow::RECTANGULAR)) {
            m_Window = SpectrumWindow::RECTANGULAR;
        }
        ImGui::SetItemTooltip("Sharpest peaks, but only for a whole number of periods, such as between cursors one "
                              "period apart");
        ImGui::EndCombo();
    }
    ImGui::SameLine(0.0f, ImGui::GetFontSize());
    if (ImGui::Checkbox("Logarithmic frequency", &m_IsLogarithmic)) {
        m_HasChanged = true;
    }
    if (m_Spectra && HasBroadband(*m_Spectra)) {
        ImGui::SameLine(0.0f, ImGui::GetFontSize());
        if (ImGui::Checkbox("Noise density", &m_IsDensity)) {
            m_HasChanged = true;
        }
        ImGui::SetItemTooltip("Plots the density of the noise in V/√Hz (A/√Hz) on logarithmic axes, which does not "
                              "depend on the stretch or the window, instead of the level of each bin in dB");
    }
}

/**
 * @brief   Tells whether the plot shows densities: the view is chosen and some trace is noise.
 */
bool SpectrumTab::IsDensityShown() const {
    return m_IsDensity && m_Spectra && HasBroadband(*m_Spectra);
}

} // namespace GUI
