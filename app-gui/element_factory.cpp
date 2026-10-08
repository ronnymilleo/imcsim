/**
 * @file    element_factory.cpp
 * @brief   Creates the schematic element that matches a component type, and names each kind of part for the user.
 */

#include "element_factory.h"

#include "ui_elements/ui_bjt.h"
#include "ui_elements/ui_capacitor.h"
#include "ui_elements/ui_controlled_source.h"
#include "ui_elements/ui_current_source.h"
#include "ui_elements/ui_diode.h"
#include "ui_elements/ui_ground.h"
#include "ui_elements/ui_inductor.h"
#include "ui_elements/ui_mosfet.h"
#include "ui_elements/ui_op_amp.h"
#include "ui_elements/ui_resistor.h"
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

constexpr auto Parts = std::to_array<PartInfo>({
    {Core::ComponentType::Resistor, "Resistor"},
    {Core::ComponentType::Capacitor, "Capacitor"},
    {Core::ComponentType::Inductor, "Inductor"},
    {Core::ComponentType::Ground, "Ground"},
    {Core::ComponentType::VCC, "VCC supply"},
    {Core::ComponentType::VoltageSource, "Voltage source"},
    {Core::ComponentType::CurrentSource, "Current source"},
    {Core::ComponentType::Diode, "Diode"},
    {Core::ComponentType::ZenerDiode, "Zener diode"},
    {Core::ComponentType::LED, "LED"},
    {Core::ComponentType::NPN, "NPN transistor"},
    {Core::ComponentType::PNP, "PNP transistor"},
    {Core::ComponentType::NMOS, "N-channel MOSFET"},
    {Core::ComponentType::PMOS, "P-channel MOSFET"},
    {Core::ComponentType::VCVS, "VCVS (E)"},
    {Core::ComponentType::VCCS, "VCCS (G)"},
    {Core::ComponentType::CCCS, "CCCS (F)"},
    {Core::ComponentType::CCVS, "CCVS (H)"},
    {Core::ComponentType::OpAmp, "Op-amp"},
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
    case Core::ComponentType::Resistor:
        return std::make_unique<UIResistor>(position, rotation);
    case Core::ComponentType::Capacitor:
        return std::make_unique<UICapacitor>(position, rotation);
    case Core::ComponentType::Inductor:
        return std::make_unique<UIInductor>(position, rotation);
    case Core::ComponentType::Ground:
        return std::make_unique<UIGround>(position, rotation);
    case Core::ComponentType::VCC:
        return std::make_unique<UIVCC>(position, rotation);
    case Core::ComponentType::VoltageSource:
        return std::make_unique<UIVoltageSource>(position, rotation);
    case Core::ComponentType::CurrentSource:
        return std::make_unique<UICurrentSource>(position, rotation);
    case Core::ComponentType::Diode:
    case Core::ComponentType::ZenerDiode:
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
    case Core::ComponentType::OpAmp:
        return std::make_unique<UIOpAmp>(position, rotation);
    }
    return nullptr;
}

/**
 * @brief   Returns the name of a kind of part, as menus, tooltips and the part editor show it.
 * @param[in] type  Kind of part.
 * @return  Such as "Voltage source" or "N-channel MOSFET".
 */
const char *GetPartName(const Core::ComponentType type) {
    const auto part = std::ranges::find(Parts, type, &PartInfo::Type);
    return part != Parts.end() ? part->Name : Core::GetTypeName(type);
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
    for (const PartInfo &part : Parts) {
        if (NormalizeSearchText(part.Name).contains(normalized_query) ||
            NormalizeSearchText(Core::GetTypeName(part.Type)).contains(normalized_query)) {
            found.push_back(part.Type);
        }
    }
    return found;
}

} // namespace GUI
