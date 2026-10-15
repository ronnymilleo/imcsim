/**
 * @file    clipboard.h
 * @brief   Copies parts and wires to text and back, and moves a group of them that is waiting to be pasted.
 */

#ifndef IMCSIM_CLIPBOARD_H
#define IMCSIM_CLIPBOARD_H

#include "helpers.h"
#include "schematic_file.h"
#include "selection.h"
#include "ui_elements/ui_element.h"
#include "ui_elements/ui_wire.h"
#include "wire_editing.h"
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace GUI {

/**
 * @struct  PastedGroup
 * @brief   Parts and wires read from the clipboard or from a file, waiting to be placed.
 * @details Pivot is the point the group is held by: the middle of its box, which follows the cursor. Warnings lists
 *          what could not be read as it was, as the file loader reports it.
 */
struct PastedGroup {
    std::vector<std::unique_ptr<UIElement>> Elements;
    std::vector<UIWire> Wires;
    GridPoint Pivot;
    std::vector<std::string> Warnings;
};

std::string CopySelection(const std::vector<std::unique_ptr<UIElement>> &elements, const std::vector<UIWire> &wires,
                          const Selection &selection);
bool LooksLikeCopiedSchematic(std::string_view text);
std::expected<PastedGroup, std::string> ParseCopiedSchematic(std::string_view text);
std::expected<PastedGroup, std::string> MakePastedGroup(LoadedSchematic loaded);
void MovePivotTo(PastedGroup &group, GridPoint target);
void TransformGroup(PastedGroup &group, const PointTransform &transform);

} // namespace GUI

#endif // IMCSIM_CLIPBOARD_H
