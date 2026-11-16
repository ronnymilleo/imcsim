/**
 * @file    stability_view.cpp
 * @brief   What the Output window shows of the stability of a two-port: the curves of K, mu and the gain bound, and the
 *          figures of the plots for the export, with the stability circles on the Smith chart.
 */

#include "plots/stability_view.h"

#include "results/stability_summary.h"
#include "spice/spice_value.h"
#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <utility>

namespace GUI {

namespace {

constexpr double MISSING = std::numeric_limits<double>::quiet_NaN();

/// How much more than the largest value the axis of the factors reaches, so the line does not touch the frame.
constexpr double AXIS_HEADROOM = 1.1;

double ToPlotted(const double value) {
    return std::isfinite(value) ? value : MISSING;
}

bool HasFiniteValue(const std::vector<double> &values) {
    return std::ranges::any_of(values, [](const double value) { return std::isfinite(value); });
}

ExportRange FindFactorRange(const std::vector<double> &first, const std::vector<double> &second) {
    double lowest = 0.0;
    double highest = 0.0;
    for (const std::vector<double> *values : {&first, &second}) {
        for (const double value : *values) {
            if (std::isfinite(value)) {
                lowest = std::min(lowest, value);
                highest = std::max(highest, value);
            }
        }
    }
    return {.From = lowest * AXIS_HEADROOM,
            .To = std::clamp(highest * AXIS_HEADROOM, FACTOR_AXIS_MINIMUM_CEILING, FACTOR_AXIS_CEILING)};
}

std::string NameMaximumAvailableGain(const TwoPortSweep &sweep) {
    std::size_t unilateral = 0;
    std::size_t available = 0;
    for (const TwoPortMetrics &metrics : sweep.Metrics) {
        unilateral += metrics.Gain == GainKind::UNILATERAL ? 1 : 0;
        available += metrics.Gain == GainKind::MAG ? 1 : 0;
    }
    if (unilateral > 0 && available == 0) {
        return "Unilateral gain (S12 = 0)";
    }
    return unilateral > 0 ? "MAG (unilateral gain where S12 = 0)" : "MAG (unconditionally stable)";
}

/**
 * @brief   Describes where the stable terminations of a boundary lie, for the table.
 */
std::string DescribeStableRegion(const StabilityBoundary &boundary) {
    if (boundary.Coverage == ChartCoverage::ENTIRELY_STABLE) {
        return "the whole chart";
    }
    if (boundary.Coverage == ChartCoverage::ENTIRELY_UNSTABLE) {
        return "none of the chart";
    }
    if (boundary.Shape == BoundaryShape::CIRCLE) {
        return boundary.IsStableInside ? "inside the circle" : "outside the circle";
    }
    const double origin_side = -std::real(std::conj(boundary.StableNormal) * boundary.Center);
    return origin_side > 0.0 ? "the side of the origin" : "the side away from the origin";
}

ExportSmithCircle MakeCircle(const std::string &name, const ExportColor color, const double frequency_hertz,
                             const StabilityBoundary &boundary) {
    ExportSmithCircle circle{.Name = name,
                             .Color = color,
                             .FrequencyHertz = frequency_hertz,
                             .Center = boundary.Center,
                             .Radius = boundary.Shape == BoundaryShape::CIRCLE ? boundary.Radius : MISSING,
                             .StableRegion = DescribeStableRegion(boundary)};
    switch (boundary.Shape) {
    case BoundaryShape::CIRCLE:
        circle.Shape = "circle";
        break;
    case BoundaryShape::LINE:
        circle.Shape = "line";
        break;
    case BoundaryShape::NONE:
    default:
        circle.Shape = "none";
        circle.Center = {};
        break;
    }
    if (boundary.Shape != BoundaryShape::NONE && boundary.Coverage == ChartCoverage::PARTLY) {
        circle.Boundary = SampleBoundary(boundary);
        for (const HatchSegment &segment : BuildUnstableHatch(boundary)) {
            circle.Hatch.push_back({.From = segment.From, .To = segment.To});
        }
    }
    return circle;
}

} // namespace

/**
 * @brief   Works out the curves of the Stability view.
 * @param[in] sweep  Metrics of a sweep.
 * @return  K and mu, and the gain as MAG where unconditionally stable and MSG where not, each with a gap (not a number)
 *          where it has no finite value, with the labels of the two kinds of gain and the range of the factors' axis.
 */
StabilityCurves BuildStabilityCurves(const TwoPortSweep &sweep) {
    StabilityCurves curves;
    curves.Frequencies = sweep.Frequencies;
    const std::size_t count = sweep.Metrics.size();
    curves.K.assign(count, MISSING);
    curves.Mu.assign(count, MISSING);
    curves.MaximumAvailableGainDecibels.assign(count, MISSING);
    curves.MaximumStableGainDecibels.assign(count, MISSING);
    for (std::size_t index = 0; index < count; ++index) {
        const TwoPortMetrics &metrics = sweep.Metrics[index];
        if (!metrics.IsValid) {
            continue;
        }
        curves.K[index] = ToPlotted(metrics.K);
        curves.Mu[index] = ToPlotted(metrics.Mu);
        if (!metrics.GainDecibels) {
            continue;
        }
        if (metrics.Gain == GainKind::MSG) {
            curves.MaximumStableGainDecibels[index] = *metrics.GainDecibels;
        } else {
            curves.MaximumAvailableGainDecibels[index] = *metrics.GainDecibels;
        }
    }
    // Where the gain turns from MAG to MSG the samples of the two kinds are a step apart; the MSG curve reaches back
    // to the stable sample at each end, so the two are drawn as one line
    for (std::size_t index = 0; index < count; ++index) {
        if (sweep.Metrics[index].Gain != GainKind::MSG) {
            continue;
        }
        for (const std::size_t neighbor : {index == 0 ? index : index - 1, std::min(index + 1, count - 1)}) {
            const TwoPortMetrics &next = sweep.Metrics[neighbor];
            if (next.Gain == GainKind::MAG && next.MaximumStableGainDecibels) {
                curves.MaximumStableGainDecibels[neighbor] = *next.MaximumStableGainDecibels;
            }
        }
    }
    curves.MaximumAvailableGainLabel = NameMaximumAvailableGain(sweep);
    curves.MaximumStableGainLabel = "MSG (potentially unstable)";
    curves.FactorRange = FindFactorRange(curves.K, curves.Mu);
    return curves;
}

/**
 * @brief   Builds the figure of the Stability view: K and mu over a line at 1, and the gain below.
 * @param[in] sweep  Metrics of a sweep.
 * @param[in] view   Frequencies the plots show, or none for the whole sweep.
 * @return  A panel of the factors, with a dashed line at 1 above which the two-port is unconditionally stable, and a
 *          panel of the gain in decibels, MAG solid and MSG dashed.
 */
ExportFigure BuildStabilityFigure(const TwoPortSweep &sweep, const std::optional<ExportRange> &view) {
    const StabilityCurves curves = BuildStabilityCurves(sweep);
    ExportFigure figure;
    figure.XAxis = {.Name = "Frequency", .Unit = "Hz"};
    figure.XLogarithmic = true;
    figure.Xs = curves.Frequencies;
    figure.XRange = view;
    ExportPanel factors{.Title = "Stability",
                        .YAxes = {{.Name = "K and mu", .UsesPrefixes = false, .Range = curves.FactorRange}}};
    factors.Series.push_back({.Name = "K", .LegendLabel = "K", .Values = curves.K, .Color = STABILITY_K_COLOR});
    factors.Series.push_back({.Name = "mu", .LegendLabel = "mu", .Values = curves.Mu, .Color = STABILITY_MU_COLOR});
    factors.Series.push_back({.Name = "1",
                              .LegendLabel = "1, stable above",
                              .Values = std::vector<double>(curves.Frequencies.size(), 1.0),
                              .Color = STABILITY_LINE_AT_ONE_COLOR,
                              .Dashed = true,
                              .InTable = false});
    ExportPanel gain{.Title = "Gain", .YAxes = {{.Name = "Gain (dB)", .Unit = "dB", .UsesPrefixes = false}}};
    if (HasFiniteValue(curves.MaximumAvailableGainDecibels)) {
        gain.Series.push_back({.Name = "MAG",
                               .LegendLabel = curves.MaximumAvailableGainLabel,
                               .Values = curves.MaximumAvailableGainDecibels,
                               .Color = STABILITY_MAG_COLOR});
    }
    if (HasFiniteValue(curves.MaximumStableGainDecibels)) {
        gain.Series.push_back({.Name = "MSG",
                               .LegendLabel = curves.MaximumStableGainLabel,
                               .Values = curves.MaximumStableGainDecibels,
                               .Color = STABILITY_MSG_COLOR,
                               .Dashed = true});
    }
    figure.Panels.push_back(std::move(factors));
    figure.Panels.push_back(std::move(gain));
    return figure;
}

/**
 * @brief   Builds the figure whose table lists the metrics at every frequency.
 * @param[in] sweep  Metrics of a sweep.
 * @return  One panel with K, mu, mu', |Delta| and the gain, MAG where it is that and MSG where it is that; the cells of
 *          a value that is infinite or missing stay empty.
 */
ExportFigure BuildStabilityTableFigure(const TwoPortSweep &sweep) {
    ExportFigure figure;
    figure.XAxis = {.Name = "Frequency", .Unit = "Hz"};
    figure.XLogarithmic = true;
    figure.Xs = sweep.Frequencies;
    ExportPanel panel{.Title = "Stability", .YAxes = {{.Name = "Value", .UsesPrefixes = false}}};
    ExportSeries k{.Name = "K"};
    ExportSeries mu{.Name = "mu"};
    ExportSeries mu_prime{.Name = "mu'"};
    ExportSeries delta{.Name = "|Delta|"};
    ExportSeries available{.Name = "MAG", .TableUnit = "dB"};
    ExportSeries stable{.Name = "MSG", .TableUnit = "dB"};
    for (const TwoPortMetrics &metrics : sweep.Metrics) {
        k.Values.push_back(metrics.IsValid ? metrics.K : MISSING);
        mu.Values.push_back(metrics.IsValid ? metrics.Mu : MISSING);
        mu_prime.Values.push_back(metrics.IsValid ? metrics.MuPrime : MISSING);
        delta.Values.push_back(metrics.IsValid ? metrics.DeltaMagnitude : MISSING);
        const bool has_msg = metrics.Gain == GainKind::MSG;
        const bool has_mag = metrics.Gain == GainKind::MAG || metrics.Gain == GainKind::UNILATERAL;
        available.Values.push_back(has_mag ? metrics.GainDecibels.value_or(MISSING) : MISSING);
        stable.Values.push_back(has_msg ? metrics.GainDecibels.value_or(MISSING) : MISSING);
    }
    for (ExportSeries *series : {&k, &mu, &mu_prime, &delta, &available, &stable}) {
        panel.Series.push_back(std::move(*series));
    }
    figure.Panels.push_back(std::move(panel));
    return figure;
}

/**
 * @brief   Builds the figure of the Smith chart of the shown entries with the stability circles at one frequency.
 * @param[in] result     S matrix of the sweep.
 * @param[in] entries    Entries to draw, which must lie inside the matrix.
 * @param[in] frequency  Index of the frequency the circles are worked out at.
 * @return  The figure of BuildSmithFigure() with the source and load circles, each with its unstable side hatched; a
 *          circle that does not cross the chart is listed in the table only. A result that is not of two ports, or a
 *          frequency outside the sweep, adds no circles.
 */
ExportFigure BuildSmithFigureWithCircles(const Core::SParameters &result, const std::vector<SParameterEntry> &entries,
                                         const std::size_t frequency) {
    ExportFigure figure = BuildSmithFigure(result, entries);
    if (!figure.Smith || !IsTwoPort(result) || frequency >= result.Frequencies.size()) {
        return figure;
    }
    const double frequency_hertz = result.Frequencies[frequency];
    const StabilityCircles circles = ComputeStabilityCircles(ReadTwoPortMatrix(result, frequency));
    ExportSmith &smith = *figure.Smith;
    smith.Circles.push_back(
        MakeCircle("Source stability circle", SOURCE_CIRCLE_COLOR, frequency_hertz, circles.Source));
    smith.Circles.push_back(MakeCircle("Load stability circle", LOAD_CIRCLE_COLOR, frequency_hertz, circles.Load));
    const bool is_any_drawn = !smith.Circles.front().Boundary.empty() || !smith.Circles.back().Boundary.empty();
    smith.Title = std::format("Smith chart, stability circles at {}Hz{}", Core::FormatValue(frequency_hertz),
                              is_any_drawn ? "; the hatching marks the unstable side" : ": none crosses the chart");
    return figure;
}

} // namespace GUI
