/**
 * @file    plot_export_complex.h
 * @brief   Draws a Polar or Nyquist plot in the complex plane as an SVG image for the plot export.
 */

#ifndef IMCSIM_PLOT_EXPORT_COMPLEX_H
#define IMCSIM_PLOT_EXPORT_COMPLEX_H

#include "plots/plot_export.h"
#include <expected>
#include <string>

namespace GUI {

[[nodiscard]] std::expected<std::string, std::string> RenderComplexPlane(const ExportComplexPlane &plane,
                                                                         const ExportStyle &style);

} // namespace GUI

#endif // IMCSIM_PLOT_EXPORT_COMPLEX_H
