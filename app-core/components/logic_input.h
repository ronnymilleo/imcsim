/**
 * @file    logic_input.h
 * @brief   A logic input that drives a digital circuit high or low, in the simulation model.
 */

#ifndef IMCSIM_LOGIC_INPUT_H
#define IMCSIM_LOGIC_INPUT_H

#include "component.h"
#include <memory>

namespace Core {

/**
 * @struct  LogicInputParameters
 * @brief   The logic level a logic input drives when it is high, in volts.
 */
struct LogicInputParameters {
    double LogicLevel = 5.0;
};

/**
 * @class   LogicInput
 * @brief   A voltage source from its terminal to ground that drives 0 V or the logic level, set by hand.
 * @details It is a constant source, so a latch or a gate gets a defined level to start from. High drives the logic
 *          level and Low drives 0 V. Both start low, so an unconnected input reads 0.
 */
class LogicInput : public Component {
public:
    LogicInput();
    ~LogicInput() override = default;

    std::unique_ptr<Component> Clone() const override;

    const LogicInputParameters &GetParameters() const;
    void SetParameters(const LogicInputParameters &parameters);
    static bool IsValidParameters(const LogicInputParameters &parameters);

    bool IsHigh() const;
    void SetHigh(bool high);
    double GetVoltage() const;

private:
    LogicInputParameters m_Parameters;
    bool m_IsHigh = false;
};

} // namespace Core

#endif // IMCSIM_LOGIC_INPUT_H
