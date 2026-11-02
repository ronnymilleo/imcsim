/**
 * @file    voltage_source.cpp
 * @brief   An independent voltage source in the simulation model.
 */

#include "voltage_source.h"
#include <memory>

namespace Core {

namespace {

constexpr double DEFAULT_VOLTAGE = 5.0;
constexpr double DEFAULT_AMPLITUDE = 1.0;
constexpr double DEFAULT_PULSE_HIGH = 5.0;

} // namespace

/**
 * @brief   Creates a 5 V DC voltage source.
 * @note    The sine starts at 1 V and the pulse goes from 0 to 5 V, ready for switching type.
 */
VoltageSource::VoltageSource()
    : Source(ComponentType::VOLTAGE_SOURCE, DEFAULT_VOLTAGE, DEFAULT_AMPLITUDE, DEFAULT_PULSE_HIGH) {
}

/**
 * @brief   Copies the component with its name, value and settings.
 * @return  An independent component of the same kind.
 */
std::unique_ptr<Component> VoltageSource::Clone() const {
    return std::make_unique<VoltageSource>(*this);
}

} // namespace Core
