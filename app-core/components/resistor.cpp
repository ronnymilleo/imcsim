/**
 * @file    resistor.cpp
 * @brief   A resistor in the simulation model.
 */

#include "resistor.h"
#include <memory>

namespace Core {

namespace {

constexpr double DEFAULT_RESISTANCE = 1e3;

} // namespace

/**
 * @brief   Creates a resistor of 1 kOhm.
 */
Resistor::Resistor() : Component(ComponentType::RESISTOR, DEFAULT_RESISTANCE) {
}

/**
 * @brief   Copies the component with its name, value and settings.
 * @return  An independent component of the same kind.
 */
std::unique_ptr<Component> Resistor::Clone() const {
    return std::make_unique<Resistor>(*this);
}

} // namespace Core
