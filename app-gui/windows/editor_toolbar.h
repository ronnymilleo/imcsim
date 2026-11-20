/**
 * @file    editor_toolbar.h
 * @brief   The command bar under the menu: tools, history and view, the part search, and the run controls at its right.
 */

#ifndef IMCSIM_EDITOR_TOOLBAR_H
#define IMCSIM_EDITOR_TOOLBAR_H

#include "analysis/analysis_controls.h"
#include "analysis/analysis_settings_panel.h"
#include "circuit/components/component.h"
#include "schematic/editor_command.h"
#include "schematic/editor_mode.h"
#include "schematic/schematic.h"
#include "windows/part_picker.h"
#include <optional>
#include <vector>

namespace GUI {

/**
 * @struct  ToolbarRequest
 * @brief   What the buttons pressed this frame ask the editor to do: start a mode, or run a command.
 */
struct ToolbarRequest {
    std::optional<EditorMode> Mode;
    /// What a button that acts without changing the mode asks for: undo, redo or fit.
    std::optional<EditorCommand> Command;
};

/**
 * @class   EditorToolbar
 * @brief   The command bar: one fixed row across the window, under the menu bar.
 * @details The tool buttons only ask for a mode, which the editor starts, and so does the part search; the run controls
 *          drive the analyses directly, since they belong to the whole document. Run sits at the right end whatever
 *          the width of the window, and the search shrinks first when room runs short.
 */
class EditorToolbar {
public:
    EditorToolbar(AnalysisControls &controls, AnalysisSettingsPanel &settings_panel, const Schematic &root_schematic);

    ToolbarRequest Draw(const EditorMode &mode,
                        const Schematic &schematic,
                        bool is_editing_block,
                        PartPicker &search,
                        const std::vector<const SavedSubcircuit *> &blocks);

private:
    AnalysisControls &m_Controls;
    AnalysisSettingsPanel &m_SettingsPanel;
    /// The document, whose simulation settings name the analysis Run starts.
    const Schematic &m_RootSchematic;

    // Simulation
    void DrawRunControls(float run_left, float row_top, bool is_editing_block);
    void DrawRunButton();
    void DrawAnalysisButton();
    void DrawLiveToggle();
};

} // namespace GUI

#endif // IMCSIM_EDITOR_TOOLBAR_H
