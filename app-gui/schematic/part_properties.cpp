/**
 * @file    part_properties.cpp
 * @brief   Which fields the properties of a part have: a value, a model, parameters, a name or a state.
 */

#include "schematic/part_properties.h"

#include "circuit/components/bjt.h"
#include "circuit/components/diode.h"
#include "circuit/components/logic_part.h"
#include "circuit/components/mosfet.h"
#include "circuit/components/op_amp.h"
#include "circuit/components/source.h"
#include "circuit/components/switch.h"
#include <algorithm>
#include <array>

namespace GUI {

/**
 * @brief   Parts with something to edit: a value, a model, a name, a state or parameters; ground has nothing.
 */
bool HasPropertyFields(const Core::Component &component) {
    const Core::ComponentType type = component.GetType();
    if (component.HasValue() || Core::IsSwitch(type) || Core::IsLogicPart(type) || Core::IsSubcircuitPin(type)) {
        return true;
    }
    static constexpr auto OTHER_TYPES = std::to_array(
        {Core::ComponentType::NET_LABEL, Core::ComponentType::SUBCIRCUIT, Core::ComponentType::V_SWITCH,
         Core::ComponentType::RELAY, Core::ComponentType::FUSE, Core::ComponentType::CLOCK,
         Core::ComponentType::LOGIC_INPUT, Core::ComponentType::LOGIC_OUTPUT, Core::ComponentType::SEVEN_SEGMENT,
         Core::ComponentType::BEHAVIORAL_VOLTAGE, Core::ComponentType::BEHAVIORAL_CURRENT});
    return std::ranges::contains(OTHER_TYPES, type);
}

/**
 * @brief   Whether a model sets the part: diodes, transistors and op-amps.
 */
bool HasModelChoice(const Core::ComponentType type) {
    return Core::IsDiode(type) || Core::IsBJT(type) || Core::IsMOSFET(type) || type == Core::ComponentType::OP_AMP;
}

/**
 * @brief   Value fields show for parts with a value, and for parts with a model only while they are custom.
 */
bool ShowsValueFields(const Core::Component &component) {
    const Core::ComponentType type = component.GetType();
    if (Core::IsDiode(type)) {
        return static_cast<const Core::Diode &>(component).IsCustom();
    }
    if (Core::IsBJT(type)) {
        return static_cast<const Core::BJT &>(component).IsCustom();
    }
    if (Core::IsMOSFET(type)) {
        return static_cast<const Core::MOSFET &>(component).IsCustom();
    }
    // The ideal op-amp has its gain-bandwidth product to edit
    if (type == Core::ComponentType::OP_AMP) {
        const auto &op_amp = static_cast<const Core::OpAmp &>(component);
        return op_amp.IsCustom() || op_amp.IsIdeal();
    }
    // The switches that follow a voltage, the logic parts, a clock and the logic inputs and outputs have only their
    // parameters
    if (type == Core::ComponentType::V_SWITCH || type == Core::ComponentType::RELAY ||
        type == Core::ComponentType::FUSE || Core::IsLogicPart(type) || type == Core::ComponentType::CLOCK ||
        type == Core::ComponentType::LOGIC_INPUT || Core::IsLogicDisplay(type)) {
        return true;
    }
    return component.HasValue();
}

/**
 * @brief   Tells whether a part has a main value that typing can start editing.
 * @return  True for parts with a value (resistors, capacitors, inductors, VCC, controlled sources), DC sources and
 *          ideal op-amps; false for parts set by a model or a waveform.
 */
bool CanStartTypingValue(const Core::Component &component) {
    const Core::ComponentType type = component.GetType();
    if (Core::IsSource(type)) {
        return static_cast<const Core::Source &>(component).GetSourceType() == Core::Source::SourceType::DC;
    }
    if (type == Core::ComponentType::OP_AMP) {
        return static_cast<const Core::OpAmp &>(component).IsIdeal();
    }
    return component.HasValue() && !HasModelChoice(type);
}

} // namespace GUI
