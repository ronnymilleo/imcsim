/**
 * @file    logic_input.cpp
 * @brief   A logic input that drives a digital circuit high or low, in the simulation model.
 */

#include "logic_input.h"

namespace Core {

/**
 * @brief   Creates a logic input that drives 0 V, with a 5 V logic level.
 */
LogicInput::LogicInput() : Component(ComponentType::LOGIC_INPUT, 0.0) {
}

/**
 * @brief   Returns the figures of the input.
 * @return  The logic level it drives when high, in volts.
 */
const LogicInputParameters &LogicInput::GetParameters() const {
    return m_Parameters;
}

/**
 * @brief   Changes the figures of the input.
 * @param[in] parameters  New logic level; check it with IsValidParameters() first.
 */
void LogicInput::SetParameters(const LogicInputParameters &parameters) {
    m_Parameters = parameters;
}

/**
 * @brief   Checks whether a logic input can be simulated.
 * @param[in] parameters  Candidate figures.
 * @return  True when the logic level is positive. Low is always 0 V, whatever the level.
 */
bool LogicInput::IsValidParameters(const LogicInputParameters &parameters) {
    return parameters.LogicLevel > 0.0;
}

/**
 * @brief   Tells whether the input drives the logic level.
 * @return  True when high, false when it drives 0 V.
 */
bool LogicInput::IsHigh() const {
    return m_IsHigh;
}

/**
 * @brief   Sets the input high or low.
 * @param[in] high  True to drive the logic level, false to drive 0 V.
 */
void LogicInput::SetHigh(const bool high) {
    m_IsHigh = high;
}

} // namespace Core
