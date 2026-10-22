/**
 * @file    plot_figures.cpp
 * @brief   The export figures of the Output window plots, and the lists of traces the plots and the figures share.
 */

#include "plot_figures.h"

#include "node_colors.h"
#include "parameter_step.h"
#include "plot_helpers.h"
#include "probing.h"
#include "spice_value.h"
#include "theme.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <utility>

namespace GUI {

namespace {

// The share of the time an export shows that a cell needs for its bit label
constexpr double MIN_BIT_LABEL_SHARE = 0.03;
// Gain from the input source to the output below which the input-referred noise is left out: -120 dB
constexpr double MIN_INPUT_GAIN = 1e-6;

ExportColor ToExportColor(const ImU32 color) {
    return {static_cast<std::uint8_t>((color >> IM_COL32_R_SHIFT) & 0xFF),
            static_cast<std::uint8_t>((color >> IM_COL32_G_SHIFT) & 0xFF),
            static_cast<std::uint8_t>((color >> IM_COL32_B_SHIFT) & 0xFF)};
}

/**
 * @struct  ExportAxes
 * @brief   How the voltage, current and third axes of a plot are described in an exported figure.
 */
struct ExportAxes {
    ExportAxis Voltage;
    ExportAxis Current;
    ExportAxis Third;
};

ExportAxes GetLinearExportAxes(const std::string &third_unit) {
    return {.Voltage = {.Name = "Voltage", .Unit = "V"},
            .Current = {.Name = "Current", .Unit = "A"},
            .Third = {.Name = third_unit.empty() ? "Math" : third_unit, .Unit = third_unit}};
}

ExportAxes GetSpectrumExportAxes() {
    return {.Voltage = {.Name = "Voltage (dBV)", .Unit = "dBV", .UsesPrefixes = false},
            .Current = {.Name = "Current (dBA)", .Unit = "dBA", .UsesPrefixes = false},
            .Third = {.Name = "Math (dB)", .Unit = "dB", .UsesPrefixes = false}};
}

/**
 * @class   PanelBuilder
 * @brief   Fills an exported panel, adding each Y axis the first time a trace is plotted on it.
 * @details The axes keep the order in which traces first need them, so the first one, drawn on the left, is the
 *          voltage axis whenever a voltage is shown.
 */
class PanelBuilder {
public:
    PanelBuilder(std::string title, ExportAxes axes) : m_Axes(std::move(axes)) { m_Panel.Title = std::move(title); }

    std::size_t UseAxis(const ImAxis axis) {
        const auto found = std::ranges::find(m_UsedAxes, axis);
        if (found != m_UsedAxes.end()) {
            return static_cast<std::size_t>(found - m_UsedAxes.begin());
        }
        m_UsedAxes.push_back(axis);
        m_Panel.YAxes.push_back(axis == ImAxis_Y1 ? m_Axes.Voltage : axis == ImAxis_Y2 ? m_Axes.Current : m_Axes.Third);
        return m_UsedAxes.size() - 1;
    }

    void Add(ExportSeries series, const ImAxis axis) {
        series.Axis = UseAxis(axis);
        m_Panel.Series.push_back(std::move(series));
    }

    void AddNote(ExportNote note, const ImAxis axis) {
        note.Axis = UseAxis(axis);
        m_Panel.Notes.push_back(std::move(note));
    }

    ExportPanel Take() { return std::move(m_Panel); }

private:
    ExportPanel m_Panel;
    ExportAxes m_Axes;
    std::vector<ImAxis> m_UsedAxes;
};

// Adds the measured voltages and currents of a result, as the plots draw them. The suffix tells the curves of a
// DC sweep family apart in the CSV headers; only the first curve of a family has a legend entry
void AddExportTraces(PanelBuilder &builder, const std::vector<std::vector<double>> &node_values,
                     const std::vector<Core::ComponentTrace> &currents, Schematic &schematic, const std::string &suffix,
                     const bool in_legend, const std::vector<int> &excluded = {}) {
    for (std::size_t node = 1; node < node_values.size(); ++node) {
        if (!schematic.IsVoltageMeasured(static_cast<int>(node)) ||
            std::ranges::contains(excluded, static_cast<int>(node))) {
            continue;
        }
        const std::string label = GetVoltageLabel(schematic.GetConnectivity(), static_cast<int>(node));
        builder.Add({.Name = label + suffix,
                     .LegendLabel = in_legend ? label : "",
                     .Values = node_values[node],
                     .Color = ToExportColor(GetNodeColor(static_cast<int>(node)))},
                    ImAxis_Y1);
    }
    for (std::size_t index = 0; index < currents.size(); ++index) {
        if (!schematic.IsCurrentMeasured(currents[index].Name)) {
            continue;
        }
        const std::string label = std::format("I({})", currents[index].Name);
        builder.Add({.Name = label + suffix,
                     .LegendLabel = in_legend ? label : "",
                     .Values = currents[index].Values,
                     .Color = ToExportColor(GetCurrentColor(index)),
                     .Dashed = true},
                    ImAxis_Y2);
    }
}

// A note for each cell that is wide enough for its label in what the image shows, which has no pixels to measure it
// with, so a share of the span stands for the width of the box
void AddBitNotes(ExportPanel &panel, const Core::Transient &transient, const std::vector<LogicNode> &lanes,
                 const PlotSpan &view) {
    std::vector<LogicSignal> signals;
    for (const LogicNode &node : lanes) {
        signals.push_back({transient.NodeVoltages[static_cast<std::size_t>(node.Node)], node.LogicLevel});
    }
    const auto cells = FindBitCells(transient.Times, signals, view.From, view.To);
    for (std::size_t lane = 0; lane < lanes.size(); ++lane) {
        const double middle = static_cast<double>(lanes.size() - 1 - lane) * LANE_STRIDE + LANE_HIGH_OFFSET / 2.0;
        for (const LogicSegment &cell : cells[lane]) {
            const double from = std::max(cell.From, view.From);
            const double to = std::min(cell.To, view.To);
            if (to - from >= (view.To - view.From) * MIN_BIT_LABEL_SHARE) {
                panel.Notes.push_back(
                    {.X = (from + to) / 2.0, .Y = middle, .Text = GetLogicStateName(cell.State), .Centered = true});
            }
        }
    }
}

// The lanes as a panel of their own: each trace is drawn at the height of its lane, and its axis names the lanes,
// while the CSV lists the voltages. The first lane is on top, as in the plot
ExportPanel BuildLanePanel(const Core::Transient &transient, const std::vector<LogicNode> &lanes, Schematic &schematic,
                           const bool bit_values, const PlotSpan &view) {
    ExportPanel panel;
    ExportAxis axis{.Name = "", .Unit = "V", .UsesPrefixes = false};
    for (std::size_t lane = 0; lane < lanes.size(); ++lane) {
        const std::size_t position = lanes.size() - 1 - lane;
        const std::string label = GetVoltageLabel(schematic.GetConnectivity(), lanes[lane].Node);
        const std::vector<double> &voltages = transient.NodeVoltages[static_cast<std::size_t>(lanes[lane].Node)];
        axis.Ticks.push_back(
            {.Value = static_cast<double>(position) * LANE_STRIDE + LANE_HIGH_OFFSET / 2.0, .Label = label});
        panel.Series.push_back({.Name = label,
                                .Values = BuildLane(voltages, lanes[lane].LogicLevel, position),
                                .Samples = voltages,
                                .Color = ToExportColor(GetNodeColor(lanes[lane].Node))});
    }
    if (bit_values) {
        AddBitNotes(panel, transient, lanes, view);
    }
    axis.Range =
        ExportRange{.From = LANE_LOW_OFFSET - LANE_MARGIN,
                    .To = static_cast<double>(lanes.size() - 1) * LANE_STRIDE + LANE_HIGH_OFFSET + LANE_MARGIN};
    panel.YAxes.push_back(std::move(axis));
    return panel;
}

void AddExportMath(PanelBuilder &builder, const std::vector<MathTrace> &math, const std::string &suffix,
                   const bool in_legend) {
    for (const MathTrace &trace : math) {
        builder.Add({.Name = trace.Label + suffix,
                     .LegendLabel = in_legend ? trace.Label : "",
                     .Values = trace.Values,
                     .Color = ToExportColor(trace.Color),
                     .Heavy = true},
                    trace.Axis);
    }
}

} // namespace

/**
 * @brief   Tells whether any current of a result is measured.
 * @param[in] currents   Currents of the result.
 * @param[in] schematic  Schematic that keeps the measurements.
 * @return  True when one of them is measured.
 */
bool HasMeasuredCurrent(const std::vector<Core::ComponentTrace> &currents, const Schematic &schematic) {
    return std::ranges::any_of(currents, [&schematic](const Core::ComponentTrace &current) {
        return schematic.IsCurrentMeasured(current.Name);
    });
}

/**
 * @brief   Lists the nodes of logic lanes.
 * @param[in] lanes  Logic nodes drawn as lanes.
 * @return  Their node numbers, in order.
 */
std::vector<int> ListNodes(const std::vector<LogicNode> &lanes) {
    std::vector<int> nodes;
    for (const LogicNode &lane : lanes) {
        nodes.push_back(lane.Node);
    }
    return nodes;
}

/**
 * @brief   Tells whether the curves of a family differ at a sample by more than residue next to their size, so
 *          labeling each one tells something; a trace the stepped value leaves unchanged would only pile its labels up
 *          on one line.
 * @param[in] curves  One curve per step.
 * @param[in] sample  Index of the sample to compare.
 * @return  True when they are apart there.
 */
bool CurvesDiffer(const std::vector<const std::vector<double> *> &curves, const std::size_t sample) {
    double largest = 0.0;
    double lowest = std::numeric_limits<double>::infinity();
    double highest = -std::numeric_limits<double>::infinity();
    for (const std::vector<double> *curve : curves) {
        for (const double value : *curve) {
            if (std::isfinite(value)) {
                largest = std::max(largest, std::abs(value));
            }
        }
        if (sample < curve->size() && std::isfinite((*curve)[sample])) {
            lowest = std::min(lowest, (*curve)[sample]);
            highest = std::max(highest, (*curve)[sample]);
        }
    }
    return highest > lowest && highest - lowest > std::max(largest * NEGLIGIBLE_SPREAD, RESIDUE_LEVEL);
}

/**
 * @brief   Lists every shown trace of a DC sweep, with one curve per step, named and colored as the plot draws it.
 * @param[in] sweep          A DC sweep, stepped or not.
 * @param[in] schematic      Schematic that keeps the measurements.
 * @param[in] show_currents  Whether the plot shows currents.
 * @param[in] math           Math traces of each curve, in the order of the curves.
 * @return  The families of the measured voltages, then currents, then math channels.
 */
std::vector<CurveFamily> ListCurveFamilies(const Core::DCSweep &sweep, Schematic &schematic, const bool show_currents,
                                           const std::vector<std::vector<MathTrace>> &math) {
    std::vector<CurveFamily> families;
    const Core::DCSweepCurve &first_curve = sweep.Curves.front();
    for (std::size_t node = 1; node < first_curve.NodeVoltages.size(); ++node) {
        if (!schematic.IsVoltageMeasured(static_cast<int>(node))) {
            continue;
        }
        CurveFamily family{.Label = GetVoltageLabel(schematic.GetConnectivity(), static_cast<int>(node)),
                           .Unit = "V",
                           .Color = GetNodeColor(static_cast<int>(node)),
                           .Axis = ImAxis_Y1,
                           .Curves = {}};
        for (const Core::DCSweepCurve &curve : sweep.Curves) {
            family.Curves.push_back(&curve.NodeVoltages[node]);
        }
        families.push_back(std::move(family));
    }
    for (std::size_t current = 0; show_currents && current < first_curve.Currents.size(); ++current) {
        if (!schematic.IsCurrentMeasured(first_curve.Currents[current].Name)) {
            continue;
        }
        CurveFamily family{.Label = std::format("I({})", first_curve.Currents[current].Name),
                           .Unit = "A",
                           .Color = GetCurrentColor(current),
                           .Axis = ImAxis_Y2,
                           .Curves = {}};
        for (const Core::DCSweepCurve &curve : sweep.Curves) {
            family.Curves.push_back(&curve.Currents[current].Values);
        }
        families.push_back(std::move(family));
    }
    // Every curve evaluates the same channels, so the traces of one channel share an index across curves
    const std::size_t math_count = math.size() == sweep.Curves.size() ? math.front().size() : 0;
    for (std::size_t trace = 0; trace < math_count; ++trace) {
        CurveFamily family{.Label = math.front()[trace].Label,
                           .Unit = math.front()[trace].Unit.c_str(),
                           .Color = math.front()[trace].Color,
                           .Axis = math.front()[trace].Axis,
                           .Curves = {}};
        for (const std::vector<MathTrace> &curve_math : math) {
            if (trace < curve_math.size()) {
                family.Curves.push_back(&curve_math[trace].Values);
            }
        }
        if (family.Curves.size() == sweep.Curves.size()) {
            families.push_back(std::move(family));
        }
    }
    return families;
}

/**
 * @brief   Names a curve of a stepped DC sweep by the value of its stepped source.
 * @param[in] sweep  A stepped DC sweep.
 * @param[in] curve  One of its curves.
 * @return  Such as "V2=5V".
 */
std::string FormatStepLabel(const Core::DCSweep &sweep, const Core::DCSweepCurve &curve) {
    return std::format("{}={}{}", sweep.SteppedSource, Core::FormatValue(curve.StepValue), sweep.SteppedUnit);
}

/**
 * @brief   Tells whether the input source reaches the output at all: through a logic gate, or behind an ideal source,
 *          the gain is next to nothing and the noise referred to the input grows without meaning.
 * @param[in] noise  A noise result.
 * @return  True when the input-referred noise means something.
 */
bool HasInputGain(const Core::Noise &noise) {
    return noise.InputTotal > 0.0 && noise.OutputTotal >= noise.InputTotal * MIN_INPUT_GAIN;
}

/**
 * @brief   Writes the unit of a density per root hertz, as noise is written.
 * @param[in] unit  Unit of the signal, such as "V".
 * @return  Such as "V/√Hz".
 */
std::string GetDensityUnit(const std::string &unit) {
    return unit + "/√Hz";
}

/**
 * @brief   Builds the figure of a transient plot: the analog traces in one panel and the logic lanes in another.
 * @param[in] transient   The transient shown, which is the selected step of a stepped one, since every step has its
 *                        own time points.
 * @param[in] math        Its math traces.
 * @param[in] lanes       Measured nodes of logic parts, drawn as lanes.
 * @param[in] schematic   Schematic that keeps the measurements and names the nodes.
 * @param[in] bit_values  Whether the lanes are labeled 0, 1 or X.
 * @param[in] view        The X range the plot shows, so zooming in before exporting crops the image as well.
 * @return  The figure; the analog panel is left out when only logic signals are measured, as on screen.
 */
ExportFigure BuildTransientFigure(const Core::Transient &transient, const std::vector<MathTrace> &math,
                                  const std::vector<LogicNode> &lanes, Schematic &schematic, const bool bit_values,
                                  const PlotSpan &view) {
    ExportFigure figure;
    figure.XAxis = {.Name = "Time", .Unit = "s"};
    figure.Xs = transient.Times;
    figure.XRange = ExportRange{.From = view.From, .To = view.To};
    PanelBuilder builder("", GetLinearExportAxes(ThirdAxisUnit(math)));
    AddExportTraces(builder, transient.NodeVoltages, transient.Currents, schematic, "", true, ListNodes(lanes));
    AddExportMath(builder, math, "", true);
    ExportPanel analog = builder.Take();
    if (!analog.Series.empty() || lanes.empty()) {
        figure.Panels.push_back(std::move(analog));
    }
    if (!lanes.empty()) {
        figure.Panels.push_back(BuildLanePanel(transient, lanes, schematic, bit_values, view));
    }
    return figure;
}

/**
 * @brief   Builds the figure of the spectra as plotted.
 * @param[in] spectra      Spectra of the measured traces.
 * @param[in] logarithmic  Whether the frequency axis is logarithmic.
 * @param[in] view         The frequencies the plot shows.
 * @return  One panel with every spectrum that is defined over the span.
 */
ExportFigure BuildSpectrumFigure(const TransientSpectra &spectra, const bool logarithmic, const PlotSpan &view) {
    ExportFigure figure;
    figure.XAxis = {.Name = "Frequency", .Unit = "Hz"};
    figure.XLogarithmic = logarithmic;
    figure.Xs = spectra.Frequencies;
    figure.XRange = ExportRange{.From = view.From, .To = view.To};
    PanelBuilder builder("", GetSpectrumExportAxes());
    for (const SpectrumTrace &trace : spectra.Traces) {
        if (trace.Decibels) {
            builder.Add({.Name = trace.Label,
                         .LegendLabel = trace.Label,
                         .Values = *trace.Decibels,
                         .Color = ToExportColor(trace.Color),
                         .Dashed = trace.Dashed},
                        trace.Axis);
        }
    }
    figure.Panels.push_back(builder.Take());
    return figure;
}

/**
 * @brief   Builds the figure of the Bode plots: both panels list the same traces, as the two plots do, with every step
 *          of a stepped sweep.
 * @param[in] sweep      The AC sweep shown.
 * @param[in] schematic  Schematic that keeps the measurements and the other steps.
 * @param[in] view       The frequencies the plots show.
 * @return  The magnitude and phase panels.
 */
ExportFigure BuildACSweepFigure(const Core::ACSweep &sweep, Schematic &schematic, const PlotSpan &view) {
    ExportFigure figure;
    figure.XAxis = {.Name = "Frequency", .Unit = "Hz"};
    figure.XLogarithmic = true;
    figure.Xs = sweep.Frequencies;
    figure.XRange = ExportRange{.From = view.From, .To = view.To};
    PanelBuilder magnitude("Magnitude", {.Voltage = {.Name = "Voltage (dB)", .Unit = "dB", .UsesPrefixes = false},
                                         .Current = {.Name = "Current (dB)", .Unit = "dB", .UsesPrefixes = false},
                                         .Third = {}});
    AddExportTraces(magnitude, sweep.NodeMagnitudesDecibels, sweep.CurrentMagnitudesDecibels, schematic, "", true);
    PanelBuilder phase("Phase", {.Voltage = {.Name = "Phase (deg)", .Unit = "deg", .UsesPrefixes = false},
                                 .Current = {.Name = "Current phase (deg)", .Unit = "deg", .UsesPrefixes = false},
                                 .Third = {}});
    AddExportTraces(phase, sweep.NodePhasesDegrees, sweep.CurrentPhasesDegrees, schematic, "", true);
    // The other steps share the frequencies, so they go in as more columns, without legend entries
    if (const auto &steps = schematic.GetACSweepSteps()) {
        for (std::size_t step = 0; step < steps->Results.size(); ++step) {
            if (step == steps->Selected) {
                continue;
            }
            const Core::ACSweep &other = steps->Results[step];
            const std::string suffix = " at " + FormatStepValue(steps->Parameter, steps->Values[step], steps->Unit);
            AddExportTraces(magnitude, other.NodeMagnitudesDecibels, other.CurrentMagnitudesDecibels, schematic, suffix,
                            false);
            AddExportTraces(phase, other.NodePhasesDegrees, other.CurrentPhasesDegrees, schematic, suffix, false);
        }
    }
    figure.Panels.push_back(magnitude.Take());
    figure.Panels.push_back(phase.Take());
    return figure;
}

/**
 * @brief   Builds the figure of a DC sweep: a stepped sweep exports every curve, labeled with its step where the
 *          curves part most, as in the plot.
 * @param[in] sweep      The DC sweep shown.
 * @param[in] math       Math traces of each curve, in the order of the curves.
 * @param[in] schematic  Schematic that keeps the measurements and names the nodes.
 * @param[in] view       The swept values the plot shows.
 * @return  One panel with every curve.
 */
ExportFigure BuildDCSweepFigure(const Core::DCSweep &sweep, const std::vector<std::vector<MathTrace>> &math,
                                Schematic &schematic, const PlotSpan &view) {
    static const std::vector<MathTrace> no_math;
    ExportFigure figure;
    figure.XAxis = {.Name = sweep.SweptSource, .Unit = sweep.SweptUnit};
    figure.Xs = sweep.SweptValues;
    figure.XRange = ExportRange{.From = view.From, .To = view.To};
    const bool stepped = !sweep.SteppedSource.empty();
    PanelBuilder builder("", GetLinearExportAxes(ThirdAxisUnit(math.empty() ? no_math : math.front())));
    for (std::size_t curve_index = 0; curve_index < sweep.Curves.size(); ++curve_index) {
        const Core::DCSweepCurve &curve = sweep.Curves[curve_index];
        const std::string suffix = stepped ? " at " + FormatStepLabel(sweep, curve) : "";
        AddExportTraces(builder, curve.NodeVoltages, curve.Currents, schematic, suffix, curve_index == 0);
        if (curve_index < math.size()) {
            AddExportMath(builder, math[curve_index], suffix, curve_index == 0);
        }
    }
    if (stepped) {
        const bool show_currents = HasMeasuredCurrent(sweep.Curves.front().Currents, schematic);
        for (const CurveFamily &family : ListCurveFamilies(sweep, schematic, show_currents, math)) {
            const std::size_t sample = FindWidestSpread(family.Curves);
            if (sample >= sweep.SweptValues.size() || !CurvesDiffer(family.Curves, sample)) {
                continue;
            }
            for (std::size_t curve = 0; curve < family.Curves.size(); ++curve) {
                const double y = (*family.Curves[curve])[sample];
                if (std::isfinite(y)) {
                    builder.AddNote(
                        {.X = sweep.SweptValues[sample], .Y = y, .Text = FormatStepLabel(sweep, sweep.Curves[curve])},
                        family.Axis);
                }
            }
        }
    }
    figure.Panels.push_back(builder.Take());
    return figure;
}

/**
 * @brief   Builds the figure of the noise densities on logarithmic axes, as plotted.
 * @param[in] noise      The noise result.
 * @param[in] schematic  Schematic that names the output node.
 * @param[in] shown      Parts whose share is checked in the panel.
 * @param[in] view       The frequencies the plot shows.
 * @return  One panel with the output, the checked shares and the input-referred noise.
 */
ExportFigure BuildNoiseFigure(const Core::Noise &noise, Schematic &schematic, const std::set<std::string> &shown,
                              const PlotSpan &view) {
    ExportFigure figure;
    figure.XAxis = {.Name = "Frequency", .Unit = "Hz"};
    figure.XLogarithmic = true;
    figure.Xs = noise.Frequencies;
    figure.XRange = ExportRange{.From = view.From, .To = view.To};
    ExportPanel panel;
    panel.YAxes = {{.Name = "Noise density", .Unit = GetDensityUnit("V"), .Logarithmic = true}};
    const bool input_shown = HasInputGain(noise);
    const bool input_in_volts = noise.InputUnit == "V";
    if (input_shown && !input_in_volts) {
        panel.YAxes.push_back(
            {.Name = "Input-referred noise", .Unit = GetDensityUnit(noise.InputUnit), .Logarithmic = true});
    }
    const std::string output_label = GetVoltageLabel(schematic.GetConnectivity(), noise.OutputNode);
    panel.Series.push_back({.Name = output_label,
                            .LegendLabel = output_label,
                            .Values = noise.OutputDensities,
                            .Color = ToExportColor(GetNodeColor(noise.OutputNode)),
                            .Heavy = true});
    for (std::size_t index = 0; index < noise.Contributions.size(); ++index) {
        const Core::NoiseContribution &contribution = noise.Contributions[index];
        if (shown.contains(contribution.Name)) {
            panel.Series.push_back({.Name = contribution.Name,
                                    .LegendLabel = contribution.Name,
                                    .Values = contribution.OutputDensities,
                                    .Color = ToExportColor(GetCurrentColor(index))});
        }
    }
    if (input_shown) {
        const std::string input_label = std::format("Input-referred ({})", noise.InputSource);
        panel.Series.push_back({.Name = input_label,
                                .LegendLabel = input_label,
                                .Values = noise.InputDensities,
                                .Axis = input_in_volts ? 0U : 1U,
                                .Color = ToExportColor(AdaptToBackground(INPUT_NOISE_COLOR)),
                                .Dashed = true});
    }
    figure.Panels.push_back(std::move(panel));
    return figure;
}

} // namespace GUI
