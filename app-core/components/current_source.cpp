/**
 * @file    current_source.cpp
 * @brief   An independent current source in the simulation model.
 */

#include "current_source.h"

namespace Core {

namespace {

constexpr double DEFAULT_CURRENT = 1e-3;
constexpr double DEFAULT_AMPLITUDE = 1e-3;
constexpr double DEFAULT_PULSE_HIGH = 1e-3;

} // namespace

/**
 * @brief   Creates a 1 mA DC current source.
 * @note    The sine starts at 1 mA and the pulse goes from 0 to 1 mA, ready for switching type.
 */
CurrentSource::CurrentSource()
    : Source(ComponentType::CURRENT_SOURCE, DEFAULT_CURRENT, DEFAULT_AMPLITUDE, DEFAULT_PULSE_HIGH) {
}

} // namespace Core
