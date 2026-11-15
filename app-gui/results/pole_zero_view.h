/**
 * @file    pole_zero_view.h
 * @brief   What the Output window shows of a pole-zero result: the rows of its table, the points of its s-plane and the
 *          frequencies that mark a Bode plot.
 * @details Pure functions with no ImGui, so the tests run them; the drawing is in the pole-zero tab and the AC sweep
 * tab.
 */

#ifndef IMCSIM_POLE_ZERO_VIEW_H
#define IMCSIM_POLE_ZERO_VIEW_H

#include "simulation/simulation_results.h"
#include <string>
#include <vector>

namespace GUI {

/**
 * @struct  PoleZeroRow
 * @brief   One row of the table: a pole or a zero, with a conjugate pair on one row.
 */
struct PoleZeroRow {
    /// "pole" or "zero".
    std::string Kind;
    std::string Real;
    /// The imaginary part of a pair, with "+/-" in front; "0" for a real root.
    std::string Imaginary;
    std::string Frequency;
    /// "-" for a real root and "inf" for a pair on the imaginary axis.
    std::string Quality;
    std::string Damping;
    bool IsRightHalfPlane = false;
};

/**
 * @struct  PoleZeroPoints
 * @brief   Points of the s-plane in rad/s, real parts and imaginary parts side by side.
 */
struct PoleZeroPoints {
    std::vector<double> Real;
    std::vector<double> Imaginary;
};

/**
 * @struct  PlaneLimits
 * @brief   The range of the s-plane that shows every root with room around it, in rad/s.
 */
struct PlaneLimits {
    double RealMin = -1.0;
    double RealMax = 1.0;
    double ImaginaryMin = -1.0;
    double ImaginaryMax = 1.0;
};

// Table
std::vector<PoleZeroRow> MakePoleZeroRows(const Core::PoleZero &result);
std::string DescribePoleZeroRoot(const Core::PoleZeroRoot &root);

// Plots
PoleZeroPoints CollectPoleZeroPoints(const Core::PoleZero &result, bool is_pole, bool is_right_half_plane);
std::vector<double> ListRootFrequencies(const Core::PoleZero &result, bool is_pole, bool is_right_half_plane);
std::string DescribeCancelledPairs(int cancelled_pairs);
PlaneLimits ComputePlaneLimits(const Core::PoleZero &result, bool is_symlog);

} // namespace GUI

#endif // IMCSIM_POLE_ZERO_VIEW_H
