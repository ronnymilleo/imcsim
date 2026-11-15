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
    /// The net name being typed for a net label.
    std::array<char, 40> Net{};
    /// Reference impedance of the pin marker that is an S-parameter port.
    ValueField PortReference;
    /// One per parameter of the block an instance places.
    std::vector<ValueField> Instance;
    /// Whether the next Draw() focuses the net name.
    bool IsNetFocusRequested = false;
};

// Loading
void LoadBlockFields(BlockFields &fields, const Core::Component &component, const Schematic &schematic);

// Drawing
void DrawNetLabel(Core::NetLabel &label, BlockFields &fields, Schematic &schematic);
bool DrawSubcircuit(Core::SubcircuitInstance &instance, BlockFields &fields, Schematic &schematic);

} // namespace GUI

#endif // IMCSIM_PART_EDITOR_BLOCKS_H
