/**
 * @file    editor_status_bar.h
 * @brief   The line under the canvas of the editor: the state of the last run, the hint of the mode and the cursor.
 */

#ifndef IMCSIM_EDITOR_STATUS_BAR_H
#define IMCSIM_EDITOR_STATUS_BAR_H

#include "analysis/analysis_controls.h"
#include "base/helpers.h"
#include "schematic/editor_mode.h"
#include "schematic/schematic.h"
#include "simulation/simulation_results.h"
#include <optional>

namespace GUI {

// Drawing
void DrawStatusBar(const EditorMode &mode, Schematic &schematic, const AnalysisControls &controls,
                   const std::optional<Core::Transient> &transient, const ViewTransform &view, bool hovered);

} // namespace GUI

#endif // IMCSIM_EDITOR_STATUS_BAR_H
