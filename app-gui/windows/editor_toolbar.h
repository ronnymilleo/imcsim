/**
 * @file    editor_toolbar.h
 * @brief   The icon toolbar above the schematic: tools, history, view, parts and the run controls.
 */

#ifndef IMCSIM_EDITOR_TOOLBAR_H
#define IMCSIM_EDITOR_TOOLBAR_H

#include "analysis/analysis_controls.h"
#include "analysis/analysis_settings_panel.h"
#include "components/component.h"
#include "document/schematic.h"
#include "editing/editor_mode.h"
#include "preferences.h"
#include "ui_elements/ui_element.h"
#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace GUI {

/**
 * @enum    ToolbarCommand
 * @brief   A button of the toolbar that acts on the editor without changing its mode.
 */
enum class ToolbarCommand {
    UNDO,
    REDO,
    FIT
};

/**
 * @struct  ToolbarRequest
 * @brief   What the buttons pressed this frame ask the editor to do: start a mode, or run a command.
 */
struct ToolbarRequest {
    std::optional<EditorMode> Mode;
    std::optional<ToolbarCommand> Command;
};

/**
 * @class   EditorToolbar
 * @brief   One row of icon buttons whose groups wrap onto new lines when the window is narrow.
 * @details The tool and part buttons only ask for a mode, which the editor starts; the run controls drive the analyses
 *          directly, since they belong to the whole document. Related parts share a button that places the last one
 *          picked from its list, and so do the ready logic blocks.
 */
class EditorToolbar {
public:
    EditorToolbar(AnalysisControls &controls, AnalysisSettingsPanel &settings_panel, const Schematic &root_schematic,
                  const Preferences &preferences);

    ToolbarRequest Draw(const EditorMode &mode, const Schematic &schematic, bool is_editing_block);

private:
    AnalysisControls &m_Controls;
    AnalysisSettingsPanel &m_SettingsPanel;
    /// The document, whose simulation settings name the analysis Run starts.
    const Schematic &m_RootSchematic;
    const Preferences &m_Preferences;
    /// Part each toolbar group places, the last one picked from its list.
    std::vector<Core::ComponentType> m_PartGroupChoices;
    /// An instance of each ready block, read once, which its button and its row draw as their icon.
    std::vector<std::unique_ptr<UIElement>> m_ReadyBlockIcons;
    /// The ready block the button places, the last one picked from its list.
    std::size_t m_ReadyBlockChoice = 0;

    // Parts
    bool DrawPartButton(Core::ComponentType type, const EditorMode &mode);
    std::optional<Core::ComponentType> DrawPartGroup(std::size_t index, const EditorMode &mode);
    std::optional<EditorMode> DrawReadyBlockGroup();
    void DrawReadyBlockIcon(std::size_t index, ImVec2 center) const;

    // Simulation
    void DrawRunControls();
    void DrawAnalysisButton(float run_left);
    void DrawLiveToggle();
};

} // namespace GUI

#endif // IMCSIM_EDITOR_TOOLBAR_H
