/**
 * @file    voltage_source.cpp
 * @brief   An independent voltage source in the simulation model.
 */

#include "voltage_source.h"

namespace Core {

namespace {

constexpr double DefaultVoltage = 5.0;

} // namespace

/**
 * @brief   Creates a DC voltage source with the default voltage.
 */
VoltageSource::VoltageSource() : Component(ComponentType::VoltageSource, DefaultVoltage) {
}

/**
 * @brief   Returns whether the source is DC or AC.
 * @return  The source type chosen at construction.
 */
VoltageSource::SourceType VoltageSource::GetSourceType() const {
    return m_Type;
}

} // namespace Core
