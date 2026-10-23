/**
 * @file    inductor.cpp
 * @brief   An inductor in the simulation model.
 */

#include "inductor.h"
#include <memory>

namespace Core {

namespace {

constexpr double DEFAULT_INDUCTANCE = 1e-3;

} // namespace

/**
 * @brief   Creates an inductor of 1 mH.
 */
Inductor::Inductor() : Component(ComponentType::INDUCTOR, DEFAULT_INDUCTANCE) {
}

/**
 * @brief   Copies the component with its name, value and settings.
 * @return  An independent component of the same kind.
 */
std::unique_ptr<Component> Inductor::Clone() const {
    return std::make_unique<Inductor>(*this);
}

} // namespace Core
