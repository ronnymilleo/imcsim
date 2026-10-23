/**
 * @file    voltage_controlled_switch.cpp
 * @brief   A switch that opens and closes with a voltage elsewhere in the circuit (SPICE S).
 */

#include "voltage_controlled_switch.h"

#include <format>
#include <memory>

namespace Core {

/**
 * @brief   Creates a switch that closes above 2.5 V, with 0.1 V of hysteresis, 10 mOhm closed and 1 GOhm open.
 */
VoltageControlledSwitch::VoltageControlledSwitch() : Component(ComponentType::V_SWITCH, 0.0) {
}

/**
 * @brief   Copies the component with its name, value and settings.
 * @return  An independent component of the same kind.
 */
std::unique_ptr<Component> VoltageControlledSwitch::Clone() const {
    return std::make_unique<VoltageControlledSwitch>(*this);
}

/**
 * @brief   Returns the parameters of the switch.
 * @return  Threshold, hysteresis and the two resistances.
 */
const SwitchParameters &VoltageControlledSwitch::GetParameters() const {
    return m_Parameters;
}

/**
 * @brief   Sets the parameters of the switch.
 * @param[in] parameters  New parameters; check them with IsValidParameters() first.
 */
void VoltageControlledSwitch::SetParameters(const SwitchParameters &parameters) {
    m_Parameters = parameters;
}

/**
 * @brief   Checks whether a set of switch parameters can be simulated.
 * @param[in] parameters  Candidate parameters.
 * @return  True when the hysteresis is not negative and the closed resistance is positive and below the open one.
 */
bool VoltageControlledSwitch::IsValidParameters(const SwitchParameters &parameters) {
    return parameters.Hysteresis >= 0.0 && parameters.OnResistance > 0.0 &&
           parameters.OnResistance < parameters.OffResistance;
}

/**
 * @brief   Writes the ".model" line of a switch model.
 * @param[in] spice_name  Name the switch lines refer to.
 * @param[in] parameters  Threshold, hysteresis and resistances.
 * @return  The line, newline included.
 */
std::string FormatSwitchModel(const std::string_view spice_name, const SwitchParameters &parameters) {
    return std::format(".model {} SW(VT={:.6g} VH={:.6g} RON={:.6g} ROFF={:.6g})\n", spice_name, parameters.Threshold,
                       parameters.Hysteresis, parameters.OnResistance, parameters.OffResistance);
}

} // namespace Core
