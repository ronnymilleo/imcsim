/**
 * @file    voltage_controlled_switch.h
 * @brief   A switch that opens and closes with a voltage elsewhere in the circuit (SPICE S).
 */

#ifndef IMCSIM_VOLTAGE_CONTROLLED_SWITCH_H
#define IMCSIM_VOLTAGE_CONTROLLED_SWITCH_H

#include "components/component.h"
#include <memory>
#include <nodalys/types.h>
#include <string>
#include <string_view>

namespace Core {

/**
 * @struct  SwitchParameters
 * @brief   What sets when a voltage-controlled switch conducts, and how well.
 * @details The switch closes when its control voltage rises past Threshold + Hysteresis and opens again when it
 *          falls below Threshold - Hysteresis, so noise around the threshold does not make it chatter.
 */
using SwitchParameters = nodalys::SwitchParameters;

/**
 * @class   VoltageControlledSwitch
 * @brief   A switch between terminals 1 and 2 that follows the voltage between terminals 3 (+) and 4 (-).
 * @details The control pins carry no current. The part has no value; its parameters are set one by one.
 */
class VoltageControlledSwitch : public Component {
public:
    VoltageControlledSwitch();
    ~VoltageControlledSwitch() override = default;

    std::unique_ptr<Component> Clone() const override;

    const SwitchParameters &GetParameters() const;
    void SetParameters(const SwitchParameters &parameters);
    static bool IsValidParameters(const SwitchParameters &parameters);

private:
    SwitchParameters m_Parameters;
};

std::string FormatSwitchModel(std::string_view spice_name, const SwitchParameters &parameters);

} // namespace Core

#endif // IMCSIM_VOLTAGE_CONTROLLED_SWITCH_H
