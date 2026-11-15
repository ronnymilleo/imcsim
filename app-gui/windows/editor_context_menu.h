/**
 * @file    editor_context_menu.h
 * @brief   The menu a right click opens on the schematic: what the part, the wire or the empty canvas under it can do.
 */

#ifndef IMCSIM_EDITOR_CONTEXT_MENU_H
#define IMCSIM_EDITOR_CONTEXT_MENU_H

#include "analysis/analysis_controls.h"
#include "base/helpers.h"
#include "schematic/canvas_view.h"
#include "schematic/editor_mode.h"
#include "schematic/probing.h"
#include "schematic/schematic.h"
#include "schematic/simulation_settings.h"
#include "windows/part_picker.h"
#include "windows/part_popover.h"
#include <optional>

namespace GUI {

/**
 * @struct  ContextMenuServices
 * @brief   What the items of the context menu act on, which the editor owns.
 */
struct ContextMenuServices {
    Schematic &Shown;
    EditorMode &Mode;
    AnalysisControls &Controls;
    PartPicker &Picker;
    CanvasView &View;
    PartPopover &Popover;
};

/**
 * @class   EditorContextMenu
 * @brief   Selects what a right click lands on and opens the menu of what it can do.
 */
class EditorContextMenu {
public:
    // Opening
    void HandleRightClick(Schematic &schematic, EditorMode &mode, const ViewTransform &view, float zoom, bool hovered,
                          PartPopover &popover);

    // Drawing
    bool Draw(const ContextMenuServices &services, Analysis selected_analysis, bool is_editing_block);

private:
    /// What the right click was on, for the measure item of the menu.
    std::optional<MeasurementTarget> m_Target;
};

} // namespace GUI

#endif // IMCSIM_EDITOR_CONTEXT_MENU_H
