/**
 * @file    ac_complex_view.cpp
 * @brief   Complex-plane curves, readings and export figures for the Nyquist and Polar views of an AC sweep.
 */

#include "plots/ac_complex_view.h"

#include "base/node_colors.h"
#include "results/parameter_step.h"
#include "results/trace_math_samples.h"
#include "schematic/probing.h"
#include "spice/spice_value.h"
#include <cmath>
#include <cstdint>
#include <format>
#include <utility>

namespace GUI {

namespace {

constexpr int READING_DIGITS = 4;

ExportColor ToExportColor(const ImU32 color) {
    return {static_cast<std::uint8_t>((color >> IM_COL32_R_SHIFT) & 0xFF),
            static_cast<std::uint8_t>((color >> IM_COL32_G_SHIFT) & 0xFF),
            static_cast<std::uint8_t>((color >> IM_COL32_B_SHIFT) & 0xFF)};
}

void AppendSweepLoci(const Core::ACSweep &sweep,
                     Schematic &schematic,
                     const std::string &suffix,
                     const bool is_faint,
                     std::vector<ComplexLocus> &out) {
    for (std::size_t node = 1; node < sweep.NodeMagnitudesDecibels.size(); ++node) {
        if (!schematic.GetMeasurements().IsVoltageMeasured(static_cast<int>(node))) {
            continue;
        }
        const std::string label = GetVoltageLabel(schematic.GetConnectivity(), static_cast<int>(node));
        out.push_back({.Name = label + suffix,
                       .LegendLabel = is_faint ? "" : label,
                       .Unit = "V",
                       .Color = GetNodeColor(static_cast<int>(node)),
                       .IsDashed = false,
                       .IsFaint = is_faint,
                       .Values = ToPhasors(sweep.NodeMagnitudesDecibels[node], sweep.NodePhasesDegrees[node])});
    }
    for (std::size_t index = 0; index < sweep.CurrentMagnitudesDecibels.size(); ++index) {
        const Core::ComponentTrace &magnitude = sweep.CurrentMagnitudesDecibels[index];
        if (!schematic.GetMeasurements().IsCurrentMeasured(magnitude.Name)
            || index >= sweep.CurrentPhasesDegrees.size()) {
            continue;
        }
        const std::string label = std::format("I({})", magnitude.Name);
        out.push_back({.Name = label + suffix,
                       .LegendLabel = is_faint ? "" : label,
                       .Unit = "A",
                       .Color = GetCurrentColor(index),
                       .IsDashed = true,
                       .IsFaint = is_faint,
                       .Values = ToPhasors(magnitude.Values, sweep.CurrentPhasesDegrees[index].Values)});
    }
}

} // namespace

/**
 * @brief   Collects the complex loci of an AC sweep: faint background steps first, then measured voltages, currents
 *          and math channels.
 * @param[in] sweep      Selected AC sweep result.
 * @param[in] math       Evaluated AC math channels for the selected sweep.
 * @param[in] results    Result store holding optional stepped AC sweeps.
 * @param[in] schematic  Schematic providing measured traces and net names.
 * @return  Ordered list of complex loci to draw or export.
 */
std::vector<ComplexLocus> BuildACSweepComplexLoci(const Core::ACSweep &sweep,
                                                  const ACMathTraces &math,
                                                  const ResultStore &results,
                                                  Schematic &schematic) {
    std::vector<ComplexLocus> loci;
    if (const auto &steps = results.GetACSweepSteps()) {
        for (std::size_t step = 0; step < steps->Results.size(); ++step) {
            if (step == steps->Selected) {
                continue;
            }
            const std::string suffix = " at " + FormatStepValue(steps->Parameter, steps->Values[step], steps->Unit);
            AppendSweepLoci(steps->Results[step], schematic, suffix, true, loci);
        }
    }
    AppendSweepLoci(sweep, schematic, "", false, loci);
    const std::size_t math_count = std::min(math.Magnitudes.size(), math.Phases.size());
    for (std::size_t index = 0; index < math_count; ++index) {
        const MathTrace &mag = math.Magnitudes[index];
        loci.push_back({.Name = mag.Label,
                        .LegendLabel = mag.Label,
                        .Unit = mag.Unit,
                        .Color = mag.Color,
                        .IsDashed = false,
                        .IsFaint = false,
                        .Values = ToPhasors(mag.Values, math.Phases[index].Values)});
    }
    return loci;
}

/**
 * @brief   Formats the tooltip readout for one sample of a complex AC locus.
 * @param[in] frequency_hertz  Frequency of the sample in hertz.
 * @param[in] locus            Complex locus being read.
 * @param[in] sample           Sample index into `locus.Values`.
 * @return  Multi-line tooltip with frequency, polar magnitude/dB/angle, and rectangular real/imaginary parts.
 */
std::string
DescribeACSweepComplexReading(const double frequency_hertz, const ComplexLocus &locus, const std::size_t sample) {
    const std::string header = std::format("{} at {}Hz",
                                           locus.LegendLabel.empty() ? locus.Name : locus.LegendLabel,
                                           Core::FormatValue(frequency_hertz));
    if (sample >= locus.Values.size()) {
        return header;
    }
    const std::complex<double> value = locus.Values[sample];
    const double magnitude = std::abs(value);
    const PhasorFigures figures = ToDecibelsAndDegrees(value);
    const std::string unit_suffix = locus.Unit.empty() ? "" : " " + locus.Unit;
    const std::string polar_line = std::isfinite(figures.MagnitudeDecibels)
                                     ? std::format("{:.{}g}{} ({:.2f} dB) at {:.2f} deg",
                                                   magnitude,
                                                   READING_DIGITS,
                                                   unit_suffix,
                                                   figures.MagnitudeDecibels,
                                                   figures.PhaseDegrees)
                                     : std::format("0{}", unit_suffix);
    const std::string rect_line = std::format("{:.{}g} {} j{:.{}g}{}",
                                              value.real(),
                                              READING_DIGITS,
                                              value.imag() < 0.0 ? "-" : "+",
                                              std::abs(value.imag()),
                                              READING_DIGITS,
                                              unit_suffix);
    return header + "\n" + polar_line + "\n" + rect_line;
}

/**
 * @brief   Builds an `ExportFigure` of the Nyquist or Polar view of an AC sweep.
 * @param[in] sweep                        Selected AC sweep result.
 * @param[in] math                         Evaluated AC math channels.
 * @param[in] results                      Result store holding optional stepped sweeps.
 * @param[in] schematic                    Schematic providing measured traces and net names.
 * @param[in] kind                         `POLAR` or `NYQUIST`.
 * @param[in] mirror_negative_frequencies  Whether Nyquist draws the conjugate negative-frequency branch.
 * @return  Export figure carrying `ComplexPlane`.
 */
ExportFigure BuildACSweepComplexFigure(const Core::ACSweep &sweep,
                                       const ACMathTraces &math,
                                       const ResultStore &results,
                                       Schematic &schematic,
                                       const ComplexPlaneKind kind,
                                       const bool mirror_negative_frequencies) {
    ExportComplexPlane plane;
    plane.Title = kind == ComplexPlaneKind::NYQUIST ? "Nyquist plot" : "Polar plot";
    plane.Kind = kind;
    plane.MirrorNegativeFrequencies = kind == ComplexPlaneKind::NYQUIST && mirror_negative_frequencies;
    if (kind == ComplexPlaneKind::NYQUIST) {
        plane.CriticalPoint = NYQUIST_CRITICAL_POINT;
    }
    for (ComplexLocus &locus : BuildACSweepComplexLoci(sweep, math, results, schematic)) {
        plane.Traces.push_back({.Name = std::move(locus.Name),
                                .LegendLabel = std::move(locus.LegendLabel),
                                .Unit = std::move(locus.Unit),
                                .Color = ToExportColor(locus.Color),
                                .Dashed = locus.IsDashed,
                                .Faint = locus.IsFaint,
                                .Frequencies = sweep.Frequencies,
                                .Values = std::move(locus.Values)});
    }
    ExportFigure figure;
    figure.ComplexPlane = std::move(plane);
    return figure;
}

} // namespace GUI
