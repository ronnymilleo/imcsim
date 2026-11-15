/**
 * @file    logic_output.cpp
 * @brief   A logic output that shows the logic level of its node on the schematic, in the simulation model.
 */

#include "circuit/components/logic_output.h"

#include <memory>

namespace Core {

/**
 * @brief   Creates a logic output read against a 5 V logic level.
 */
LogicOutput::LogicOutput() : Component(ComponentType::LOGIC_OUTPUT, 0.0) {
}

/**
 * @brief   Copies the output, with its logic level.
 * @return  The copy.
 */
std::unique_ptr<Component> LogicOutput::Clone() const {
    return std::make_unique<LogicOutput>(*this);
}

/**
 * @brief   Returns the logic level its node is read against.
 * @return  The parameters of the output.
 */
const LogicOutputParameters &LogicOutput::GetParameters() const {
    return m_Parameters;
}

/**
 * @brief   Sets the logic level its node is read against.
 * @param[in] parameters  Parameters that IsValidParameters() accepts.
 */
void LogicOutput::SetParameters(const LogicOutputParameters &parameters) {
    m_Parameters = parameters;
}

/**
 * @brief   Checks the parameters of a logic output.
 * @param[in] parameters  Parameters to check.
 * @return  True when the logic level is positive.
 */
bool LogicOutput::IsValidParameters(const LogicOutputParameters &parameters) {
    return parameters.LogicLevel > 0.0;
}

} // namespace Core
