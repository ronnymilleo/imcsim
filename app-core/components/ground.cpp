/**
 * @file    ground.cpp
 * @brief   A ground reference (0 V node) in the simulation model.
 */

#include "ground.h"

namespace Core {

/**
 * @brief   Creates a ground reference.
 */
Ground::Ground() : Component(ComponentType::Ground) {
}

} // namespace Core
