/**
 * @file    clipboard.cpp
 * @brief   Copies parts and wires to text and back, and moves a group of them that is waiting to be pasted.
 */

#include "clipboard.h"

#include "group_editing.h"
#include "schematic_file.h"

namespace GUI {

namespace {

GridPoint FindGroupPivot(const PastedGroup &group) {
    std::set<std::size_t> elements;
    std::set<std::size_t> wires;
    for (std::size_t index = 0; index < group.Elements.size(); ++index) {
        elements.insert(index);
    }
    for (std::size_t index = 0; index < group.Wires.size(); ++index) {
        wires.insert(index);
    }
    return FindPivot(group.Elements, elements, group.Wires, wires);
}

} // namespace

/**
 * @brief   Writes the selected parts and wires as text for the clipboard.
 * @param[in] elements   Parts of the schematic.
 * @param[in] wires      Wires of the schematic.
 * @param[in] selection  What to copy.
 * @return  A schematic of just those items, in the format of the files, so it pastes into another copy of the program
 *          as well as this one.
 */
std::string CopySelection(const std::vector<std::unique_ptr<UIElement>> &elements, const std::vector<UIWire> &wires,
                          const Selection &selection) {
    std::vector<const UIElement *> copied_elements;
    for (const std::size_t index : selection.Elements) {
        copied_elements.push_back(elements[index].get());
    }
    std::vector<UIWire> copied_wires;
    for (const std::size_t index : selection.Wires) {
        copied_wires.push_back(wires[index]);
    }
    return SaveParts(copied_elements, copied_wires);
}

/**
 * @brief   Tells quickly whether some text may be a copied schematic, without reading it.
 * @param[in] text  Text of the clipboard.
 * @return  True when the text names the schematic format, so the Paste command can be offered.
 */
bool LooksLikeCopiedSchematic(const std::string_view text) {
    return text.contains("imcsim-schematic");
}

/**
 * @brief   Reads text from the clipboard as parts and wires to paste.
 * @param[in] text  Text of the clipboard.
 * @return  The group, held by the middle of its box, or the reason the text cannot be pasted: not a schematic, or a
 *          schematic with nothing in it. Settings and measurements in the text are ignored, as are parts and wires
 *          that cannot be read.
 */
std::expected<PastedGroup, std::string> ParseCopiedSchematic(const std::string_view text) {
    auto loaded = LoadSchematic(text);
    if (!loaded) {
        return std::unexpected(loaded.error());
    }
    if (loaded->Elements.empty() && loaded->Wires.empty()) {
        return std::unexpected("There is nothing to paste");
    }
    PastedGroup group;
    group.Elements = std::move(loaded->Elements);
    group.Wires = std::move(loaded->Wires);
    group.Pivot = FindGroupPivot(group);
    return group;
}

/**
 * @brief   Moves a group so it is held by a point.
 * @param[in,out] group  Group to move.
 * @param[in] target     Where its pivot goes.
 */
void MovePivotTo(PastedGroup &group, const GridPoint target) {
    const GridPoint delta = target - group.Pivot;
    if (delta == GridPoint{}) {
        return;
    }
    for (const auto &element : group.Elements) {
        element->SetPosition(element->GetPosition() + delta);
    }
    for (UIWire &wire : group.Wires) {
        wire = UIWire(wire.GetStart() + delta, wire.GetEnd() + delta);
    }
    group.Pivot = target;
}

/**
 * @brief   Turns or mirrors a group about its pivot, as the selection is turned.
 * @param[in,out] group  Group to change; its pivot stays where it is.
 * @param[in] transform  A quarter turn or one of the mirrors; its own pivot is ignored.
 */
void TransformGroup(PastedGroup &group, const PointTransform &transform) {
    PointTransform about_pivot = transform;
    about_pivot.Pivot = group.Pivot;
    for (const auto &element : group.Elements) {
        TransformElement(*element, about_pivot);
    }
    for (UIWire &wire : group.Wires) {
        wire = UIWire(about_pivot.Apply(wire.GetStart()), about_pivot.Apply(wire.GetEnd()));
    }
}

} // namespace GUI
