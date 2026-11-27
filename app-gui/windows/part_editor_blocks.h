/**
 * @file    part_editor_blocks.h
 * @brief   The properties of net labels, pin markers and ports, and of the instances of blocks.
 */

#ifndef IMCSIM_PART_EDITOR_BLOCKS_H
#define IMCSIM_PART_EDITOR_BLOCKS_H

#include "base/value_field.h"
#include "circuit/components/net_label.h"
#include "circuit/components/subcircuit.h"
#include "circuit/components/subcircuit_pin.h"
#include "schematic/schematic.h"
#include <array>
#include <vector>

namespace GUI {

/**
 * @struct  BlockFields
 * @brief   The texts being edited for a net label, a pin marker and a block instance.
 */
struct BlockFields {
    std::array<char, 40> Net{};       ///< The net name being typed for a net label.
    ValueField PortReference;         ///< Reference impedance of the pin marker that is an S-parameter port.
    std::vector<ValueField> Instance; ///< One per parameter of the block an instance places.
    bool IsNetFocusRequested = false; ///< Whether the next Draw() focuses the net name.
};

// Loading
void LoadBlockFields(BlockFields &fields, const Core::Component &component, const Schematic &schematic);

// Drawing
void DrawNetLabel(Core::NetLabel &label, BlockFields &fields, Schematic &schematic);
bool DrawSubcircuit(Core::SubcircuitInstance &instance, BlockFields &fields, Schematic &schematic);

} // namespace GUI

#endif // IMCSIM_PART_EDITOR_BLOCKS_H
