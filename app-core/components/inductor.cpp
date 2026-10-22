/**
 * @file    inductor.cpp
 * @brief   An inductor in the simulation model.
 */

#include "inductor.h"

namespace Core {

namespace {

constexpr double DEFAULT_INDUCTANCE = 1e-3;

} // namespace

/**
 * @brief   Creates an inductor of 1 mH.
 */
Inductor::Inductor() : Component(ComponentType::INDUCTOR, DEFAULT_INDUCTANCE) {
}

} // namespace Core
