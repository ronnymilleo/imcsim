/**
 * @file    plot_export_style.cpp
 * @brief   Colors and sizes that the SVG images of the plot export share, for the cartesian panels and the Smith chart.
 */

#include "plots/plot_export_style.h"

namespace GUI {

/**
 * @brief   Estimates how wide a text is, since the viewer picks the font that renders the SVG.
 * @param[in] text       Text to measure.
 * @param[in] font_size  Font size in pixels.
 * @return  Width in pixels, from the average advance of a sans-serif character.
 */
double EstimateTextWidth(const std::string_view text, const double font_size) {
    return CHARACTER_WIDTH * font_size * static_cast<double>(text.size());
}

} // namespace GUI
