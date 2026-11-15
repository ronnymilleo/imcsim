/**
 * @file    placement_editing.h
 * @brief   Changing the mode of the editor, and placing what a mode holds in the schematic.
 */

#ifndef IMCSIM_PLACEMENT_EDITING_H
#define IMCSIM_PLACEMENT_EDITING_H

#include "schematic/block_session.h"
#include "schematic/clipboard.h"
#include "schematic/editor_mode.h"
#include "schematic/schematic.h"
#include "ui_elements/ui_element.h"
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace GUI {

// Modes
void SwitchMode(EditorMode &current, EditorMode next, Schematic &schematic, const BlockSession &blocks);
void StopEditing(Schematic &schematic, EditorMode &mode);

// Placing
std::unique_ptr<UIElement> MakePlacementPreview(const PlacingMode &mode, GridPoint position, const Schematic &schematic,
                                                const std::vector<SavedSubcircuit> &definitions);
std::optional<std::string> PlacePastedGroup(Schematic &root, BlockSession &blocks, PastedGroup &group);
std::string FindLastNet(const Schematic &schematic);

} // namespace GUI

#endif // IMCSIM_PLACEMENT_EDITING_H
