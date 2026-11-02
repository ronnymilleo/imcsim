/**
 * @file    relay.cpp
 * @brief   An electromechanical relay: a coil and the contact it closes.
 */

#include "relay.h"
#include <memory>

namespace Core {

namespace {

constexpr double OPEN_CONTACT_RESISTANCE = 1e9;

} // namespace

/**
 * @brief   Creates a relay for a 5 V coil of 70 Ohm and 50 mH that pulls in at 3.5 V and drops out at 1 V.
 */
Relay::Relay() : Component(ComponentType::RELAY, 0.0) {
}

/**
 * @brief   Copies the component with its name, value and settings.
 * @return  An independent component of the same kind.
 */
std::unique_ptr<Component> Relay::Clone() const {
    return std::make_unique<Relay>(*this);
}

/**
 * @brief   Returns the figures of the relay.
 * @return  Coil, thresholds and contact resistance.
 */
const RelayParameters &Relay::GetParameters() const {
    return m_Parameters;
}

/**
 * @brief   Sets the figures of the relay.
 * @param[in] parameters  New figures; check them with IsValidParameters() first.
 */
void Relay::SetParameters(const RelayParameters &parameters) {
    m_Parameters = parameters;
}

/**
 * @brief   Checks whether a set of relay figures can be simulated.
 * @param[in] parameters  Candidate figures.
 * @return  True when the coil has resistance and inductance, the contact has a resistance, and the pull-in voltage
 *          is above the drop-out voltage, which is not negative.
 */
bool Relay::IsValidParameters(const RelayParameters &parameters) {
    return parameters.CoilResistance > 0.0 && parameters.CoilInductance > 0.0 && parameters.ContactResistance > 0.0 &&
           parameters.DropOutVoltage >= 0.0 && parameters.PullInVoltage > parameters.DropOutVoltage;
}

/**
 * @brief   Works out the switch that is the contact of the relay.
 * @return  A switch that closes at the pull-in voltage and opens at the drop-out voltage, halfway between them
 *          plus or minus the hysteresis.
 */
SwitchParameters Relay::GetContactParameters() const {
    return {.Threshold = (m_Parameters.PullInVoltage + m_Parameters.DropOutVoltage) / 2.0,
            .Hysteresis = (m_Parameters.PullInVoltage - m_Parameters.DropOutVoltage) / 2.0,
            .OnResistance = m_Parameters.ContactResistance,
            .OffResistance = OPEN_CONTACT_RESISTANCE};
}

} // namespace Core
