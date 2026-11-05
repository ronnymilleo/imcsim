/**
 * @file    logic_output.h
 * @brief   A logic output that shows the logic level of its node on the schematic, in the simulation model.
 */

#ifndef IMCSIM_LOGIC_OUTPUT_H
#define IMCSIM_LOGIC_OUTPUT_H

#include "components/component.h"
#include <memory>

namespace Core {

/**
 * @struct  LogicOutputParameters
 * @brief   The logic level the voltage of a logic output is read against, in volts.
 */
struct LogicOutputParameters {
    double LogicLevel = 5.0;
};

/**
 * @class   LogicOutput
 * @brief   A display that reads its node as 0, 1 or unknown, like the input of a gate, and takes no part in the
 *          simulation.
 * @details It has no netlist line, no name and no value: the schematic only reads the voltage of its node, at or above
 *          70% of the logic level as 1 and at or below 30% as 0, so trying a digital circuit needs no LED, resistor or
 *          Ground.
 */
class LogicOutput : public Component {
public:
    LogicOutput();
    ~LogicOutput() override = default;

    std::unique_ptr<Component> Clone() const override;

    const LogicOutputParameters &GetParameters() const;
    void SetParameters(const LogicOutputParameters &parameters);
    static bool IsValidParameters(const LogicOutputParameters &parameters);

private:
    LogicOutputParameters m_Parameters;
};

} // namespace Core

#endif // IMCSIM_LOGIC_OUTPUT_H
