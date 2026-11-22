/**
 * @file    controlled_sources.h
 * @brief   Linear controlled sources: VCVS (E), VCCS (G), CCCS (F), and CCVS (H).
 */

#ifndef NODALYS_CONTROLLED_SOURCES_H
#define NODALYS_CONTROLLED_SOURCES_H

#include "devices/device.h"

namespace Nodalys {

/**
 * @class   CccsDevice
 * @brief   A current-controlled current source (SPICE F): the current from a to b is gain times the current of
 *          the controlling branch, which a probe source in series with the controlling terminal carries.
 */
class CccsDevice : public Device {
public:
    CccsDevice(int node_a, int node_b, int control_branch, double gain);

    int GetNodeA() const { return m_NodeA; }

    int GetNodeB() const { return m_NodeB; }

    int ControlBranch() const { return m_ControlBranch; }

    void BindControlBranch(int branch) { m_ControlBranch = branch; }

    double GetGain() const { return m_Gain; }

    void Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) override;
    double TerminalCurrent(int terminal, const std::vector<double> &x) const override;
    std::complex<double> AcTerminalCurrent(int terminal,
                                           const std::vector<double> &x_dc,
                                           const std::vector<std::complex<double>> &x_ac,
                                           double omega) const override;
    [[nodiscard]] std::unique_ptr<Device> Clone() const override;

private:
    int m_NodeA;
    int m_NodeB;
    int m_ControlBranch;
    double m_Gain;
};

/**
 * @class   CcvsDevice
 * @brief   A current-controlled voltage source (SPICE H): v(a) − v(b) = gain times the controlling branch current.
 */
class CcvsDevice : public Device {
public:
    CcvsDevice(int node_a, int node_b, int branch, int control_branch, double gain);

    int GetNodeA() const { return m_NodeA; }

    int GetNodeB() const { return m_NodeB; }

    int GetBranch() const { return m_Branch; }

    int ControlBranch() const { return m_ControlBranch; }

    void BindControlBranch(int branch) { m_ControlBranch = branch; }

    double GetGain() const { return m_Gain; }

    void Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) override;
    double TerminalCurrent(int terminal, const std::vector<double> &x) const override;
    std::complex<double> AcTerminalCurrent(int terminal,
                                           const std::vector<double> &x_dc,
                                           const std::vector<std::complex<double>> &x_ac,
                                           double omega) const override;
    [[nodiscard]] std::unique_ptr<Device> Clone() const override;

private:
    int m_NodeA;
    int m_NodeB;
    int m_Branch;
    int m_ControlBranch;
    double m_Gain;
};

/**
 * @class   VccsDevice
 * @brief   A voltage-controlled current source (SPICE G): the current from a to b is gain·(v(cp) − v(cm)).
 */
class VccsDevice : public Device {
public:
    VccsDevice(int node_a, int node_b, int control_positive, int control_negative, double gain);

    int GetNodeA() const { return m_NodeA; }

    int GetNodeB() const { return m_NodeB; }

    int GetControlPositive() const { return m_ControlPositive; }

    int GetControlNegative() const { return m_ControlNegative; }

    double GetGain() const { return m_Gain; }

    void Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) override;
    double TerminalCurrent(int terminal, const std::vector<double> &x) const override;
    std::complex<double> AcTerminalCurrent(int terminal,
                                           const std::vector<double> &x_dc,
                                           const std::vector<std::complex<double>> &x_ac,
                                           double omega) const override;
    [[nodiscard]] std::unique_ptr<Device> Clone() const override;

private:
    int m_NodeA;
    int m_NodeB;
    int m_ControlPositive;
    int m_ControlNegative;
    double m_Gain;
};

/**
 * @class   VcvsDevice
 * @brief   A voltage-controlled voltage source (SPICE E): v(a) − v(b) = gain·(v(cp) − v(cm)) as a branch.
 */
class VcvsDevice : public Device {
public:
    VcvsDevice(int node_a, int node_b, int control_positive, int control_negative, int branch, double gain);

    int GetNodeA() const { return m_NodeA; }

    int GetNodeB() const { return m_NodeB; }

    int GetControlPositive() const { return m_ControlPositive; }

    int GetControlNegative() const { return m_ControlNegative; }

    int GetBranch() const { return m_Branch; }

    double GetGain() const { return m_Gain; }

    void Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) override;
    double TerminalCurrent(int terminal, const std::vector<double> &x) const override;
    std::complex<double> AcTerminalCurrent(int terminal,
                                           const std::vector<double> &x_dc,
                                           const std::vector<std::complex<double>> &x_ac,
                                           double omega) const override;
    [[nodiscard]] std::unique_ptr<Device> Clone() const override;

private:
    int m_NodeA;
    int m_NodeB;
    int m_ControlPositive;
    int m_ControlNegative;
    int m_Branch;
    double m_Gain;
};

} // namespace Nodalys

#endif // NODALYS_CONTROLLED_SOURCES_H
