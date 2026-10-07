/**
 * @file    voltage_source.cpp
 * @brief   An independent voltage source in the simulation model.
 */

#include "voltage_source.h"

namespace Core {

namespace {

constexpr double DefaultVoltage = 5.0;
constexpr double DefaultAmplitude = 1.0;
constexpr double DefaultPulseHigh = 5.0;

} // namespace

/**
 * @brief   Creates a 5 V DC voltage source.
 * @note    The sine starts at 1 V and the pulse goes from 0 to 5 V, ready for switching type.
 */
VoltageSource::VoltageSource()
    : Source(ComponentType::VoltageSource, DefaultVoltage, DefaultAmplitude, DefaultPulseHigh) {
}

} // namespace Core
