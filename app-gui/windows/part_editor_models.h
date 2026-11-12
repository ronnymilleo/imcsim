/**
 * @file    part_editor_models.h
 * @brief   The properties of diodes, transistors and op-amps: the model they use and its parameters.
 */

#ifndef IMCSIM_PART_EDITOR_MODELS_H
#define IMCSIM_PART_EDITOR_MODELS_H

#include "base/value_field.h"
#include "circuit/components/bjt.h"
#include "circuit/components/component.h"
#include "circuit/components/diode.h"
#include "circuit/components/mosfet.h"
#include "circuit/components/op_amp.h"
#include "schematic/schematic.h"
#include <array>

namespace GUI {

/**
 * @struct  ModelFields
 * @brief   The texts being edited for the parameters of custom diodes, transistors and op-amps.
 * @details One field per member of the parameters of each.
 */
struct ModelFields {
    std::array<ValueField, 11> Diode;
    std::array<ValueField, 16> BJT;
    std::array<ValueField, 13> MOSFET;
    std::array<ValueField, 12> OpAmp;
};

// Loading
void LoadModelFields(ModelFields &fields, const Core::Component &component, ValueField &main_value);

// Drawing
void DrawDiode(Core::Diode &diode, ModelFields &fields, Schematic &schematic);
void DrawBJT(Core::BJT &bjt, ModelFields &fields, Schematic &schematic);
void DrawMOSFET(Core::MOSFET &mosfet, ModelFields &fields, Schematic &schematic);
void DrawOpAmp(Core::OpAmp &op_amp, ModelFields &fields, ValueField &main_value, Schematic &schematic);

} // namespace GUI

#endif // IMCSIM_PART_EDITOR_MODELS_H
