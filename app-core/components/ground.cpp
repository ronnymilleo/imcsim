/**
 * @file    ground.cpp
 * @brief   A ground reference (0 V node) in the simulation model.
 */

#include "ground.h"
#include <memory>

namespace Core {

/**
 * @brief   Creates a ground reference.
 */
Ground::Ground() : Component(ComponentType::GROUND, 0.0) {
}

/**
 * @brief   Copies the component with its name, value and settings.
 * @return  An independent component of the same kind.
 */
std::unique_ptr<Component> Ground::Clone() const {
    return std::make_unique<Ground>(*this);
}

} // namespace Core
