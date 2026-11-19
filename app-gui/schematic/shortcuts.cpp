/**
 * @file    shortcuts.cpp
 * @brief   The keys of the commands of the editor, where they apply, and how menus and tooltips name them.
 */

#include "schematic/shortcuts.h"

#include <algorithm>
#include <array>
#include <format>

namespace GUI {

namespace {

using Scope = ShortcutScope;

/// In the order the editor reads them in a frame.
constexpr std::array SHORTCUTS = {
    EditorShortcut{.Command = EditorCommand::NEW,
                   .Chord = ImGuiMod_Ctrl | ImGuiKey_N,
                   .Label = IMCSIM_SHORTCUT "N",
                   .Scope = Scope::DOCUMENT},
    EditorShortcut{.Command = EditorCommand::OPEN,
                   .Chord = ImGuiMod_Ctrl | ImGuiKey_O,
                   .Label = IMCSIM_SHORTCUT "O",
                   .Scope = Scope::DOCUMENT},
    EditorShortcut{.Command = EditorCommand::SAVE,
                   .Chord = ImGuiMod_Ctrl | ImGuiKey_S,
                   .Label = IMCSIM_SHORTCUT "S",
                   .Scope = Scope::DOCUMENT},
    EditorShortcut{.Command = EditorCommand::SAVE_AS,
                   .Chord = ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S,
                   .Label = IMCSIM_SHORTCUT "Shift+S",
                   .Scope = Scope::DOCUMENT},
    EditorShortcut{.Command = EditorCommand::RUN, .Chord = ImGuiKey_F5, .Label = "F5", .Scope = Scope::DOCUMENT},
    EditorShortcut{.Command = EditorCommand::QUIT,
                   .Chord = ImGuiMod_Ctrl | ImGuiKey_Q,
                   .Label = IMCSIM_SHORTCUT "Q",
                   .Scope = Scope::DOCUMENT},
    EditorShortcut{.Command = EditorCommand::SELECT_ALL,
                   .Chord = ImGuiMod_Ctrl | ImGuiKey_A,
                   .Label = IMCSIM_SHORTCUT "A",
                   .Scope = Scope::EDITING},
    EditorShortcut{.Command = EditorCommand::UNDO,
                   .Chord = ImGuiMod_Ctrl | ImGuiKey_Z,
                   .Label = IMCSIM_SHORTCUT "Z",
                   .Scope = Scope::EDITING},
    EditorShortcut{.Command = EditorCommand::REDO,
                   .Chord = ImGuiMod_Ctrl | ImGuiKey_Y,
                   .Label = IMCSIM_SHORTCUT "Y",
                   .Scope = Scope::EDITING},
    // The redo of most macOS programs, which the menu does not show
    EditorShortcut{.Command = EditorCommand::REDO,
                   .Chord = ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z,
                   .Label = nullptr,
                   .Scope = Scope::EDITING},
    EditorShortcut{.Command = EditorCommand::FIT, .Chord = ImGuiKey_Home, .Label = "Home", .Scope = Scope::VIEW},
    EditorShortcut{.Command = EditorCommand::COPY,
                   .Chord = ImGuiMod_Ctrl | ImGuiKey_C,
                   .Label = IMCSIM_SHORTCUT "C",
                   .Scope = Scope::CANVAS},
    EditorShortcut{.Command = EditorCommand::CUT,
                   .Chord = ImGuiMod_Ctrl | ImGuiKey_X,
                   .Label = IMCSIM_SHORTCUT "X",
                   .Scope = Scope::CANVAS},
    EditorShortcut{.Command = EditorCommand::PASTE,
                   .Chord = ImGuiMod_Ctrl | ImGuiKey_V,
                   .Label = IMCSIM_SHORTCUT "V",
                   .Scope = Scope::CANVAS},
    EditorShortcut{.Command = EditorCommand::ROTATE, .Chord = ImGuiKey_R, .Label = "R", .Scope = Scope::SELECTING},
    EditorShortcut{.Command = EditorCommand::MIRROR, .Chord = ImGuiKey_M, .Label = "M", .Scope = Scope::SELECTING},
    EditorShortcut{.Command = EditorCommand::FLIP,
                   .Chord = ImGuiMod_Shift | ImGuiKey_M,
                   .Label = "Shift+M",
                   .Scope = Scope::SELECTING},
    EditorShortcut{.Command = EditorCommand::ROTATE_KEEP_WIRES,
                   .Chord = ImGuiMod_Alt | ImGuiKey_R,
                   .Label = "Alt+R",
                   .Scope = Scope::SELECTING},
    EditorShortcut{.Command = EditorCommand::MIRROR_KEEP_WIRES,
                   .Chord = ImGuiMod_Alt | ImGuiKey_M,
                   .Label = "Alt+M",
                   .Scope = Scope::SELECTING},
    EditorShortcut{.Command = EditorCommand::FLIP_KEEP_WIRES,
                   .Chord = ImGuiMod_Alt | ImGuiMod_Shift | ImGuiKey_M,
                   .Label = "Alt+Shift+M",
                   .Scope = Scope::SELECTING},
    EditorShortcut{.Command = EditorCommand::DELETE_SELECTION,
                   .Chord = ImGuiKey_Delete,
                   .Label = "Del",
                   .Scope = Scope::SELECTING},
};

} // namespace

/**
 * @brief   Every shortcut of the editor, in the order the editor reads them; a command may have more than one.
 * @return  The table, which lives as long as the program.
 */
std::span<const EditorShortcut> GetEditorShortcuts() {
    return SHORTCUTS;
}

/**
 * @brief   How a menu item or a tooltip names the keys of a command, from the same table the editor reads them from.
 * @param[in] command  Any command.
 * @return  The label of its first shown shortcut, such as "Cmd+Z", or null for a command without one, which
 *          ImGui::MenuItem() takes as no shortcut.
 */
const char *GetShortcutLabel(const EditorCommand command) {
    const auto found = std::ranges::find_if(SHORTCUTS, [command](const EditorShortcut &shortcut) {
        return shortcut.Command == command && shortcut.Label != nullptr;
    });
    return found == SHORTCUTS.end() ? nullptr : found->Label;
}

/**
 * @brief   Names a command with its keys after it, as a tooltip shows it.
 * @param[in] name     What the command does, such as "Undo".
 * @param[in] command  The command, whose keys are named.
 * @return  Text such as "Undo (Cmd+Z)", or the name alone for a command without a shortcut.
 */
std::string NameWithShortcut(const std::string_view name, const EditorCommand command) {
    const char *label = GetShortcutLabel(command);
    if (label == nullptr) {
        return std::string(name);
    }
    return std::format("{} ({})", name, label);
}

/**
 * @brief   Whether the keys of a scope work this frame.
 * @param[in] scope  Where the keys work.
 * @param[in] state  The block, the text field and the focus of this frame.
 * @return  True when the editor should read them; never for the keys of the selecting mode, which reads them itself.
 */
bool IsShortcutScopeOpen(const ShortcutScope scope, const ShortcutState &state) {
    switch (scope) {
    case ShortcutScope::DOCUMENT:
        return !state.IsEditingBlock;
    case ShortcutScope::EDITING:
        return !state.IsTyping;
    case ShortcutScope::VIEW:
        return true;
    case ShortcutScope::CANVAS:
        return state.IsEditorFocused && !state.IsTyping;
    case ShortcutScope::SELECTING:
    default:
        return false;
    }
}

/**
 * @brief   How ImGui::Shortcut() routes the keys of a scope.
 * @param[in] scope  Where the keys work.
 * @return  The global route for the keys that work whichever window is in front, so they reach the editor while
 *          another window such as Properties has the focus; otherwise the default route, to the focused window.
 */
ImGuiInputFlags GetShortcutRoute(const ShortcutScope scope) {
    switch (scope) {
    case ShortcutScope::DOCUMENT:
    case ShortcutScope::EDITING:
        return ImGuiInputFlags_RouteGlobal;
    case ShortcutScope::VIEW:
    case ShortcutScope::CANVAS:
    case ShortcutScope::SELECTING:
    default:
        return ImGuiInputFlags_None;
    }
}

} // namespace GUI
