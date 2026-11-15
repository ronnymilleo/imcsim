/**
 * @file    plot_export_smith.h
 * @brief   Draws a Smith chart of reflection coefficients as an SVG image for the plot export.
 */

#ifndef IMCSIM_PLOT_EXPORT_SMITH_H
#define IMCSIM_PLOT_EXPORT_SMITH_H

#include "plots/plot_export.h"
#include <expected>
#include <string>

namespace GUI {

// Rendering
std::expected<std::string, std::string> RenderSmith(const ExportSmith &smith, const ExportStyle &style);

} // namespace GUI

#endif // IMCSIM_PLOT_EXPORT_SMITH_H
