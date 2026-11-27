/**
 * @file    part_properties.cpp
 * @brief   Which fields the properties of a part have: a value, a model, parameters, a name or a state.
 */

#include "schematic/part_properties.h"

#include "circuit/components/bjt.h"
#include "circuit/components/diode.h"
#include "circuit/components/igbt.h"
#include "circuit/components/logic_part.h"
#include "circuit/components/mosfet.h"
#include "circuit/components/op_amp.h"
#include "circuit/components/source.h"
#include "circuit/components/switch.h"
#include <algorithm>
#include <array>

namespace GUI {

namespace {

/// The switches that follow a voltage, the thyristors, a clock and a logic input, which have only their parameters.
constexpr auto PARAMETER_ONLY_TYPES = std::to_array({Core::ComponentType::V_SWITCH,
                                                     Core::ComponentType::RELAY,
                                                     Core::ComponentType::FUSE,
                                                     Core::ComponentType::THYRISTOR,
                                                     Core::ComponentType::TRIAC,
                                                     Core::ComponentType::GTO,
                                                     Core::ComponentType::DIAC,
                                                     Core::ComponentType::CLOCK,
                                                     Core::ComponentType::LOGIC_INPUT});

/**
 * @brief   Whether the part is set by its parameters alone: the types above, the logic parts and the logic displays.
 */
bool HasParametersOnly(const Core::ComponentType type) {
    return std::ranges::contains(PARAMETER_ONLY_TYPES, type) || Core::IsLogicPart(type) || Core::IsLogicDisplay(type);
}

} // namespace

/**
 * @brief   Parts with something to edit: a value, a model, a name, a state or parameters; ground has nothing.
 * @param[in] component  Component to inspect.
 * @return  True when the part editor has at least one property field for this component.
 */
bool HasPropertyFields(const Core::Component &component) {
    const Core::ComponentType type = component.GetType();
    if (component.HasValue() || Core::IsSwitch(type) || Core::IsLogicPart(type) || Core::IsSubcircuitPin(type)) {
        return true;
    }
    static constexpr auto OTHER_TYPES = std::to_array({Core::ComponentType::NET_LABEL,
                                                       Core::ComponentType::SUBCIRCUIT,
                                                       Core::ComponentType::V_SWITCH,
                                                       Core::ComponentType::RELAY,
                                                       Core::ComponentType::FUSE,
                                                       Core::ComponentType::THYRISTOR,
                                                       Core::ComponentType::TRIAC,
                                                       Core::ComponentType::GTO,
                                                       Core::ComponentType::DIAC,
                                                       Core::ComponentType::CLOCK,
                                                       Core::ComponentType::LOGIC_INPUT,
                                                       Core::ComponentType::LOGIC_OUTPUT,
                                                       Core::ComponentType::SEVEN_SEGMENT,
                                                       Core::ComponentType::BEHAVIORAL_VOLTAGE,
                                                       Core::ComponentType::BEHAVIORAL_CURRENT});
    return std::ranges::contains(OTHER_TYPES, type);
}

/**
 * @brief   Whether a model sets the part: diodes, transistors, IGBTs and op-amps.
 * @param[in] type  Component type to inspect.
 * @return  True when the part offers ready, library or custom device models.
 */
bool HasModelChoice(const Core::ComponentType type) {
    return Core::IsDiode(type) || Core::IsBJT(type) || Core::IsMOSFET(type) || Core::IsIGBT(type)
        || type == Core::ComponentType::OP_AMP;
}

/**
 * @brief   Value fields show for parts with a value, and for parts with a model only while they are custom.
 * @param[in] component  Component to inspect.
 * @return  True when numeric parameter or value inputs are shown for the component in its current state.
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
    if (Core::IsIGBT(type)) {
        return static_cast<const Core::IGBT &>(component).IsCustom();
    }
    // The ideal op-amp has its gain-bandwidth product to edit
    if (type == Core::ComponentType::OP_AMP) {
        const auto &op_amp = static_cast<const Core::OpAmp &>(component);
        return op_amp.IsCustom() || op_amp.IsIdeal();
    }
    return HasParametersOnly(type) || component.HasValue();
}

/**
 * @brief   Tells whether a part has a main value that typing can start editing.
 * @param[in] component  Component to inspect.
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
