/**
 * @file    initial_voltage.cpp
 * @brief   A marker that sets the voltage its node starts a transient at, in the simulation model.
 */

#include "circuit/components/initial_voltage.h"

#include <memory>

namespace Core {

namespace {

constexpr double DEFAULT_VOLTAGE = 2.5;

} // namespace

/**
 * @brief   Creates an initial voltage of 2.5 V.
 */
InitialVoltage::InitialVoltage() : Component(ComponentType::INITIAL_VOLTAGE, DEFAULT_VOLTAGE) {
}

/**
 * @brief   Copies the marker with its voltage.
 * @return  An independent component of the same kind.
 */
std::unique_ptr<Component> InitialVoltage::Clone() const {
    return std::make_unique<InitialVoltage>(*this);
}

} // namespace Core
