/**
 * @file    complex_plane_plot.h
 * @brief   Interactive ImPlot widget for Polar and Nyquist diagrams in the complex plane.
 */

#ifndef IMCSIM_COMPLEX_PLANE_PLOT_H
#define IMCSIM_COMPLEX_PLANE_PLOT_H

#include "imgui.h"
#include "plots/ac_complex_view.h"
#include "plots/complex_plane.h"
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace GUI {

/**
 * @class   ComplexPlanePlot
 * @brief   Draws complex frequency loci on equal axes over a Polar or Nyquist grid, with direction arrows and hover
 *          readout.
 */
class ComplexPlanePlot {
public:
    /// Formats the tooltip text for a hovered `(locus_index, sample_index)` pair.
    using HoverFormatter = std::function<std::string(std::size_t locus_index, std::size_t sample_index)>;

    void Draw(const char *id,
              ComplexPlaneKind kind,
              const std::vector<ComplexLocus> &loci,
              bool mirror_negative_frequencies,
              bool has_legend,
              bool &is_fitting,
              const HoverFormatter &describe_hover);

private:
    float m_FittedWidth = 0.0f;
    float m_FittedHeight = 0.0f;

    bool TakeResized(ImVec2 room);
};

} // namespace GUI

#endif // IMCSIM_COMPLEX_PLANE_PLOT_H
