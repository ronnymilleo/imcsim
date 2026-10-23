/**
 * @file    capacitor.cpp
 * @brief   A capacitor in the simulation model.
 */

#include "capacitor.h"
#include <memory>

namespace Core {

namespace {

constexpr double DEFAULT_CAPACITANCE = 1e-6;

} // namespace

/**
 * @brief   Creates a capacitor of 1 uF.
 */
Capacitor::Capacitor() : Component(ComponentType::CAPACITOR, DEFAULT_CAPACITANCE) {
}

/**
 * @brief   Copies the component with its name, value and settings.
 * @return  An independent component of the same kind.
 */
std::unique_ptr<Component> Capacitor::Clone() const {
    return std::make_unique<Capacitor>(*this);
}

} // namespace Core
