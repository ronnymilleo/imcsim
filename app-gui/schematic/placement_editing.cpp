/**
 * @file    placement_editing.cpp
 * @brief   Changing the mode of the editor, and placing what a mode holds in the schematic.
 */

#include "schematic/placement_editing.h"

#include "circuit/components/net_label.h"
#include "circuit/components/subcircuit.h"
#include "schematic/block_nesting.h"
#include "schematic/element_factory.h"
#include "schematic/selection_editing.h"
#include "schematic/subcircuit_pins.h"
#include <algorithm>
#include <cstddef>
#include <format>
#include <ranges>
#include <set>
#include <utility>
#include <variant>

namespace GUI {

/**
 * @brief   Leaves the mode the editor is in for another, as the toolbar, the part picker, a key or the end of a mode
 *          asks.
 * @details Selecting keeps the selection, which a paste or a placed label has just made; every other mode starts with
 *          an empty selection, ending a drag in progress first. Wire mode asked again keeps the wire in progress, and
 *          placing starts over with the new part. A block cannot be placed where it would contain itself.
 * @param[in,out] current    Mode the editor is in.
 * @param[in]     next       Mode to start.
 * @param[in,out] schematic  Schematic on show, whose selection a mode other than selecting clears.
 * @param[in]     blocks     The open blocks, which say whether a block can be placed where the editor is.
 */
void SwitchMode(EditorMode &current, EditorMode next, Schematic &schematic, const BlockSession &blocks) {
    if (std::holds_alternative<SelectingMode>(next)) {
        if (!IsSelecting(current)) {
            current = SelectingMode{};
        }
        return;
    }
    if (const auto *placing = std::get_if<PlacingMode>(&next);
        placing != nullptr && !placing->Block.empty() && !blocks.CanPlaceBlock(placing->Block)) {
        return;
    }
    ClearSelection(schematic, current);
    if (std::holds_alternative<WiringMode>(next) && std::holds_alternative<WiringMode>(current)) {
        return;
    }
    current = std::move(next);
}

/**
 * @brief   Before the schematic is cleared or replaced: a drag or paste in progress would point at parts that are gone.
 * @details Probing measures whatever schematic is shown, so it goes on.
 */
void StopEditing(Schematic &schematic, EditorMode &mode) {
    EndDrag(schematic, mode);
    if (!IsSelecting(mode) && !std::holds_alternative<ProbingMode>(mode)) {
        mode = SelectingMode{};
    }
}

/**
 * @brief   The part a placing mode would put at a point, as it is drawn following the cursor.
 * @details A new label starts with the name of the last one, so the same net is a click away, and a block takes the
 *          pins of its definition.
 * @param[in] mode         The placing mode.
 * @param[in] position     Grid point the part sits at.
 * @param[in] schematic    Schematic on show, whose newest label names the new one.
 * @param[in] definitions  The blocks of the document.
 * @return  The part, or null when the block being placed is no longer defined.
 */
std::unique_ptr<UIElement> MakePlacementPreview(const PlacingMode &mode, const GridPoint position,
                                                const Schematic &schematic,
                                                const std::vector<SavedSubcircuit> &definitions) {
    auto preview = CreateElement(mode.Type, position, mode.Orientation);
    preview->SetMirrored(mode.IsMirrored);
    if (mode.Type == Core::ComponentType::NET_LABEL) {
        static_cast<Core::NetLabel &>(preview->GetComponent()).SetNet(FindLastNet(schematic));
    }
    if (!mode.Block.empty()) {
        const auto definition = std::ranges::find(definitions, mode.Block, &SavedSubcircuit::Name);
        if (definition == definitions.end()) {
            return nullptr;
        }
        ApplyDefinitionPins(static_cast<Core::SubcircuitInstance &>(preview->GetComponent()), *definition);
    }
    return preview;
}

/**
 * @brief   Adds the pasted group to the schematic shown and selects it, after its blocks and models join the document.
 * @details Inside an open block, a group that places a block containing the one being edited is refused, as it would
 *          make a block that contains itself.
 * @param[in,out] root    The document, which takes the blocks and models the group brings.
 * @param[in,out] blocks  The open blocks, whose copies of the definitions are refreshed; the group lands in the shown
 *                        schematic.
 * @param[in,out] group   The group, whose parts and wires are moved into the schematic.
 * @return  Why the group was refused, or none once it was placed.
 */
std::optional<std::string> PlacePastedGroup(Schematic &root, BlockSession &blocks, PastedGroup &group) {
    // The blocks and models a group brings belong to the document, wherever it is pasted. Models go first, so a
    // renamed one is renamed in the blocks too before they join
    root.MergeLibraryModels(std::move(group.Models), group.Elements, group.Subcircuits);
    root.MergeSubcircuits(std::move(group.Subcircuits), group.Elements);
    if (blocks.IsOpen()) {
        blocks.SyncDefinitions();
        for (const std::string &used : ListUsedBlocks(group.Elements)) {
            if (!blocks.CanPlaceBlock(used)) {
                return std::format("{} would contain itself, directly or through another block", used);
            }
        }
    }
    Schematic &schematic = blocks.GetShown();
    const std::size_t first_element = schematic.GetElements().size();
    const std::size_t first_wire = schematic.GetWires().size();
    for (auto &element : group.Elements) {
        schematic.AddElement(std::move(element));
    }
    for (const UIWire &wire : group.Wires) {
        schematic.AddWire(wire.GetStart(), wire.GetEnd());
    }
    std::set<std::size_t> elements;
    for (std::size_t index = first_element; index < schematic.GetElements().size(); ++index) {
        elements.insert(index);
    }
    std::set<std::size_t> wires;
    for (std::size_t index = first_wire; index < schematic.GetWires().size(); ++index) {
        wires.insert(index);
    }
    schematic.SetSelection(std::move(elements), std::move(wires));
    return std::nullopt;
}

/**
 * @brief   The name of the newest label that has one, or none.
 */
std::string FindLastNet(const Schematic &schematic) {
    for (const auto &element : std::views::reverse(schematic.GetElements())) {
        if (element->GetComponent().GetType() != Core::ComponentType::NET_LABEL) {
            continue;
        }
        if (const std::string &net = static_cast<const Core::NetLabel &>(element->GetComponent()).GetNet();
            !net.empty()) {
            return net;
        }
    }
    return {};
}

} // namespace GUI
