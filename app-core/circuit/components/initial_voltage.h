/**
 * @file    initial_voltage.h
 * @brief   A marker that sets the voltage its node starts a transient at, in the simulation model.
 */

#ifndef IMCSIM_INITIAL_VOLTAGE_H
#define IMCSIM_INITIAL_VOLTAGE_H

#include "circuit/components/component.h"
#include <memory>

namespace Core {

/**
 * @class   InitialVoltage
 * @brief   One terminal and a voltage: the node it sits on starts a transient at that voltage.
 * @details It has no netlist line and no name, like a net label: the circuit builder turns it into an initial voltage
 *          of its node (Circuit::AddInitialVoltage()). The voltage is the value of the part. The node is held there
 *          while the operating point that starts the transient is found and then released, so the rest of the circuit
 *          starts from the point that is consistent with it. Only the transient reads it.
 */
class InitialVoltage : public Component {
public:
    InitialVoltage();
    ~InitialVoltage() override = default;

    std::unique_ptr<Component> Clone() const override;
};

} // namespace Core

#endif // IMCSIM_INITIAL_VOLTAGE_H
