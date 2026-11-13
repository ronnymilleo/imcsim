/**
 * @file    behavioral_device.h
 * @brief   Behavioral sources of Nodalys: a voltage or a current that follows an expression of node voltages, branch
 *          currents and time.
 */

#ifndef NODALYS_BEHAVIORAL_DEVICE_H
#define NODALYS_BEHAVIORAL_DEVICE_H

#include "devices/devices.h"
#include "nodalys/expression.h"
#include <complex>
#include <string>
#include <vector>

namespace Nodalys {

/**
 * @struct  BehavioralBinding
 * @brief   Where an input of an expression is read in the unknowns of the system.
 */
struct BehavioralBinding {
    /// Unknown of the node the voltage is read at, or of the branch whose current is read; -1 for ground.
    int Unknown = -1;
    /// Unknown of the node the voltage is measured from, or -1 for ground; a current has none.
    int Reference = -1;
    bool IsCurrent = false;
};

/**
 * @class   BehavioralSourceDevice
 * @brief   What the two behavioral sources share: reading the inputs, evaluating the expression at the iterate and
 *          keeping its linearization.
 * @details The expression F(x) of the inputs is replaced at each Newton iterate x0 by F(x0) + sum of dF/dxk times
 *          (xk - x0k), with the derivatives exact. A derivative that the iterate has no value for, a domain error such
 *          as the logarithm of a negative voltage, stamps a zero source and reports a fault, which keeps the
 *          iteration from converging on it and tells why if the solve fails.
 *
 *          The small-signal analyses use the derivatives of the last stamp, which is the one at the operating point.
 *          `time` reads 0 outside a transient, as in ngspice.
 */
class BehavioralSourceDevice : public Device {
public:
    void BindCurrent(std::size_t input, int branch);

    bool IsLinear() const override;
    bool SupportsHarmonicBalance() const override;
    std::string_view GetFault() const override;

protected:
    BehavioralSourceDevice(std::string name, Expression expression, std::vector<BehavioralBinding> bindings);

    void Linearize(const std::vector<double> &x, const TimeContext &time);
    void StampPartials(MnaSystem &sys, int row, double sign) const;
    double EvaluateAt(const std::vector<double> &x) const;
    std::complex<double> SmallSignalValue(const std::vector<std::complex<double>> &x_ac) const;
    /// The constant of the linearization: the value minus the sum of each derivative times its input at the iterate.
    double GetOffset() const;

private:
    std::vector<double> ReadInputs(const std::vector<double> &x) const;

    std::string m_Name;
    Expression m_Expression;
    std::vector<BehavioralBinding> m_Bindings;
    std::string m_Fault;
    double m_TimeSeconds = 0.0;
    double m_Value = 0.0;
    double m_Offset = 0.0;
    /// Derivative of the value with respect to each input at the last stamp.
    std::vector<double> m_Partials;
};

/**
 * @class   BehavioralVoltageDevice
 * @brief   A voltage source (SPICE B with V=) as a branch: v(a) - v(b) = F, the current of the branch flowing from a
 *          through the source to b.
 */
class BehavioralVoltageDevice : public BehavioralSourceDevice {
public:
    BehavioralVoltageDevice(std::string name, int node_a, int node_b, int branch, Expression expression,
                            std::vector<BehavioralBinding> bindings);

    int GetBranch() const;

    void Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) override;
    double TerminalCurrent(int terminal, const std::vector<double> &x) const override;
    std::complex<double> AcTerminalCurrent(int terminal, const std::vector<double> &x_dc,
                                           const std::vector<std::complex<double>> &x_ac, double omega) const override;

private:
    int m_NodeA;
    int m_NodeB;
    int m_Branch;
};

/**
 * @class   BehavioralCurrentDevice
 * @brief   A current source (SPICE B with I=): the current F flows from a to b through the source, as for an
 *          independent current source.
 */
class BehavioralCurrentDevice : public BehavioralSourceDevice {
public:
    BehavioralCurrentDevice(std::string name, int node_a, int node_b, Expression expression,
                            std::vector<BehavioralBinding> bindings);

    void Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) override;
    double TerminalCurrent(int terminal, const std::vector<double> &x) const override;
    std::complex<double> AcTerminalCurrent(int terminal, const std::vector<double> &x_dc,
                                           const std::vector<std::complex<double>> &x_ac, double omega) const override;

private:
    int m_NodeA;
    int m_NodeB;
};

} // namespace Nodalys

#endif // NODALYS_BEHAVIORAL_DEVICE_H
