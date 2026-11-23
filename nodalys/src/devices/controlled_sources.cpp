/**
 * @file    controlled_sources.cpp
 * @brief   Linear controlled sources: VCVS (E), VCCS (G), CCCS (F), and CCVS (H).
 */

#include "devices/controlled_sources.h"

#include "devices/stamp_helpers.h"
#include <cstddef>

namespace Nodalys {

// ============================================================================
// CccsDevice
// ============================================================================

CccsDevice::CccsDevice(const int node_a, const int node_b, const int control_branch, const double gain)
    : m_NodeA(node_a), m_NodeB(node_b), m_ControlBranch(control_branch), m_Gain(gain) {
}

void CccsDevice::Stamp(MnaSystem &sys, const std::vector<double> &, const TimeContext &) {
    // Current leaving node_a and entering node_b is: i = gain * i(control_branch)
    sys.AddA(m_NodeA, m_ControlBranch, m_Gain);
    sys.AddA(m_NodeB, m_ControlBranch, -m_Gain);
}

double CccsDevice::TerminalCurrent(const int terminal, const std::vector<double> &x) const {
    const double current = m_Gain * x[static_cast<std::size_t>(m_ControlBranch)];
    return terminal == 0 ? current : -current;
}

std::complex<double> CccsDevice::AcTerminalCurrent(const int terminal,
                                                   const std::vector<double> & /*x_dc*/,
                                                   const std::vector<std::complex<double>> &x_ac,
                                                   const double /*omega*/) const {
    const std::complex<double> current = m_Gain * x_ac[static_cast<std::size_t>(m_ControlBranch)];
    return terminal == 0 ? current : -current;
}

std::optional<SensitivityDescriptor> CccsDevice::DescribeSensitivity() const {
    return SensitivityDescriptor{.Parameter = SensitivityParameter::GAIN, .Value = m_Gain, .IsGainParameter = true};
}

double CccsDevice::DcSensitivityDerivative(const DcAdjointView &point) const {
    return (point.AdjointAt(m_NodeB) - point.AdjointAt(m_NodeA)) * point.VoltageAt(m_ControlBranch);
}

std::complex<double> CccsDevice::AcSensitivityDerivative(const AcAdjointView &point) const {
    return -point.AdjointAcross(m_NodeA, m_NodeB) * point.AcAt(m_ControlBranch)
         - point.BiasAdjointAcross(m_NodeA, m_NodeB) * point.DcAt(m_ControlBranch);
}

std::unique_ptr<Device> CccsDevice::Clone() const {
    return std::make_unique<CccsDevice>(*this);
}

// ============================================================================
// CcvsDevice
// ============================================================================

CcvsDevice::CcvsDevice(const int node_a,
                       const int node_b,
                       const int branch,
                       const int control_branch,
                       const double gain)
    : m_NodeA(node_a), m_NodeB(node_b), m_Branch(branch), m_ControlBranch(control_branch), m_Gain(gain) {
}

void CcvsDevice::Stamp(MnaSystem &sys, const std::vector<double> &, const TimeContext &) {
    // Branch equation: v(a) - v(b) - gain * i(control_branch) = 0
    sys.AddA(m_NodeA, m_Branch, 1.0);
    sys.AddA(m_NodeB, m_Branch, -1.0);
    sys.AddA(m_Branch, m_NodeA, 1.0);
    sys.AddA(m_Branch, m_NodeB, -1.0);
    sys.AddA(m_Branch, m_ControlBranch, -m_Gain);
}

double CcvsDevice::TerminalCurrent(const int terminal, const std::vector<double> &x) const {
    const double current = x[static_cast<std::size_t>(m_Branch)];
    return terminal == 0 ? current : -current;
}

std::complex<double> CcvsDevice::AcTerminalCurrent(const int terminal,
                                                   const std::vector<double> & /*x_dc*/,
                                                   const std::vector<std::complex<double>> &x_ac,
                                                   const double /*omega*/) const {
    const std::complex<double> current = x_ac[static_cast<std::size_t>(m_Branch)];
    return terminal == 0 ? current : -current;
}

std::optional<SensitivityDescriptor> CcvsDevice::DescribeSensitivity() const {
    return SensitivityDescriptor{.Parameter = SensitivityParameter::GAIN, .Value = m_Gain, .IsGainParameter = true};
}

double CcvsDevice::DcSensitivityDerivative(const DcAdjointView &point) const {
    return point.AdjointAt(m_Branch) * point.VoltageAt(m_ControlBranch);
}

std::complex<double> CcvsDevice::AcSensitivityDerivative(const AcAdjointView &point) const {
    return point.AdjointAt(m_Branch) * point.AcAt(m_ControlBranch)
         + point.BiasAdjointAt(m_Branch) * point.DcAt(m_ControlBranch);
}

std::unique_ptr<Device> CcvsDevice::Clone() const {
    return std::make_unique<CcvsDevice>(*this);
}

// ============================================================================
// VccsDevice
// ============================================================================

VccsDevice::VccsDevice(const int node_a,
                       const int node_b,
                       const int control_positive,
                       const int control_negative,
                       const double gain)
    : m_NodeA(node_a), m_NodeB(node_b), m_ControlPositive(control_positive), m_ControlNegative(control_negative),
      m_Gain(gain) {
}

void VccsDevice::Stamp(MnaSystem &sys, const std::vector<double> &, const TimeContext &) {
    // Current leaving node_a and entering node_b is: i = gain * (v(cp) - v(cm))
    sys.AddA(m_NodeA, m_ControlPositive, m_Gain);
    sys.AddA(m_NodeA, m_ControlNegative, -m_Gain);
    sys.AddA(m_NodeB, m_ControlPositive, -m_Gain);
    sys.AddA(m_NodeB, m_ControlNegative, m_Gain);
}

double VccsDevice::TerminalCurrent(const int terminal, const std::vector<double> &x) const {
    const double control = NodeVoltage(x, m_ControlPositive) - NodeVoltage(x, m_ControlNegative);
    const double current = m_Gain * control;
    return terminal == 0 ? current : -current;
}

std::complex<double> VccsDevice::AcTerminalCurrent(const int terminal,
                                                   const std::vector<double> & /*x_dc*/,
                                                   const std::vector<std::complex<double>> &x_ac,
                                                   const double /*omega*/) const {
    const std::complex<double> current = m_Gain * AcBranchVoltage(x_ac, m_ControlPositive, m_ControlNegative);
    return terminal == 0 ? current : -current;
}

std::optional<SensitivityDescriptor> VccsDevice::DescribeSensitivity() const {
    return SensitivityDescriptor{.Parameter = SensitivityParameter::GAIN, .Value = m_Gain, .IsGainParameter = true};
}

double VccsDevice::DcSensitivityDerivative(const DcAdjointView &point) const {
    const double v_ctrl = point.VoltageAt(m_ControlPositive) - point.VoltageAt(m_ControlNegative);
    return (point.AdjointAt(m_NodeB) - point.AdjointAt(m_NodeA)) * v_ctrl;
}

std::complex<double> VccsDevice::AcSensitivityDerivative(const AcAdjointView &point) const {
    return -point.AdjointAcross(m_NodeA, m_NodeB) * point.AcAcross(m_ControlPositive, m_ControlNegative)
         - point.BiasAdjointAcross(m_NodeA, m_NodeB) * point.DcAcross(m_ControlPositive, m_ControlNegative);
}

std::unique_ptr<Device> VccsDevice::Clone() const {
    return std::make_unique<VccsDevice>(*this);
}

// ============================================================================
// VcvsDevice
// ============================================================================

VcvsDevice::VcvsDevice(const int node_a,
                       const int node_b,
                       const int control_positive,
                       const int control_negative,
                       const int branch,
                       const double gain)
    : m_NodeA(node_a), m_NodeB(node_b), m_ControlPositive(control_positive), m_ControlNegative(control_negative),
      m_Branch(branch), m_Gain(gain) {
}

void VcvsDevice::Stamp(MnaSystem &sys, const std::vector<double> &, const TimeContext &) {
    // Branch equation: v(a) - v(b) - gain * (v(cp) - v(cm)) = 0
    sys.AddA(m_NodeA, m_Branch, 1.0);
    sys.AddA(m_NodeB, m_Branch, -1.0);
    sys.AddA(m_Branch, m_NodeA, 1.0);
    sys.AddA(m_Branch, m_NodeB, -1.0);
    sys.AddA(m_Branch, m_ControlPositive, -m_Gain);
    sys.AddA(m_Branch, m_ControlNegative, m_Gain);
}

double VcvsDevice::TerminalCurrent(const int terminal, const std::vector<double> &x) const {
    const double current = x[static_cast<std::size_t>(m_Branch)];
    return terminal == 0 ? current : -current;
}

std::complex<double> VcvsDevice::AcTerminalCurrent(const int terminal,
                                                   const std::vector<double> & /*x_dc*/,
                                                   const std::vector<std::complex<double>> &x_ac,
                                                   const double /*omega*/) const {
    const std::complex<double> current = x_ac[static_cast<std::size_t>(m_Branch)];
    return terminal == 0 ? current : -current;
}

std::optional<SensitivityDescriptor> VcvsDevice::DescribeSensitivity() const {
    return SensitivityDescriptor{.Parameter = SensitivityParameter::GAIN, .Value = m_Gain, .IsGainParameter = true};
}

double VcvsDevice::DcSensitivityDerivative(const DcAdjointView &point) const {
    const double v_ctrl = point.VoltageAt(m_ControlPositive) - point.VoltageAt(m_ControlNegative);
    return point.AdjointAt(m_Branch) * v_ctrl;
}

std::complex<double> VcvsDevice::AcSensitivityDerivative(const AcAdjointView &point) const {
    const std::complex<double> direct =
        point.AdjointAt(m_Branch) * point.AcAcross(m_ControlPositive, m_ControlNegative);
    const std::complex<double> bias =
        point.BiasAdjointAt(m_Branch) * point.DcAcross(m_ControlPositive, m_ControlNegative);
    return direct + bias;
}

std::unique_ptr<Device> VcvsDevice::Clone() const {
    return std::make_unique<VcvsDevice>(*this);
}

} // namespace Nodalys
