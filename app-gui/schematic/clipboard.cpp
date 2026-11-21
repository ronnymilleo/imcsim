/**
 * @file    clipboard.cpp
 * @brief   Copies parts and wires to text and back, and moves a group of them that is waiting to be pasted.
 */

#include "schematic/clipboard.h"

#include "circuit/components/subcircuit.h"
#include "schematic/block_nesting.h"
#include "schematic/group_editing.h"
#include "schematic/schematic_file.h"
#include "schematic/selection.h"
#include <algorithm>
#include <iterator>
#include <set>
#include <string>
#include <vector>

namespace GUI {

namespace {

GridPoint FindGroupPivot(const PastedGroup &group) {
    return FindPivot(group.Elements, group.Wires, PickAll(group.Elements, group.Wires));
}

/**
 * @brief   Copies the blocks that some parts place, with the blocks inside them at any depth.
 * @details Every definition is read back together, so the instances inside the copies find each other.
 * @param[in] subcircuits  Block definitions of the schematic.
 * @param[in] parts        Parts being copied.
 * @return  Deep copies of the definitions the parts need, in the order of the schematic; none when no part is a block.
 */
std::vector<SavedSubcircuit> CopyNeededDefinitions(const std::span<const SavedSubcircuit> subcircuits,
                                                   const std::vector<const UIElement *> &parts) {
    std::set<std::string> placed;
    for (const UIElement *part : parts) {
        if (part->GetComponent().GetType() == Core::ComponentType::SUBCIRCUIT) {
            placed.insert(static_cast<const Core::SubcircuitInstance &>(part->GetComponent()).GetDefinitionName());
        }
    }
    std::vector<SavedSubcircuit> copied;
    if (placed.empty()) {
        return copied;
    }
    auto loaded = LoadSchematic(SaveSchematic({}, {}, {}, {}, subcircuits));
    if (!loaded) {
        return copied;
    }
    for (SavedSubcircuit &definition : loaded->Subcircuits) {
        const bool is_needed = std::ranges::any_of(placed, [&](const std::string &name) {
            return ContainsBlock(subcircuits, name, definition.Name);
        });
        if (is_needed) {
            copied.push_back(std::move(definition));
        }
    }
    return copied;
}

/**
 * @brief   Copies the library models that some parts use, those inside the blocks copied with them included.
 * @param[in] models       Library models of the schematic.
 * @param[in] parts        Parts being copied.
 * @param[in] definitions  Blocks copied with the parts.
 * @return  The models any of them names, in the order of the schematic.
 */
std::vector<Core::LibraryModel> CopyNeededModels(const std::span<const Core::LibraryModel> models,
                                                 const std::vector<const UIElement *> &parts,
                                                 const std::vector<SavedSubcircuit> &definitions) {
    std::set<std::string> needed;
    for (const UIElement *part : parts) {
        needed.insert(part->GetComponent().GetLibraryModel());
    }
    for (const SavedSubcircuit &definition : definitions) {
        for (const auto &element : definition.Elements) {
            needed.insert(element->GetComponent().GetLibraryModel());
        }
    }
    std::vector<Core::LibraryModel> copied;
    std::ranges::copy_if(models, std::back_inserter(copied), [&](const Core::LibraryModel &model) {
        return needed.contains(model.Name);
    });
    return copied;
}

} // namespace

/**
 * @brief   Writes the selected parts and wires as text for the clipboard.
 * @param[in] elements     Parts of the schematic.
 * @param[in] wires        Wires of the schematic.
 * @param[in] selection    What to copy.
 * @param[in] subcircuits  Block definitions of the schematic; those the copied instances use go with them, with the
 *                         blocks inside them.
 * @param[in] models       Library models of the schematic; those the copied parts use go with them.
 * @return  A schematic of just those items, in the format of the files, so it pastes into another copy of the program
 *          as well as this one.
 */
std::string CopySelection(const std::vector<std::unique_ptr<UIElement>> &elements,
                          const std::vector<UIWire> &wires,
                          const Selection &selection,
                          const std::span<const SavedSubcircuit> subcircuits,
                          const std::span<const Core::LibraryModel> models) {
    const SelectedItems selected = CollectSelected(elements, wires, selection);
    std::vector<SavedSubcircuit> copied_definitions = CopyNeededDefinitions(subcircuits, selected.Elements);
    const std::vector<Core::LibraryModel> copied_models =
        CopyNeededModels(models, selected.Elements, copied_definitions);
    return SaveParts(selected.Elements, selected.Wires, copied_definitions, copied_models);
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
 * @brief   Reads a schematic, from the clipboard or from a file, as parts and wires to place.
 * @param[in] text  Text of the clipboard, or contents of a schematic file.
 * @return  The group, held by the middle of its box, or the reason the text cannot be placed: not a schematic, or a
 *          schematic with nothing in it. Settings and measurements in the text are ignored. Parts and wires that
 *          cannot be read are left out and reported in the warnings of the group.
 */
std::expected<PastedGroup, std::string> ParseCopiedSchematic(const std::string_view text) {
    auto loaded = LoadSchematic(text);
    if (!loaded) {
        return std::unexpected(loaded.error());
    }
    return MakePastedGroup(std::move(*loaded));
}

/**
 * @brief   Takes the parts and wires of a loaded schematic as a group to place, such as an example.
 * @param[in] loaded  A schematic read by LoadSchematic() or LoadExample().
 * @return  The group, held by the middle of its box, or an error when the schematic has no parts or wires. The
 *          settings and measurements are dropped, and the warnings of the loader go with the group.
 */
std::expected<PastedGroup, std::string> MakePastedGroup(LoadedSchematic loaded) {
    if (loaded.Elements.empty() && loaded.Wires.empty()) {
        return std::unexpected("The schematic has no parts or wires");
    }
    PastedGroup group;
    group.Elements = std::move(loaded.Elements);
    group.Wires = std::move(loaded.Wires);
    group.Subcircuits = std::move(loaded.Subcircuits);
    group.Models = std::move(loaded.Models);
    group.Warnings = std::move(loaded.Warnings);
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
