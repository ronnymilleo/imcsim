/**
 * @file    clipboard_actions.h
 * @brief   Copy, cut and paste of the editor through the clipboard of the system.
 */

#ifndef IMCSIM_CLIPBOARD_ACTIONS_H
#define IMCSIM_CLIPBOARD_ACTIONS_H

#include "schematic/clipboard.h"
#include "schematic/schematic.h"
#include <optional>
#include <string>

namespace GUI {

// Copying
[[nodiscard]] std::optional<std::string> WriteSelectionText(const Schematic &schematic);
void CopySelectionToClipboard(const Schematic &schematic);
void CutSelectionToClipboard(Schematic &schematic);

// Pasting
[[nodiscard]] std::optional<PastedGroup> ReadClipboardGroup();

} // namespace GUI

#endif // IMCSIM_CLIPBOARD_ACTIONS_H
