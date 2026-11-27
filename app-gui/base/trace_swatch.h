/**
 * @file    trace_swatch.h
 * @brief   A row of a trace list: its checkbox and a short line in the color and style the trace is plotted with.
 */

#ifndef IMCSIM_TRACE_SWATCH_H
#define IMCSIM_TRACE_SWATCH_H

#include "imgui.h"

namespace GUI {

/**
 * @enum    TraceStyle
 * @brief   How a trace is drawn on the plots, which its swatch copies.
 */
enum class TraceStyle {
    SOLID,  ///< A voltage.
    DASHED, ///< A current.
    HEAVY,  ///< A math channel, heavier than the traces it comes from.
};

bool DrawTraceCheckbox(const char *label, bool *is_shown, ImU32 color, TraceStyle style);

} // namespace GUI

#endif // IMCSIM_TRACE_SWATCH_H
