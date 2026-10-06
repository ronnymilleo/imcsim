/**
 * @file    inductor.cpp
 * @brief   An inductor in the simulation model.
 */

#include "inductor.h"

namespace Core {

namespace {

constexpr double DefaultInductance = 1e-3;

} // namespace

/**
 * @brief   Creates an inductor of 1 mH.
 */
Inductor::Inductor() : Component(ComponentType::Inductor, DefaultInductance) {
}

} // namespace Core
