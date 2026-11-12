/**
 * @file    part_editor_electromechanical.h
 * @brief   The properties of switches, relays, fuses and transformers.
 */

#ifndef IMCSIM_PART_EDITOR_ELECTROMECHANICAL_H
#define IMCSIM_PART_EDITOR_ELECTROMECHANICAL_H

#include "base/value_field.h"
#include "circuit/components/component.h"
#include "circuit/components/fuse.h"
#include "circuit/components/relay.h"
#include "circuit/components/switch.h"
#include "circuit/components/transformer.h"
#include "circuit/components/voltage_controlled_switch.h"
#include "schematic/schematic.h"
#include <array>

namespace GUI {

/**
 * @struct  ElectromechanicalFields
 * @brief   The texts being edited for the parameters of the switches that follow a voltage, relays, fuses and
 *          transformers.
 * @details One field per member of each set of parameters.
 */
struct ElectromechanicalFields {
    /// One per member of Core::SwitchParameters.
    std::array<ValueField, 4> VoltageSwitch;
    /// One per member of Core::RelayParameters.
    std::array<ValueField, 5> Relay;
    /// One per member of Core::FuseParameters.
    std::array<ValueField, 4> Fuse;
    /// One per member of Core::TransformerParameters.
    std::array<ValueField, 3> Transformer;
};

// Loading
void LoadElectromechanicalFields(ElectromechanicalFields &fields, const Core::Component &component);

// Drawing
void DrawSwitch(Core::Switch &contact, Schematic &schematic);
void DrawControlledSwitch(Core::VoltageControlledSwitch &controlled, ElectromechanicalFields &fields,
                          Schematic &schematic);
void DrawRelay(Core::Relay &relay, ElectromechanicalFields &fields, Schematic &schematic);
void DrawFuse(Core::Fuse &fuse, ElectromechanicalFields &fields, Schematic &schematic);
void DrawTransformer(Core::Transformer &transformer, ElectromechanicalFields &fields, Schematic &schematic);

} // namespace GUI

#endif // IMCSIM_PART_EDITOR_ELECTROMECHANICAL_H
