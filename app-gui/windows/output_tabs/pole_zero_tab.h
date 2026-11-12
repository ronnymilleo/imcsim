/**
 * @file    pole_zero_tab.h
 * @brief   The Poles and zeros tab of the Output window: the s-plane of the last pole-zero analysis and its table.
 */

#ifndef IMCSIM_POLE_ZERO_TAB_H
#define IMCSIM_POLE_ZERO_TAB_H

#include "results/result_store.h"
#include "schematic/schematic.h"
#include "simulation/simulation_results.h"
#include <cstddef>

namespace GUI {

/**
 * @class   PoleZeroTab
 * @brief   Plots the poles (crosses) and zeros (circles) of the last pole-zero analysis on the s-plane, in rad/s, with
 *          a table of their frequency, quality factor and damping on the right.
 * @details The axes are symmetric logarithmic by default, since the roots of a circuit span many decades, and can be
 *          made linear. A root in the right half-plane is drawn in the warning color: it means an unstable circuit, or
 * a root the solver got wrong.
 */
class PoleZeroTab {
public:
    PoleZeroTab(Schematic &schematic, const ResultStore &results);

    void Draw();

private:
    Schematic &m_Schematic;
    const ResultStore &m_Results;
    /// Whether the axes are linear instead of symmetric logarithmic.
    bool m_IsLinear = false;
    /// The result the axes of the plot were fitted to.
    std::size_t m_FittedVersion = 0;
    /// The scale the limits of the plot were last set for.
    bool m_FittedLinear = false;

    void DrawSummary(const Core::PoleZero &result);
    void DrawPlane(const Core::PoleZero &result);
    void SetupPlaneAxes(const Core::PoleZero &result, bool is_fitting);
    void DrawTable(const Core::PoleZero &result);
};

} // namespace GUI

#endif // IMCSIM_POLE_ZERO_TAB_H
