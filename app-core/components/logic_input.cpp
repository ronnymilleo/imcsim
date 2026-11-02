/**
 * @file    logic_input.cpp
 * @brief   A logic input that drives a digital circuit high or low, in the simulation model.
 */

#include "logic_input.h"
#include <memory>

namespace Core {

/**
 * @brief   Creates a logic input that drives 0 V, with a 5 V logic level.
 */
LogicInput::LogicInput() : Component(ComponentType::LOGIC_INPUT, 0.0) {
}

/**
 * @brief   Copies the component with its name, value and settings.
 * @return  An independent component of the same kind.
 */
std::unique_ptr<Component> LogicInput::Clone() const {
    return std::make_unique<LogicInput>(*this);
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

/**
 * @brief   Returns the voltage the input drives.
 * @return  The logic level when high, 0 V when low.
 */
double LogicInput::GetVoltage() const {
    return m_IsHigh ? m_Parameters.LogicLevel : 0.0;
}

} // namespace Core
