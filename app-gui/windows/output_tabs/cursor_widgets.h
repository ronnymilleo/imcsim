/**
 * @file    cursor_widgets.h
 * @brief   The measurement cursors of the plots of the Output window, their readout and the playback bar of a
 * transient.
 */

#ifndef IMCSIM_CURSOR_WIDGETS_H
#define IMCSIM_CURSOR_WIDGETS_H

#include "plots/plot_types.h"
#include "results/playback.h"
#include "windows/output_tabs/plot_widgets.h"
#include <vector>

namespace GUI {

// Cursors
void DrawCursorReadout(const std::vector<double> &xs, const char *x_unit, bool logarithmic,
                       const std::vector<PlottedTrace> &plotted);
void DrawCursors(PlotCursors &cursors, bool logarithmic);
PlotSpan MeasuredSpan(const PlotCursors &cursors, const PlotSpan &view);
float GetCursorToggleWidth();
void DrawCursorToggle(PlotCursors &cursors, float right_edge);
void DrawCursorCheckbox(PlotCursors &cursors);

// Playback
void DrawPlaybackBar(TransientPlayback &playback, double from, double to);

} // namespace GUI

#endif // IMCSIM_CURSOR_WIDGETS_H
