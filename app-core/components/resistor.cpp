/**
 * @file    resistor.cpp
 * @brief   A resistor in the simulation model.
 */

#include "resistor.h"

namespace Core {

namespace {

constexpr double DefaultResistance = 1e3;

} // namespace

/**
 * @brief   Creates a resistor of 1 kOhm.
 */
Resistor::Resistor() : Component(ComponentType::Resistor, DefaultResistance) {
}

} // namespace Core
