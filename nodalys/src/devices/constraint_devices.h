/**
 * @file    constraint_devices.h
 * @brief   Operating-point and transient node constraints: initial conditions (.ic), nodesets (.nodeset), and Newton
 *          seeds.
 */

#ifndef NODALYS_CONSTRAINT_DEVICES_H
#define NODALYS_CONSTRAINT_DEVICES_H

#include "devices/device.h"

namespace Nodalys {

/**
 * @struct  NodeSeed
 * @brief   One node index and its initial Newton guess for `NodeSeedDevice`.
 */
struct NodeSeed {
    int Node = -1;
    double Voltage = 0.0;
};

/**
 * @class   NodeInitialConditionDevice
 * @brief   Clamps a node to a fixed initial voltage while the operating point that starts a transient is found (SPICE
 *          `.ic`), then disconnects.
 * @details Stamps a stiff Norton companion to ground only in that solve, so the rest of the circuit starts from the
 *          point that is consistent with the node, and the node evolves freely for $t > 0$. A transient that starts
 *          from initial conditions (`uic`) has no operating point: there the voltage reaches the circuit only through
 *          the capacitors on the node, which `CapacitorDevice::TakeInitialVoltageFrom()` sets up, as in SPICE.
 *          Operating point and small-signal analyses are unaffected.
 */
class NodeInitialConditionDevice : public Device {
public:
    NodeInitialConditionDevice(int node, double voltage);

    void Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) override;
    double TerminalCurrent(int terminal, const std::vector<double> &x) const override;
    std::complex<double> AcTerminalCurrent(int terminal,
                                           const std::vector<double> &x_dc,
                                           const std::vector<std::complex<double>> &x_ac,
                                           double omega) const override;
    [[nodiscard]] std::unique_ptr<Device> Clone() const override;

private:
    int m_Node;
    double m_Voltage;
};

/**
 * @class   NodeSeedDevice
 * @brief   Provides an initial Newton guess for bistable subcircuits such as flip-flops without stamping any branch.
 */
class NodeSeedDevice : public Device {
public:
    explicit NodeSeedDevice(std::vector<NodeSeed> seeds);

    void Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) override;
    void SeedOperatingPoint(std::vector<double> &x) const override;
    double TerminalCurrent(int terminal, const std::vector<double> &x) const override;
    std::complex<double> AcTerminalCurrent(int terminal,
                                           const std::vector<double> &x_dc,
                                           const std::vector<std::complex<double>> &x_ac,
                                           double omega) const override;
    [[nodiscard]] std::unique_ptr<Device> Clone() const override;

private:
    std::vector<NodeSeed> m_Seeds;
};

/**
 * @class   NodesetDevice
 * @brief   Pulls a node to a voltage in the first pass of the solve of an operating point (SPICE `.nodeset`), which
 *          then lets go and converges again.
 * @details A stiff Norton companion to ground, stamped only while `TimeContext::IsNodesetPass` is true. The solver
 *          finds the point with every nodeset held, releases them and converges again from there, so a circuit with
 *          several stable states, such as a latch, ends in the one nearest the voltages, and a circuit with one ends
 *          where it always does. A nodeset is a guess where `NodeInitialConditionDevice` is a held value, which the
 *          transient keeps in the operating point that starts it. Every solve that starts from an operating point reads
 *          it, the transient's without `uic` included; the solves that continue a DC sweep do not.
 */
class NodesetDevice : public Device {
public:
    NodesetDevice(int node, double voltage);

    void Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) override;
    double TerminalCurrent(int terminal, const std::vector<double> &x) const override;
    std::complex<double> AcTerminalCurrent(int terminal,
                                           const std::vector<double> &x_dc,
                                           const std::vector<std::complex<double>> &x_ac,
                                           double omega) const override;
    [[nodiscard]] std::unique_ptr<Device> Clone() const override;

private:
    int m_Node;
    double m_Voltage;
};

} // namespace Nodalys

#endif // NODALYS_CONSTRAINT_DEVICES_H
