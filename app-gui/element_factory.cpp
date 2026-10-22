/**
 * @file    element_factory.cpp
 * @brief   Creates the schematic element that matches a component type, and names each kind of part for the user.
 */

#include "element_factory.h"

#include "ui_elements/ui_bjt.h"
#include "ui_elements/ui_capacitor.h"
#include "ui_elements/ui_clock.h"
#include "ui_elements/ui_controlled_source.h"
#include "ui_elements/ui_controlled_switch.h"
#include "ui_elements/ui_current_source.h"
#include "ui_elements/ui_diode.h"
#include "ui_elements/ui_ground.h"
#include "ui_elements/ui_inductor.h"
#include "ui_elements/ui_logic_input.h"
#include "ui_elements/ui_logic_part.h"
#include "ui_elements/ui_mosfet.h"
#include "ui_elements/ui_net_label.h"
#include "ui_elements/ui_op_amp.h"
#include "ui_elements/ui_potentiometer.h"
#include "ui_elements/ui_resistor.h"
#include "ui_elements/ui_switch.h"
#include "ui_elements/ui_transformer.h"
#include "ui_elements/ui_vcc.h"
#include "ui_elements/ui_voltage_source.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <string>

namespace GUI {

namespace {

/**
 * @struct  PartInfo
 * @brief   How a kind of part is named to the user.
 */
struct PartInfo {
    Core::ComponentType Type;
    const char *Name;
};

constexpr auto PARTS = std::to_array<PartInfo>({
    {Core::ComponentType::RESISTOR, "Resistor"},
    {Core::ComponentType::CAPACITOR, "Capacitor"},
    {Core::ComponentType::INDUCTOR, "Inductor"},
    {Core::ComponentType::GROUND, "Ground"},
    {Core::ComponentType::VCC, "VCC supply"},
    {Core::ComponentType::VOLTAGE_SOURCE, "Voltage source"},
    {Core::ComponentType::CURRENT_SOURCE, "Current source"},
    {Core::ComponentType::DIODE, "Diode"},
    {Core::ComponentType::ZENER_DIODE, "Zener diode"},
    {Core::ComponentType::LED, "LED"},
    {Core::ComponentType::NPN, "NPN transistor"},
    {Core::ComponentType::PNP, "PNP transistor"},
    {Core::ComponentType::NMOS, "N-channel MOSFET"},
    {Core::ComponentType::PMOS, "P-channel MOSFET"},
    {Core::ComponentType::VCVS, "VCVS (E)"},
    {Core::ComponentType::VCCS, "VCCS (G)"},
    {Core::ComponentType::CCCS, "CCCS (F)"},
    {Core::ComponentType::CCVS, "CCVS (H)"},
    {Core::ComponentType::OP_AMP, "Op-amp"},
    {Core::ComponentType::NET_LABEL, "Net label"},
    {Core::ComponentType::POTENTIOMETER, "Potentiometer"},
    {Core::ComponentType::SWITCH, "Switch"},
    {Core::ComponentType::PUSH_BUTTON, "Push button"},
    {Core::ComponentType::V_SWITCH, "Voltage-controlled switch"},
    {Core::ComponentType::RELAY, "Relay"},
    {Core::ComponentType::TRANSFORMER, "Transformer"},
    {Core::ComponentType::AND, "AND gate"},
    {Core::ComponentType::OR, "OR gate"},
    {Core::ComponentType::NOT, "NOT gate (inverter)"},
    {Core::ComponentType::NAND, "NAND gate"},
    {Core::ComponentType::NOR, "NOR gate"},
    {Core::ComponentType::XOR, "XOR gate"},
    {Core::ComponentType::XNOR, "XNOR gate"},
    {Core::ComponentType::BUFFER, "Buffer"},
    {Core::ComponentType::D_FLIP_FLOP, "D flip-flop"},
    {Core::ComponentType::JK_FLIP_FLOP, "JK flip-flop"},
    {Core::ComponentType::T_FLIP_FLOP, "T flip-flop"},
    {Core::ComponentType::SR_FLIP_FLOP, "SR flip-flop"},
    {Core::ComponentType::CLOCK, "Clock"},
    {Core::ComponentType::LOGIC_INPUT, "Logic input"},
});

// Lowercase letters and digits only, so "opamp" finds "Op-amp" and "vcc" finds "VCC supply"
std::string NormalizeSearchText(const std::string_view text) {
    std::string normalized;
    for (const char character : text) {
        if (std::isalnum(static_cast<unsigned char>(character)) != 0) {
            normalized += static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        }
    }
    return normalized;
}

} // namespace

/**
 * @brief   Creates an element, with its simulation component, for a component type.
 * @param[in] type      Kind of component.
 * @param[in] position  Grid position in world units.
 * @param[in] rotation  Orientation on the grid.
 * @return  The new element; its component starts unnamed and with the default value.
 */
std::unique_ptr<UIElement> CreateElement(const Core::ComponentType type, const GridPoint position,
                                         const Rotation rotation) {
    switch (type) {
    case Core::ComponentType::RESISTOR:
        return std::make_unique<UIResistor>(position, rotation);
    case Core::ComponentType::CAPACITOR:
        return std::make_unique<UICapacitor>(position, rotation);
    case Core::ComponentType::INDUCTOR:
        return std::make_unique<UIInductor>(position, rotation);
    case Core::ComponentType::GROUND:
        return std::make_unique<UIGround>(position, rotation);
    case Core::ComponentType::VCC:
        return std::make_unique<UIVCC>(position, rotation);
    case Core::ComponentType::VOLTAGE_SOURCE:
        return std::make_unique<UIVoltageSource>(position, rotation);
    case Core::ComponentType::CURRENT_SOURCE:
        return std::make_unique<UICurrentSource>(position, rotation);
    case Core::ComponentType::DIODE:
    case Core::ComponentType::ZENER_DIODE:
    case Core::ComponentType::LED:
        return std::make_unique<UIDiode>(type, position, rotation);
    case Core::ComponentType::NPN:
    case Core::ComponentType::PNP:
        return std::make_unique<UIBJT>(type, position, rotation);
    case Core::ComponentType::NMOS:
    case Core::ComponentType::PMOS:
        return std::make_unique<UIMOSFET>(type, position, rotation);
    case Core::ComponentType::VCVS:
    case Core::ComponentType::VCCS:
    case Core::ComponentType::CCCS:
    case Core::ComponentType::CCVS:
        return std::make_unique<UIControlledSource>(type, position, rotation);
    case Core::ComponentType::OP_AMP:
        return std::make_unique<UIOpAmp>(position, rotation);
    case Core::ComponentType::NET_LABEL:
        return std::make_unique<UINetLabel>(position, rotation);
    case Core::ComponentType::POTENTIOMETER:
        return std::make_unique<UIPotentiometer>(position, rotation);
    case Core::ComponentType::SWITCH:
    case Core::ComponentType::PUSH_BUTTON:
        return std::make_unique<UISwitch>(type, position, rotation);
    case Core::ComponentType::V_SWITCH:
    case Core::ComponentType::RELAY:
        return std::make_unique<UIControlledSwitch>(type, position, rotation);
    case Core::ComponentType::TRANSFORMER:
        return std::make_unique<UITransformer>(position, rotation);
    case Core::ComponentType::AND:
    case Core::ComponentType::OR:
    case Core::ComponentType::NOT:
    case Core::ComponentType::NAND:
    case Core::ComponentType::NOR:
    case Core::ComponentType::XOR:
    case Core::ComponentType::XNOR:
    case Core::ComponentType::BUFFER:
    case Core::ComponentType::D_FLIP_FLOP:
    case Core::ComponentType::JK_FLIP_FLOP:
    case Core::ComponentType::T_FLIP_FLOP:
    case Core::ComponentType::SR_FLIP_FLOP:
        return std::make_unique<UILogicPart>(type, position, rotation);
    case Core::ComponentType::CLOCK:
        return std::make_unique<UIClock>(position, rotation);
    case Core::ComponentType::LOGIC_INPUT:
        return std::make_unique<UILogicInput>(position, rotation);
    }
    return nullptr;
}

/**
 * @brief   Returns the name of a kind of part, as menus, tooltips and the part editor show it.
 * @param[in] type  Kind of part.
 * @return  Such as "Voltage source" or "N-channel MOSFET".
 */
const char *GetPartName(const Core::ComponentType type) {
    const auto part = std::ranges::find(PARTS, type, &PartInfo::Type);
    return part != PARTS.end() ? part->Name : Core::GetTypeName(type);
}

/**
 * @brief   Finds the parts whose name contains a query, ignoring case, spaces and punctuation.
 * @param[in] query  Text typed by the user, such as "mos", "opamp" or "vcvs".
 * @return  Matching part types, in the order the part list names them; every part for an empty query.
 * @note    The type name counts too, so "nmos" finds the N-channel MOSFET.
 */
std::vector<Core::ComponentType> FindParts(const std::string_view query) {
    const std::string normalized_query = NormalizeSearchText(query);
    std::vector<Core::ComponentType> found;
    for (const PartInfo &part : PARTS) {
        if (NormalizeSearchText(part.Name).contains(normalized_query) ||
            NormalizeSearchText(Core::GetTypeName(part.Type)).contains(normalized_query)) {
            found.push_back(part.Type);
        }
    }
    return found;
}

} // namespace GUI
