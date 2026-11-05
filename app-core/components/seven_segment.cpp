/**
 * @file    seven_segment.cpp
 * @brief   A 7-segment display that lights each segment whose node is at a logic 1, in the simulation model.
 */

#include "components/seven_segment.h"

#include <memory>

namespace Core {

/**
 * @brief   Creates a display read against a 5 V logic level.
 */
SevenSegmentDisplay::SevenSegmentDisplay() : Component(ComponentType::SEVEN_SEGMENT, 0.0) {
}

/**
 * @brief   Copies the display, with its logic level.
 * @return  The copy.
 */
std::unique_ptr<Component> SevenSegmentDisplay::Clone() const {
    return std::make_unique<SevenSegmentDisplay>(*this);
}

/**
 * @brief   Returns the logic level its segments are read against.
 * @return  The parameters of the display.
 */
const SevenSegmentParameters &SevenSegmentDisplay::GetParameters() const {
    return m_Parameters;
}

/**
 * @brief   Sets the logic level its segments are read against.
 * @param[in] parameters  Parameters that IsValidParameters() accepts.
 */
void SevenSegmentDisplay::SetParameters(const SevenSegmentParameters &parameters) {
    m_Parameters = parameters;
}

/**
 * @brief   Checks the parameters of a display.
 * @param[in] parameters  Parameters to check.
 * @return  True when the logic level is positive.
 */
bool SevenSegmentDisplay::IsValidParameters(const SevenSegmentParameters &parameters) {
    return parameters.LogicLevel > 0.0;
}

} // namespace Core
