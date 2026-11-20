/**
 * @file    shortcuts.h
 * @brief   The keys of the commands of the editor, where they apply, and how menus and tooltips name them.
 */

#ifndef IMCSIM_SHORTCUTS_H
#define IMCSIM_SHORTCUTS_H

#include "imgui.h"
#include "schematic/editor_command.h"
#include <span>
#include <string>
#include <string_view>

// ImGui reads Cmd as ImGuiMod_Ctrl on macOS (io.ConfigMacOSXBehaviors), so the same shortcuts are labeled with the
// key pressed there. A macro, so labels stay string literals: IMCSIM_SHORTCUT "N" is "Ctrl+N" or "Cmd+N"
#ifdef __APPLE__
#define IMCSIM_SHORTCUT "Cmd+"
#else
#define IMCSIM_SHORTCUT "Ctrl+"
#endif

namespace GUI {

/**
 * @enum    ShortcutScope
 * @brief   Where the keys of a command work, and which part of the editor reads them.
 */
enum class ShortcutScope {
    /// Whichever window is in front, outside a block, since they act on the whole document.
    DOCUMENT,
    /// Whichever window is in front, unless a text field takes the keys, so it still undoes its own typing.
    EDITING,
    /// While the editor has the focus, since text fields elsewhere use the same keys.
    VIEW,
    /// While the editor window itself has the focus and no text field takes the keys.
    CANVAS,
    /// Read by the modes themselves, whatever other modifier is held; listed here for their labels.
    SELECTING
};

/**
 * @struct  ShortcutState
 * @brief   What decides whether the keys of a scope work this frame.
 */
struct ShortcutState {
    /// Whether a block is open.
    bool IsEditingBlock = false;
    /// Whether a text field takes the keys.
    bool IsTyping = false;
    /// Whether the editor window itself has the focus.
    bool IsEditorFocused = false;
};

/**
 * @struct  EditorShortcut
 * @brief   The keys of one command, where they work, and how menus and tooltips name them.
 */
struct EditorShortcut {
    EditorCommand Command;
    ImGuiKeyChord Chord;
    /// The keys as a menu item shows them, or null for a second chord that is not shown.
    const char *Label;
    ShortcutScope Scope;
};

// Table
std::span<const EditorShortcut> GetEditorShortcuts();
const char *GetShortcutLabel(EditorCommand command);
ImGuiKeyChord GetShortcutChord(EditorCommand command);
std::string NameWithShortcut(std::string_view name, EditorCommand command);

// Scopes
bool IsShortcutScopeOpen(ShortcutScope scope, const ShortcutState &state);
ImGuiInputFlags GetShortcutRoute(ShortcutScope scope);

} // namespace GUI

#endif // IMCSIM_SHORTCUTS_H
