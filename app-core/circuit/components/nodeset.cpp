/**
 * @file    nodeset.cpp
 * @brief   A marker that gives the solver a voltage to start looking for the operating point of its node from.
 */

#include "circuit/components/nodeset.h"

#include <memory>

namespace Core {

namespace {

constexpr double DEFAULT_VOLTAGE = 2.5;

} // namespace

/**
 * @brief   Creates a nodeset of 2.5 V.
 */
Nodeset::Nodeset() : Component(ComponentType::NODESET, DEFAULT_VOLTAGE) {
}

/**
 * @brief   Copies the marker with its voltage.
 * @return  An independent component of the same kind.
 */
std::unique_ptr<Component> Nodeset::Clone() const {
    return std::make_unique<Nodeset>(*this);
}

} // namespace Core
