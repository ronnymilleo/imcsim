/**
 * @file    vcc.cpp
 * @brief   A positive supply rail in the simulation model.
 */

#include "vcc.h"
#include <memory>

namespace Core {

namespace {

constexpr double DEFAULT_VOLTAGE = 5.0;

} // namespace

/**
 * @brief   Creates a supply rail at 5 V.
 */
VCC::VCC() : Component(ComponentType::VCC, DEFAULT_VOLTAGE) {
}

/**
 * @brief   Copies the component with its name, value and settings.
 * @return  An independent component of the same kind.
 */
std::unique_ptr<Component> VCC::Clone() const {
    return std::make_unique<VCC>(*this);
}

} // namespace Core
