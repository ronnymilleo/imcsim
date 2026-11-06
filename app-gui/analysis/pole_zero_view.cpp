/**
 * @file    pole_zero_view.cpp
 * @brief   What the Output window shows of a pole-zero result: the rows of its table, the points of its s-plane and the
 *          frequencies that mark a Bode plot.
 */

#include "analysis/pole_zero_view.h"

#include "spice/spice_value.h"
#include <algorithm>
#include <format>
#include <utility>

namespace GUI {

namespace {

std::string FormatHertz(const double frequency) {
    return std::format("{}Hz", Core::FormatValue(frequency));
}

/// Quality factors and damping ratios are plain numbers, so no scale suffix: 0.74, not 740m.
std::string FormatPlain(const double value) {
    return std::format("{:.3g}", value);
}

std::string FormatQuality(const Core::PoleZeroRoot &root) {
    if (root.QualityFactor) {
        return FormatPlain(*root.QualityFactor);
    }
    return root.DampingRatio ? "inf" : "-";
}

std::string FormatDamping(const Core::PoleZeroRoot &root) {
    return root.DampingRatio ? FormatPlain(*root.DampingRatio) : "-";
}

/// Half a decade, the room a symmetric logarithmic axis keeps past the outermost root, so no mark sits on the edge.
constexpr double SYMLOG_PADDING_FACTOR = 3.1622776601683795;
/// Share of the span a linear axis keeps past the outermost root.
constexpr double LINEAR_PADDING_SHARE = 0.1;
/// Share of the larger side a range gets on the side that holds no root, so the axes through the origin stay in view.
constexpr double EMPTY_SIDE_SHARE = 0.1;
/// Range of an axis that holds only the origin.
constexpr double DEFAULT_HALF_RANGE = 1.0;

/**
 * @brief   Widens the range of some values, which includes 0, so every mark has room around it.
 */
std::pair<double, double> PadRange(double lowest, double highest, const bool is_symlog) {
    if (lowest == 0.0 && highest == 0.0) {
        return {-DEFAULT_HALF_RANGE, DEFAULT_HALF_RANGE};
    }
    if (is_symlog) {
        lowest *= SYMLOG_PADDING_FACTOR;
        highest *= SYMLOG_PADDING_FACTOR;
    } else {
        const double margin = (highest - lowest) * LINEAR_PADDING_SHARE;
        lowest -= lowest < 0.0 ? margin : 0.0;
        highest += highest > 0.0 ? margin : 0.0;
    }
    // A side with no root still shows a little of the plane, as the axis through the origin
    const double empty_side = EMPTY_SIDE_SHARE * std::max(-lowest, highest);
    return {std::min(lowest, -empty_side), std::max(highest, empty_side)};
}

} // namespace

/**
 * @brief   Writes the table of a result, one row per real root and per conjugate pair, in the order of the result.
 * @param[in] result  The result.
 * @return  The rows; the imaginary part of a pair has "+/-" in front, and a root with no quality factor reads "-".
 */
std::vector<PoleZeroRow> MakePoleZeroRows(const Core::PoleZero &result) {
    std::vector<PoleZeroRow> rows;
    for (const Core::PoleZeroRoot &root : result.Roots) {
        PoleZeroRow row;
        row.Kind = root.IsPole ? "pole" : "zero";
        row.Real = Core::FormatValue(root.Real);
        row.Imaginary = root.Imaginary != 0.0 ? std::format("+/-{}", Core::FormatValue(root.Imaginary)) : "0";
        row.Frequency = FormatHertz(root.NaturalFrequencyHertz);
        row.Quality = FormatQuality(root);
        row.Damping = FormatDamping(root);
        row.IsRightHalfPlane = root.IsRightHalfPlane;
        rows.push_back(std::move(row));
    }
    return rows;
}

/**
 * @brief   Writes what a mark in the s-plane says when hovered: the kind, the natural frequency and, for a pair, its
 *          quality factor and damping ratio.
 * @param[in] root  The pole or zero.
 * @return  Such as "pole at 1.59kHz" or "pole pair at 1.07kHz, Q 0.742, zeta 0.674".
 */
std::string DescribePoleZeroRoot(const Core::PoleZeroRoot &root) {
    const char *kind = root.IsPole ? "pole" : "zero";
    if (root.Imaginary == 0.0) {
        return std::format("{} at {}", kind, FormatHertz(root.NaturalFrequencyHertz));
    }
    return std::format("{} pair at {}, Q {}, zeta {}", kind, FormatHertz(root.NaturalFrequencyHertz),
                       FormatQuality(root), FormatDamping(root));
}

/**
 * @brief   Lists the points of the s-plane for the poles or the zeros that are in the left or in the right half-plane.
 * @param[in] result               The result.
 * @param[in] is_pole              True for the poles, false for the zeros.
 * @param[in] is_right_half_plane  Which half-plane to take them from.
 * @return  Every root, with both members of a conjugate pair, in rad/s.
 */
PoleZeroPoints CollectPoleZeroPoints(const Core::PoleZero &result, const bool is_pole, const bool is_right_half_plane) {
    PoleZeroPoints points;
    for (const Core::PoleZeroRoot &root : result.Roots) {
        if (root.IsPole != is_pole || root.IsRightHalfPlane != is_right_half_plane) {
            continue;
        }
        points.Real.push_back(root.Real);
        points.Imaginary.push_back(root.Imaginary);
        if (root.Imaginary != 0.0) {
            points.Real.push_back(root.Real);
            points.Imaginary.push_back(-root.Imaginary);
        }
    }
    return points;
}

/**
 * @brief   Lists the frequencies that mark the poles or the zeros on a Bode plot.
 * @param[in] result               The result.
 * @param[in] is_pole              True for the poles, false for the zeros.
 * @param[in] is_right_half_plane  Which half-plane to take them from.
 * @return  The natural frequency of each, in Hz, leaving out those at 0 Hz, which a logarithmic axis cannot show.
 */
std::vector<double> ListRootFrequencies(const Core::PoleZero &result, const bool is_pole,
                                        const bool is_right_half_plane) {
    std::vector<double> frequencies;
    for (const Core::PoleZeroRoot &root : result.Roots) {
        if (root.IsPole == is_pole && root.IsRightHalfPlane == is_right_half_plane &&
            root.NaturalFrequencyHertz > 0.0) {
            frequencies.push_back(root.NaturalFrequencyHertz);
        }
    }
    return frequencies;
}

/**
 * @brief   Says how many pole-zero pairs were left out because they sit at the same place.
 * @param[in] cancelled_pairs  Count from the result.
 * @return  The note under the plot; empty when none cancelled.
 */
std::string DescribeCancelledPairs(const int cancelled_pairs) {
    if (cancelled_pairs <= 0) {
        return {};
    }
    return std::format("{} pole-zero {} at the same place cancel and {} left out", cancelled_pairs,
                       cancelled_pairs == 1 ? "pair" : "pairs", cancelled_pairs == 1 ? "is" : "are");
}

/**
 * @brief   Works out the visible range of the s-plane for a result.
 * @param[in] result      The result.
 * @param[in] is_symlog   Whether the axes are symmetric logarithmic, which pad by half a decade, or linear, which pad
 * by a tenth of the span.
 * @return  A range that holds the origin and every root, both members of a pair, with room beyond the outermost ones;
 *          the imaginary range is symmetric, as the roots are.
 */
PlaneLimits ComputePlaneLimits(const Core::PoleZero &result, const bool is_symlog) {
    double real_lowest = 0.0;
    double real_highest = 0.0;
    double imaginary_largest = 0.0;
    for (const Core::PoleZeroRoot &root : result.Roots) {
        real_lowest = std::min(real_lowest, root.Real);
        real_highest = std::max(real_highest, root.Real);
        imaginary_largest = std::max(imaginary_largest, root.Imaginary);
    }
    const auto [real_min, real_max] = PadRange(real_lowest, real_highest, is_symlog);
    const auto [imaginary_min, imaginary_max] = PadRange(-imaginary_largest, imaginary_largest, is_symlog);
    return {.RealMin = real_min, .RealMax = real_max, .ImaginaryMin = imaginary_min, .ImaginaryMax = imaginary_max};
}

} // namespace GUI
