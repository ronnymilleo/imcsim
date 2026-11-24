/**
 * @file    controlled_sources.h
 * @brief   Linear controlled sources: VCVS (E), VCCS (G), CCCS (F), and CCVS (H).
 */

#ifndef NODALYS_CONTROLLED_SOURCES_H
#define NODALYS_CONTROLLED_SOURCES_H

#include "devices/device.h"
#include "devices/sensitivity.h"

namespace Nodalys {

/**
 * @class   CccsDevice
 * @brief   A current-controlled current source (SPICE F): the current from a to b is gain times the current of
 *          the controlling branch, which a probe source in series with the controlling terminal carries.
 */
class CccsDevice : public Device, public SensitiveDevice {
public:
    CccsDevice(int node_a, int node_b, int control_branch, double gain);

    /**
     * @brief   Returns the positive terminal MNA node index.
     * @return  Node unknown index, or `-1` for ground.
     */
    int GetNodeA() const { return m_NodeA; }

    /**
     * @brief   Returns the negative terminal MNA node index.
     * @return  Node unknown index, or `-1` for ground.
     */
    int GetNodeB() const { return m_NodeB; }

    /**
     * @brief   Returns the controlling branch unknown index.
     * @return  Branch unknown index.
     */
    int ControlBranch() const { return m_ControlBranch; }

    /**
     * @brief   Binds the controlling branch unknown index after all voltage sources are compiled.
     * @param[in] branch  Controlling branch unknown index.
     */
    void BindControlBranch(int branch) { m_ControlBranch = branch; }

    /**
     * @brief   Returns the current gain factor.
     * @return  Dimensionless current gain.
     */
    double GetGain() const { return m_Gain; }

    void Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) override;
    double TerminalCurrent(int terminal, const std::vector<double> &x) const override;
    std::complex<double> AcTerminalCurrent(int terminal,
                                           const std::vector<double> &x_dc,
                                           const std::vector<std::complex<double>> &x_ac,
                                           double omega) const override;

    [[nodiscard]] DeviceRoles GetRoles() override { return {.Sensitive = this}; }

    [[nodiscard]] std::optional<SensitivityDescriptor> DescribeSensitivity() const override;
    [[nodiscard]] double DcSensitivityDerivative(const DcAdjointView &point) const override;
    [[nodiscard]] std::complex<double> AcSensitivityDerivative(const AcAdjointView &point) const override;
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
class CcvsDevice : public Device, public SensitiveDevice {
public:
    CcvsDevice(int node_a, int node_b, int branch, int control_branch, double gain);

    /**
     * @brief   Returns the positive terminal MNA node index.
     * @return  Node unknown index, or `-1` for ground.
     */
    int GetNodeA() const { return m_NodeA; }

    /**
     * @brief   Returns the negative terminal MNA node index.
     * @return  Node unknown index, or `-1` for ground.
     */
    int GetNodeB() const { return m_NodeB; }

    /**
     * @brief   Returns the output voltage-source branch unknown index.
     * @return  Branch unknown index.
     */
    int GetBranch() const { return m_Branch; }

    /**
     * @brief   Returns the controlling branch unknown index.
     * @return  Controlling branch unknown index.
     */
    int ControlBranch() const { return m_ControlBranch; }

    /**
     * @brief   Binds the controlling branch unknown index after all voltage sources are compiled.
     * @param[in] branch  Controlling branch unknown index.
     */
    void BindControlBranch(int branch) { m_ControlBranch = branch; }

    /**
     * @brief   Returns the transresistance gain in Ohms.
     * @return  Transresistance in Ohms.
     */
    double GetGain() const { return m_Gain; }

    void Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) override;
    double TerminalCurrent(int terminal, const std::vector<double> &x) const override;
    std::complex<double> AcTerminalCurrent(int terminal,
                                           const std::vector<double> &x_dc,
                                           const std::vector<std::complex<double>> &x_ac,
                                           double omega) const override;

    [[nodiscard]] DeviceRoles GetRoles() override { return {.Sensitive = this}; }

    [[nodiscard]] std::optional<SensitivityDescriptor> DescribeSensitivity() const override;
    [[nodiscard]] double DcSensitivityDerivative(const DcAdjointView &point) const override;
    [[nodiscard]] std::complex<double> AcSensitivityDerivative(const AcAdjointView &point) const override;
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
class VccsDevice : public Device, public SensitiveDevice {
public:
    VccsDevice(int node_a, int node_b, int control_positive, int control_negative, double gain);

    /**
     * @brief   Returns the positive output MNA node index.
     * @return  Node unknown index, or `-1` for ground.
     */
    int GetNodeA() const { return m_NodeA; }

    /**
     * @brief   Returns the negative output MNA node index.
     * @return  Node unknown index, or `-1` for ground.
     */
    int GetNodeB() const { return m_NodeB; }

    /**
     * @brief   Returns the positive controlling MNA node index.
     * @return  Node unknown index, or `-1` for ground.
     */
    int GetControlPositive() const { return m_ControlPositive; }

    /**
     * @brief   Returns the negative controlling MNA node index.
     * @return  Node unknown index, or `-1` for ground.
     */
    int GetControlNegative() const { return m_ControlNegative; }

    /**
     * @brief   Returns the transconductance gain in Siemens.
     * @return  Transconductance in Siemens.
     */
    double GetGain() const { return m_Gain; }

    void Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) override;
    double TerminalCurrent(int terminal, const std::vector<double> &x) const override;
    std::complex<double> AcTerminalCurrent(int terminal,
                                           const std::vector<double> &x_dc,
                                           const std::vector<std::complex<double>> &x_ac,
                                           double omega) const override;

    [[nodiscard]] DeviceRoles GetRoles() override { return {.Sensitive = this}; }

    [[nodiscard]] std::optional<SensitivityDescriptor> DescribeSensitivity() const override;
    [[nodiscard]] double DcSensitivityDerivative(const DcAdjointView &point) const override;
    [[nodiscard]] std::complex<double> AcSensitivityDerivative(const AcAdjointView &point) const override;
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
class VcvsDevice : public Device, public SensitiveDevice {
public:
    VcvsDevice(int node_a, int node_b, int control_positive, int control_negative, int branch, double gain);

    /**
     * @brief   Returns the positive output MNA node index.
     * @return  Node unknown index, or `-1` for ground.
     */
    int GetNodeA() const { return m_NodeA; }

    /**
     * @brief   Returns the negative output MNA node index.
     * @return  Node unknown index, or `-1` for ground.
     */
    int GetNodeB() const { return m_NodeB; }

    /**
     * @brief   Returns the positive controlling MNA node index.
     * @return  Node unknown index, or `-1` for ground.
     */
    int GetControlPositive() const { return m_ControlPositive; }

    /**
     * @brief   Returns the negative controlling MNA node index.
     * @return  Node unknown index, or `-1` for ground.
     */
    int GetControlNegative() const { return m_ControlNegative; }

    /**
     * @brief   Returns the output voltage-source branch unknown index.
     * @return  Branch unknown index.
     */
    int GetBranch() const { return m_Branch; }

    /**
     * @brief   Returns the voltage gain factor.
     * @return  Dimensionless voltage gain.
     */
    double GetGain() const { return m_Gain; }

    void Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) override;
    double TerminalCurrent(int terminal, const std::vector<double> &x) const override;
    std::complex<double> AcTerminalCurrent(int terminal,
                                           const std::vector<double> &x_dc,
                                           const std::vector<std::complex<double>> &x_ac,
                                           double omega) const override;

    [[nodiscard]] DeviceRoles GetRoles() override { return {.Sensitive = this}; }

    [[nodiscard]] std::optional<SensitivityDescriptor> DescribeSensitivity() const override;
    [[nodiscard]] double DcSensitivityDerivative(const DcAdjointView &point) const override;
    [[nodiscard]] std::complex<double> AcSensitivityDerivative(const AcAdjointView &point) const override;
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
