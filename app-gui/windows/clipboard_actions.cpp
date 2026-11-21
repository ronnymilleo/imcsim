/**
 * @file    clipboard_actions.cpp
 * @brief   Copy, cut and paste of the editor through the clipboard of the system.
 */

#include "windows/clipboard_actions.h"

#include "imgui.h"
#include <utility>

namespace GUI {

/**
 * @brief   Writes the selection as text in the format of the files, so another copy of the program can paste it.
 * @param[in] schematic  Schematic on show, with the blocks and models its parts use.
 * @return  The text, or none when nothing is selected.
 */
std::optional<std::string> WriteSelectionText(const Schematic &schematic) {
    if (!schematic.HasSelection()) {
        return std::nullopt;
    }
    return CopySelection(schematic.GetElements(),
                         schematic.GetWires(),
                         schematic.GetSelection(),
                         schematic.GetSubcircuits(),
                         schematic.GetLibraryModels());
}

/**
 * @brief   Puts the selection on the clipboard, as WriteSelectionText() writes it; nothing selected leaves the
 *          clipboard alone.
 * @param[in] schematic  Schematic on show.
 */
void CopySelectionToClipboard(const Schematic &schematic) {
    if (const std::optional<std::string> text = WriteSelectionText(schematic)) {
        ImGui::SetClipboardText(text->c_str());
    }
}

/**
 * @brief   Puts the selection on the clipboard, as CopySelectionToClipboard() does, and removes it from the schematic.
 * @param[in,out] schematic  Schematic on show.
 */
void CutSelectionToClipboard(Schematic &schematic) {
    if (!schematic.HasSelection()) {
        return;
    }
    CopySelectionToClipboard(schematic);
    schematic.DeleteSelection();
}

/**
 * @brief   Reads the clipboard as a group to paste.
 * @return  The group, or none when the clipboard is empty or holds text that is not a schematic.
 */
std::optional<PastedGroup> ReadClipboardGroup() {
    const char *text = ImGui::GetClipboardText();
    if (text == nullptr) {
        return std::nullopt;
    }
    auto group = ParseCopiedSchematic(text);
    if (!group) {
        return std::nullopt;
    }
    return std::move(*group);
}

} // namespace GUI
