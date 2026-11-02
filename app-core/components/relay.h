/**
 * @file    relay.h
 * @brief   An electromechanical relay: a coil and the contact it closes.
 */

#ifndef IMCSIM_RELAY_H
#define IMCSIM_RELAY_H

#include "component.h"
#include "voltage_controlled_switch.h"
#include <memory>

namespace Core {

/**
 * @struct  RelayParameters
 * @brief   Datasheet figures of a relay.
 * @details The contact closes when the coil voltage reaches the pull-in voltage and opens again when it falls to
 *          the drop-out voltage, which is lower. The contact is open when the coil is off.
 */
struct RelayParameters {
    double CoilResistance = 70.0;
    double CoilInductance = 50e-3;
    double PullInVoltage = 3.5;
    double DropOutVoltage = 1.0;
    double ContactResistance = 0.05;
};

/**
 * @class   Relay
 * @brief   A normally open relay: contact terminals 1 and 2, then coil terminals 3 (+) and 4 (-).
 * @details The coil is its resistance in series with its inductance, so the current builds up and dies away with a
 *          time constant of L/R, and the contact follows that current. The part has no value.
 */
class Relay : public Component {
public:
    Relay();
    ~Relay() override = default;

    std::unique_ptr<Component> Clone() const override;

    const RelayParameters &GetParameters() const;
    void SetParameters(const RelayParameters &parameters);
    static bool IsValidParameters(const RelayParameters &parameters);
    SwitchParameters GetContactParameters() const;

private:
    RelayParameters m_Parameters;
};

} // namespace Core

#endif // IMCSIM_RELAY_H
