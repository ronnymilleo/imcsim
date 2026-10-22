/**
 * @file    clock.h
 * @brief   A digital clock source in the simulation model.
 */

#ifndef IMCSIM_CLOCK_H
#define IMCSIM_CLOCK_H

#include "component.h"

namespace Core {

/**
 * @struct  ClockParameters
 * @brief   Shape of a digital clock: a frequency in hertz, a duty cycle as a share of the period, a logic level in
 *          volts and a start delay in seconds.
 * @details The clock swings between 0 V and the logic level. It picks its own edge times, a fixed fraction of the
 *          period, so the shape is the same at every frequency. The duty cycle is the share of each period the clock
 *          spends above half the logic level, which is the high phase plus half of the rising and falling edges.
 */
struct ClockParameters {
    double Frequency = 1e3;
    double DutyCycle = 0.5;
    double LogicLevel = 5.0;
    double StartDelay = 0.0;
};

/**
 * @class   Clock
 * @brief   A voltage source that drives a digital clock, written as a SPICE pulse source between its two terminals.
 * @details The clock is low until its start delay, which is also its operating point value, and its first rising edge
 *          begins there; a start delay of 0 puts the first edge at the start. The user sets no rise or fall times.
 */
class Clock : public Component {
public:
    Clock();
    ~Clock() override = default;

    const ClockParameters &GetParameters() const;
    void SetParameters(const ClockParameters &parameters);
    static bool IsValidParameters(const ClockParameters &parameters);

private:
    ClockParameters m_Parameters;
};

// Rise and fall time of a clock, each as a share of the period; the part picks these so the shape is the same at
// every frequency and the user never has to set them
inline constexpr double ClockEdgeFraction = 1e-3;

} // namespace Core

#endif // IMCSIM_CLOCK_H
