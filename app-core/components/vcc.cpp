/**
 * @file    vcc.cpp
 * @brief   A positive supply rail in the simulation model.
 */

#include "vcc.h"

namespace Core {

namespace {

constexpr double DefaultVoltage = 5.0;

} // namespace

/**
 * @brief   Creates a supply rail at 5 V.
 */
VCC::VCC() : Component(ComponentType::VCC, DefaultVoltage) {
}

} // namespace Core
