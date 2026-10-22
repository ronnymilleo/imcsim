/**
 * @file    capacitor.cpp
 * @brief   A capacitor in the simulation model.
 */

#include "capacitor.h"

namespace Core {

namespace {

constexpr double DEFAULT_CAPACITANCE = 1e-6;

} // namespace

/**
 * @brief   Creates a capacitor of 1 uF.
 */
Capacitor::Capacitor() : Component(ComponentType::CAPACITOR, DEFAULT_CAPACITANCE) {
}

} // namespace Core
