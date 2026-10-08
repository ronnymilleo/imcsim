/**
 * @file    controlled_source.cpp
 * @brief   Sources whose output follows a voltage or a current elsewhere in the circuit (SPICE E, F, G and H).
 */

#include "controlled_source.h"

#include <utility>

namespace Core {

namespace {

// Gains that give round outputs from typical controlling values: 1 V or 1 mA in, 10 V, 1 mA, 10 mA or 1 V out
double DefaultGain(const ComponentType type) {
    switch (type) {
    case ComponentType::VCVS:
    case ComponentType::CCCS:
        return 10.0;
    case ComponentType::VCCS:
        return 1e-3;
    case ComponentType::CCVS:
        return 1e3;
    case ComponentType::Resistor:
    case ComponentType::Capacitor:
    case ComponentType::Inductor:
    case ComponentType::Ground:
    case ComponentType::VCC:
    case ComponentType::VoltageSource:
    case ComponentType::CurrentSource:
    case ComponentType::Diode:
    case ComponentType::ZenerDiode:
    case ComponentType::LED:
    case ComponentType::NPN:
    case ComponentType::PNP:
    case ComponentType::NMOS:
    case ComponentType::PMOS:
    case ComponentType::OpAmp:
        break;
    }
    return 1.0;
}

} // namespace

/**
 * @brief   Creates a controlled source with a default gain and, for a current-controlled one, no controlling
 *          current yet.
 * @param[in] type  VCVS, VCCS, CCCS or CCVS.
 */
ControlledSource::ControlledSource(const ComponentType type) : Component(type, DefaultGain(type)) {
}

/**
 * @brief   Returns the current a current-controlled source follows.
 * @return  The current as the results name it, such as "R1" or "Q1.C"; empty until one is picked, and always
 *          empty for a voltage-controlled source.
 */
const std::string &ControlledSource::GetControllingCurrent() const {
    return m_ControllingCurrent;
}

/**
 * @brief   Sets the current a current-controlled source follows.
 * @param[in] current  A current as the results name it; it is checked against the circuit when it is simulated.
 */
void ControlledSource::SetControllingCurrent(std::string current) {
    m_ControllingCurrent = std::move(current);
}

/**
 * @brief   Tells whether a kind of component is a ControlledSource, so it can be cast to one.
 * @param[in] type  Kind of component.
 * @return  True for VCVS, VCCS, CCCS and CCVS.
 */
bool IsControlledSource(const ComponentType type) {
    return type == ComponentType::VCVS || type == ComponentType::VCCS || type == ComponentType::CCCS ||
           type == ComponentType::CCVS;
}

/**
 * @brief   Tells whether a kind of controlled source follows a current instead of a voltage.
 * @param[in] type  Kind of component.
 * @return  True for CCCS and CCVS, which need a controlling current.
 */
bool IsCurrentControlled(const ComponentType type) {
    return type == ComponentType::CCCS || type == ComponentType::CCVS;
}

} // namespace Core
