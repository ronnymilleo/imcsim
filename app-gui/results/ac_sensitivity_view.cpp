/**
 * @file    ac_sensitivity_view.cpp
 * @brief   What the editor and the Output window show of an AC sensitivity result: which frequency to read, the parts
 *          that matter most, the heat level of each part, the curves and the rows and texts of the table.
 */

#include "results/ac_sensitivity_view.h"

#include "results/logarithmic_level.h"
#include "results/sensitivity_view.h"
#include "simulation/worst_case.h"
#include "spice/spice_value.h"
#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <numbers>
#include <numeric>

namespace GUI {

namespace {

constexpr double DECIBELS_PER_NEPER = 20.0 / std::numbers::ln10;
constexpr double DEGREES_PER_RADIAN = 180.0 / std::numbers::pi;
constexpr double MISSING = std::numeric_limits<double>::quiet_NaN();
/// Significant digits of a figure of the table, enough to tell 0.043 dB from 0.047 dB.
constexpr int SIGNIFICANT_DIGITS = 3;

/**
 * @brief   The frequency where the magnitude is at -3 dB from the peak, seen from the peak: the first one to cross
 *          it going up in frequency, or down when the response holds above it all the way up.
 */
std::optional<std::size_t> FindCrossing(const std::vector<double> &magnitudes, const std::size_t peak) {
    const double target = magnitudes[peak] / std::numbers::sqrt2;
    const auto closer = [&](const std::size_t first, const std::size_t second) {
        return std::abs(magnitudes[first] - target) <= std::abs(magnitudes[second] - target) ? first : second;
    };
    for (std::size_t index = peak + 1; index < magnitudes.size(); ++index) {
        if (magnitudes[index] <= target) {
            return closer(index - 1, index);
        }
    }
    for (std::size_t index = peak; index-- > 0;) {
        if (magnitudes[index] <= target) {
            return closer(index + 1, index);
        }
    }
    return std::nullopt;
}

std::string FormatFigure(const std::optional<double> &value, const char *unit) {
    if (!value) {
        return "-";
    }
    return std::format("{:+.{}g} {}", *value == 0.0 ? 0.0 : *value, SIGNIFICANT_DIGITS, unit);
}

std::string FormatMagnitudeBound(const double value, const char *unit) {
    return std::format("±{:.{}g} {}", value == 0.0 ? 0.0 : value, SIGNIFICANT_DIGITS, unit);
}

std::string FormatACPartTolerance(const Core::ACPartSensitivity &part, const std::size_t frequency_index) {
    if (!part.TolerancePercent || *part.TolerancePercent <= 0.0) {
        return "-";
    }
    const std::optional<double> gain = Core::GetACPartGainWorstCase(part, frequency_index);
    const std::optional<double> phase = Core::GetACPartPhaseWorstCase(part, frequency_index);
    if (!gain && !phase) {
        return "-";
    }
    const std::string gain_text = gain ? FormatMagnitudeBound(*gain, "dB") : "-";
    const std::string phase_text = phase ? FormatMagnitudeBound(*phase, "deg") : "-";
    return std::format("{} / {} ({}%)", gain_text, phase_text, Core::FormatValue(*part.TolerancePercent));
}

} // namespace

/**
 * @brief   Finds the frequency the table starts at: the -3 dB point of the response, where the corner is.
 * @param[in] sensitivity  The result.
 * @return  Index of the frequency nearest to where the magnitude of the output has fallen to 1/sqrt(2) of its peak,
 *          going up from the peak and then down; the peak itself when it never falls that far; 0 for an empty result.
 */
std::size_t FindCornerFrequencyIndex(const Core::ACSensitivity &sensitivity) {
    if (sensitivity.Output.empty()) {
        return 0;
    }
    std::vector<double> magnitudes;
    for (const std::complex<double> &output : sensitivity.Output) {
        magnitudes.push_back(std::abs(output));
    }
    const auto peak = static_cast<std::size_t>(std::ranges::max_element(magnitudes) - magnitudes.begin());
    return FindCrossing(magnitudes, peak).value_or(peak);
}

/**
 * @brief   Finds the frequency of the sweep nearest to one, on the logarithmic scale the sweep is spaced on.
 * @param[in] sensitivity  The result.
 * @param[in] frequency    Frequency in hertz, such as where the user dragged the marker.
 * @return  Index of the nearest frequency; 0 for an empty result or a frequency that is not positive.
 */
std::size_t FindNearestFrequencyIndex(const Core::ACSensitivity &sensitivity, const double frequency) {
    if (sensitivity.Frequencies.empty() || frequency <= 0.0) {
        return 0;
    }
    std::size_t nearest = 0;
    double smallest = std::numeric_limits<double>::infinity();
    for (std::size_t index = 0; index < sensitivity.Frequencies.size(); ++index) {
        const double distance = std::abs(std::log(sensitivity.Frequencies[index] / frequency));
        if (distance < smallest) {
            smallest = distance;
            nearest = index;
        }
    }
    return nearest;
}

/**
 * @brief   Works out how much the response moves when a part grows by 1 %, as a fraction of the response.
 * @param[in] part             The part.
 * @param[in] frequency_index  Index of the frequency in the result.
 * @return  The relative change of the complex response for a 1 % change of the part, in neper (the decibels and the
 *          degrees of the figures, put back into the real and imaginary part of the logarithm of the response); 0 where
 *          the part has no figure.
 */
double GetInfluence(const Core::ACPartSensitivity &part, const std::size_t frequency_index) {
    if (frequency_index >= part.MagnitudeDecibelsPerPercent.size()) {
        return 0.0;
    }
    const double nepers = part.MagnitudeDecibelsPerPercent[frequency_index].value_or(0.0) / DECIBELS_PER_NEPER;
    const double radians = frequency_index < part.PhaseDegreesPerPercent.size()
                             ? part.PhaseDegreesPerPercent[frequency_index].value_or(0.0) / DEGREES_PER_RADIAN
                             : 0.0;
    return std::hypot(nepers, radians);
}

/**
 * @brief   Finds the largest influence at a frequency, which the shares and the heat scale are relative to.
 * @param[in] sensitivity      The result.
 * @param[in] frequency_index  Index of the frequency in the result.
 * @return  The largest influence of any part, or 0 when the result has no part.
 */
double GetLargestInfluence(const Core::ACSensitivity &sensitivity, const std::size_t frequency_index) {
    double largest = 0.0;
    for (const Core::ACPartSensitivity &part : sensitivity.Parts) {
        largest = std::max(largest, GetInfluence(part, frequency_index));
    }
    return largest;
}

/**
 * @brief   Picks the parts that move the response most anywhere in the sweep, to start the plot with.
 * @param[in] sensitivity  The result.
 * @param[in] count        How many to pick at most.
 * @return  Indexes into the parts of the result, the one with the largest peak influence first; parts with no
 *          influence at all are never picked.
 */
std::vector<std::size_t> FindMostInfluential(const Core::ACSensitivity &sensitivity, const std::size_t count) {
    std::vector<double> peaks;
    for (const Core::ACPartSensitivity &part : sensitivity.Parts) {
        double peak = 0.0;
        for (std::size_t index = 0; index < sensitivity.Frequencies.size(); ++index) {
            peak = std::max(peak, GetInfluence(part, index));
        }
        peaks.push_back(peak);
    }
    std::vector<std::size_t> order(peaks.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::ranges::stable_sort(order, [&peaks](const std::size_t first, const std::size_t second) {
        return peaks[first] > peaks[second];
    });
    std::erase_if(order, [&peaks](const std::size_t index) { return peaks[index] <= 0.0; });
    order.resize(std::min(order.size(), count));
    return order;
}

/**
 * @brief   Places a part on the logarithmic heat scale of the influences at a frequency.
 * @param[in] sensitivity      The result.
 * @param[in] part_name        Name of the part, such as "R1".
 * @param[in] frequency_index  Index of the frequency in the result.
 * @return  1 for the part with the largest influence, 0 for one AC_SENSITIVITY_DECADES below it or with none, linear in
 *          between on a logarithmic scale, or no value for a part the result does not list.
 */
std::optional<float> FindACSensitivityLevel(const Core::ACSensitivity &sensitivity,
                                            const std::string_view part_name,
                                            const std::size_t frequency_index) {
    const auto found = std::ranges::find(sensitivity.Parts, part_name, &Core::ACPartSensitivity::Name);
    if (found == sensitivity.Parts.end()) {
        return std::nullopt;
    }
    const double largest = GetLargestInfluence(sensitivity, frequency_index);
    const double influence = GetInfluence(*found, frequency_index);
    if (influence <= 0.0 || largest <= 0.0) {
        return 0.0f;
    }
    return std::clamp(LogarithmicLevel(influence, largest, AC_SENSITIVITY_DECADES), 0.0f, 1.0f);
}

/**
 * @brief   Writes the table of a result at one frequency, the part that moves the response most first.
 * @param[in] sensitivity      The result.
 * @param[in] frequency_index  Index of the frequency in the result.
 * @return  One row per part, ranked by influence; parts of equal influence keep the order of the result.
 */
std::vector<ACSensitivityRow> MakeACSensitivityRows(const Core::ACSensitivity &sensitivity,
                                                    const std::size_t frequency_index) {
    const double largest = GetLargestInfluence(sensitivity, frequency_index);
    std::vector<const Core::ACPartSensitivity *> ranked;
    for (const Core::ACPartSensitivity &part : sensitivity.Parts) {
        ranked.push_back(&part);
    }
    std::ranges::stable_sort(ranked, [frequency_index](const auto *first, const auto *second) {
        return GetInfluence(*first, frequency_index) > GetInfluence(*second, frequency_index);
    });
    std::vector<ACSensitivityRow> rows;
    for (const Core::ACPartSensitivity *part : ranked) {
        const auto figure = [frequency_index](const std::vector<std::optional<double>> &figures) {
            return frequency_index < figures.size() ? figures[frequency_index] : std::nullopt;
        };
        rows.push_back({.Part = FormatSensitivityPartLabel(part->Type, part->Name, part->Element, part->Value),
                        .PartName = part->Name,
                        .Value = std::format("{}{}", Core::FormatValue(part->Value), part->Unit),
                        .Magnitude = FormatDecibelsPerPercent(figure(part->MagnitudeDecibelsPerPercent)),
                        .Phase = FormatDegreesPerPercent(figure(part->PhaseDegreesPerPercent)),
                        .Tolerance = FormatACPartTolerance(*part, frequency_index),
                        .Share = largest > 0.0 ? GetInfluence(*part, frequency_index) / largest : 0.0});
    }
    return rows;
}

/**
 * @brief   Turns the figures of every part into curves a plot can draw.
 * @param[in] sensitivity  The result.
 * @return  One curve per part, in the order of the result, with a gap where the output was too small.
 */
std::vector<ACSensitivityCurve> MakeACSensitivityCurves(const Core::ACSensitivity &sensitivity) {
    const auto to_samples = [](const std::vector<std::optional<double>> &figures) {
        std::vector<double> samples;
        samples.reserve(figures.size());
        for (const std::optional<double> &figure : figures) {
            samples.push_back(figure.value_or(MISSING));
        }
        return samples;
    };
    std::vector<ACSensitivityCurve> curves;
    for (const Core::ACPartSensitivity &part : sensitivity.Parts) {
        curves.push_back({.Magnitude = to_samples(part.MagnitudeDecibelsPerPercent),
                          .Phase = to_samples(part.PhaseDegreesPerPercent)});
    }
    return curves;
}

/**
 * @brief   Writes the change of the gain for a 1 % higher value of a part.
 * @param[in] decibels  Decibels per 1 %, or none where the output was too small.
 * @return  Such as "-0.0434 dB", or "-" for none.
 */
std::string FormatDecibelsPerPercent(const std::optional<double> &decibels) {
    return FormatFigure(decibels, "dB");
}

/**
 * @brief   Writes the change of the phase for a 1 % higher value of a part.
 * @param[in] degrees  Degrees per 1 %, or none where the output was too small.
 * @return  Such as "-0.286 deg", or "-" for none.
 */
std::string FormatDegreesPerPercent(const std::optional<double> &degrees) {
    return FormatFigure(degrees, "deg");
}

/**
 * @brief   Formats the analytical worst-case summary line at one frequency of an AC sensitivity result.
 * @param[in] sensitivity      The result.
 * @param[in] frequency_index  Index of the frequency in the result.
 * @return  Summary of EVA and RSS gain/phase bounds when at least one part has a tolerance, or none otherwise.
 */
std::optional<std::string> FormatACWorstCaseSummary(const Core::ACSensitivity &sensitivity,
                                                    const std::size_t frequency_index) {
    if (frequency_index >= sensitivity.Frequencies.size()) {
        return std::nullopt;
    }
    const Core::ACWorstCasePoint point = Core::ComputeACWorstCaseAt(sensitivity, frequency_index);
    if (point.TolerancedPartCount == 0) {
        return std::nullopt;
    }
    const std::string frequency = std::format("{}Hz", Core::FormatValue(sensitivity.Frequencies[frequency_index]));
    const char *part_word = point.TolerancedPartCount == 1 ? "part" : "parts";
    if (!point.ExtremeGainDecibels && !point.ExtremePhaseDegrees) {
        return std::format("Worst case at {} ({} toleranced {}): output too small",
                           frequency,
                           point.TolerancedPartCount,
                           part_word);
    }
    std::string bounds;
    if (point.ExtremeGainDecibels && point.RssGainDecibels) {
        bounds += std::format("gain {} (extreme), {} (RSS)",
                              FormatMagnitudeBound(*point.ExtremeGainDecibels, "dB"),
                              FormatMagnitudeBound(*point.RssGainDecibels, "dB"));
    }
    if (point.ExtremePhaseDegrees && point.RssPhaseDegrees) {
        if (!bounds.empty()) {
            bounds += " · ";
        }
        bounds += std::format("phase {} (extreme), {} (RSS)",
                              FormatMagnitudeBound(*point.ExtremePhaseDegrees, "deg"),
                              FormatMagnitudeBound(*point.RssPhaseDegrees, "deg"));
    }
    return std::format("Worst case at {} ({} toleranced {}): {}",
                       frequency,
                       point.TolerancedPartCount,
                       part_word,
                       bounds);
}

} // namespace GUI
