/**
 * @file    shortcuts_tests.cpp
 * @brief   Tests for the keys of the commands of the editor, where they apply, and how menus and tooltips name them.
 */

#include "schematic/shortcuts.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <string>

namespace {

/**
 * @brief   The label of a command as text, empty for a command without one, so a failed check prints it.
 */
std::string LabelOf(const GUI::EditorCommand command) {
    const char *label = GUI::GetShortcutLabel(command);
    return label == nullptr ? std::string() : std::string(label);
}

} // namespace

TEST_CASE("Menus name the keys of a command from the table the editor reads them from", "[shortcuts]") {
    CHECK(LabelOf(GUI::EditorCommand::UNDO) == IMCSIM_SHORTCUT "Z");
    CHECK(LabelOf(GUI::EditorCommand::SAVE_AS) == IMCSIM_SHORTCUT "Shift+S");
    CHECK(LabelOf(GUI::EditorCommand::FIT) == "Home");
    CHECK(LabelOf(GUI::EditorCommand::FLIP_KEEP_WIRES) == "Alt+Shift+M");
    CHECK(LabelOf(GUI::EditorCommand::DELETE_SELECTION) == "Del");
    CHECK(LabelOf(GUI::EditorCommand::RUN) == "F5");
}

TEST_CASE("A command with two chords is named by the one the menu shows", "[shortcuts]") {
    // Redo answers Ctrl+Shift+Z too, but the menu shows Ctrl+Y
    const auto redo_chords =
        std::ranges::count(GUI::GetEditorShortcuts(), GUI::EditorCommand::REDO, &GUI::EditorShortcut::Command);
    CHECK(redo_chords == 2);
    CHECK(LabelOf(GUI::EditorCommand::REDO) == IMCSIM_SHORTCUT "Y");
}

TEST_CASE("The keys of a command come from the table, and a command without keys has none", "[shortcuts]") {
    CHECK(GUI::GetShortcutChord(GUI::EditorCommand::RUN) == ImGuiKey_F5);
    CHECK(GUI::GetShortcutChord(GUI::EditorCommand::REDO) == (ImGuiMod_Ctrl | ImGuiKey_Y));
    CHECK(GUI::GetShortcutChord(GUI::EditorCommand::IMPORT) == ImGuiKey_None);
}

TEST_CASE("A command without keys has no label, and its tooltip is its name alone", "[shortcuts]") {
    CHECK(GUI::GetShortcutLabel(GUI::EditorCommand::IMPORT) == nullptr);
    CHECK(GUI::GetShortcutLabel(GUI::EditorCommand::EDIT_BLOCK) == nullptr);
    CHECK(GUI::NameWithShortcut("Import", GUI::EditorCommand::IMPORT) == "Import");
    CHECK(GUI::NameWithShortcut("Undo", GUI::EditorCommand::UNDO) == "Undo (" IMCSIM_SHORTCUT "Z)");
    CHECK(GUI::NameWithShortcut("Fit the schematic in view", GUI::EditorCommand::FIT)
          == "Fit the schematic in view (Home)");
}

TEST_CASE("No two shortcuts of one scope share a chord", "[shortcuts]") {
    const auto shortcuts = GUI::GetEditorShortcuts();
    for (auto first = shortcuts.begin(); first != shortcuts.end(); ++first) {
        for (auto second = first + 1; second != shortcuts.end(); ++second) {
            INFO("Chord " << first->Chord);
            CHECK_FALSE((first->Scope == second->Scope && first->Chord == second->Chord));
        }
    }
}

TEST_CASE("The document keys stop inside a block and the editing keys while a text field takes them", "[shortcuts]") {
    using GUI::ShortcutScope;
    const GUI::ShortcutState in_block = {.IsEditingBlock = true, .IsTyping = false, .IsEditorFocused = true};
    CHECK_FALSE(GUI::IsShortcutScopeOpen(ShortcutScope::DOCUMENT, in_block));
    CHECK(GUI::IsShortcutScopeOpen(ShortcutScope::EDITING, in_block));

    const GUI::ShortcutState typing = {.IsEditingBlock = false, .IsTyping = true, .IsEditorFocused = true};
    CHECK(GUI::IsShortcutScopeOpen(ShortcutScope::DOCUMENT, typing));
    CHECK_FALSE(GUI::IsShortcutScopeOpen(ShortcutScope::EDITING, typing));
    CHECK_FALSE(GUI::IsShortcutScopeOpen(ShortcutScope::CANVAS, typing));
    // Home leaves the text fields of other windows alone through the focus, not through this state
    CHECK(GUI::IsShortcutScopeOpen(ShortcutScope::VIEW, typing));
}

TEST_CASE("The clipboard keys need the editor focused, and the selecting keys are never read from the table",
          "[shortcuts]") {
    using GUI::ShortcutScope;
    const GUI::ShortcutState elsewhere = {.IsEditingBlock = false, .IsTyping = false, .IsEditorFocused = false};
    CHECK_FALSE(GUI::IsShortcutScopeOpen(ShortcutScope::CANVAS, elsewhere));
    const GUI::ShortcutState focused = {.IsEditingBlock = false, .IsTyping = false, .IsEditorFocused = true};
    CHECK(GUI::IsShortcutScopeOpen(ShortcutScope::CANVAS, focused));
    CHECK_FALSE(GUI::IsShortcutScopeOpen(ShortcutScope::SELECTING, focused));
}

TEST_CASE("Only the document and editing keys take the global route", "[shortcuts]") {
    using GUI::ShortcutScope;
    CHECK(GUI::GetShortcutRoute(ShortcutScope::DOCUMENT) == ImGuiInputFlags_RouteGlobal);
    CHECK(GUI::GetShortcutRoute(ShortcutScope::EDITING) == ImGuiInputFlags_RouteGlobal);
    CHECK(GUI::GetShortcutRoute(ShortcutScope::VIEW) == ImGuiInputFlags_None);
    CHECK(GUI::GetShortcutRoute(ShortcutScope::CANVAS) == ImGuiInputFlags_None);
}

TEST_CASE("The keys that start a mode are named from the table", "[shortcuts]") {
    CHECK(GUI::NameWithShortcut("Wire", GUI::EditorCommand::START_WIRING) == "Wire (W)");
    CHECK(GUI::NameWithShortcut("Probe", GUI::EditorCommand::START_PROBING) == "Probe (P)");
    CHECK(GUI::NameWithShortcut("Select", GUI::EditorCommand::START_SELECTING) == "Select (Esc)");
    CHECK(GUI::GetShortcutChord(GUI::EditorCommand::PLACE_NET_LABEL) == ImGuiKey_L);
}
