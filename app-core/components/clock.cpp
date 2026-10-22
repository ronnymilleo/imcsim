/**
 * @file    clock.cpp
 * @brief   A digital clock source in the simulation model.
 */

#include "clock.h"

namespace Core {

/**
 * @brief   Creates a 1 kHz, 50% clock at 5 V with no start delay.
 */
Clock::Clock() : Component(ComponentType::Clock, 0.0) {
}

/**
 * @brief   Returns the shape of the clock.
 * @return  Frequency in hertz, duty cycle as a share of the period, logic level in volts and start delay in seconds.
 */
const ClockParameters &Clock::GetParameters() const {
    return m_Parameters;
}

/**
 * @brief   Changes the shape of the clock.
 * @param[in] parameters  New figures; check them with IsValidParameters() first.
 */
void Clock::SetParameters(const ClockParameters &parameters) {
    m_Parameters = parameters;
}

/**
 * @brief   Checks whether a clock can be simulated.
 * @param[in] parameters  Candidate figures.
 * @return  True when the frequency is positive, the duty cycle is a share strictly between 0 and 1, the logic level is
 *          positive and the start delay is not negative.
 */
bool Clock::IsValidParameters(const ClockParameters &parameters) {
    return parameters.Frequency > 0.0 && parameters.DutyCycle > 0.0 && parameters.DutyCycle < 1.0 &&
           parameters.LogicLevel > 0.0 && parameters.StartDelay >= 0.0;
}

} // namespace Core
