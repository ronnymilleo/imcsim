/**
 * @file    switch_device.cpp
 * @brief   Voltage-controlled switch with hysteresis (SPICE S model).
 */

#include "devices/passive/switch_device.h"

#include "devices/stamp_helpers.h"

namespace Nodalys {

SwitchDevice::SwitchDevice(const int node_a,
                           const int node_b,
                           const int control_positive,
                           const int control_negative,
                           SwitchParameters parameters)
    : m_NodeA(node_a), m_NodeB(node_b), m_ControlPositive(control_positive), m_ControlNegative(control_negative),
      m_Parameters(parameters), m_Resistance(parameters.OffResistance) {
}

void SwitchDevice::Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext & /*time*/) {
    const double control = NodeVoltage(x, m_ControlPositive) - NodeVoltage(x, m_ControlNegative);
    const SwitchParameters &p = m_Parameters;
    if (!m_IsClosed && control >= p.Threshold + p.Hysteresis) {
        m_IsClosed = true;
    } else if (m_IsClosed && control <= p.Threshold - p.Hysteresis) {
        m_IsClosed = false;
    }
    m_Resistance = m_IsClosed ? p.OnResistance : p.OffResistance;
    StampConductance(sys, m_NodeA, m_NodeB, 1.0 / m_Resistance);
}

double SwitchDevice::TerminalCurrent(const int terminal, const std::vector<double> &x) const {
    const double current = BranchVoltage(x, m_NodeA, m_NodeB) / m_Resistance;
    return terminal == 0 ? current : -current;
}

std::complex<double> SwitchDevice::AcTerminalCurrent(const int terminal,
                                                     const std::vector<double> & /*x_dc*/,
                                                     const std::vector<std::complex<double>> &x_ac,
                                                     const double /*omega*/) const {
    const std::complex<double> current = AcBranchVoltage(x_ac, m_NodeA, m_NodeB) / m_Resistance;
    return terminal == 0 ? current : -current;
}

void SwitchDevice::AcceptStep(const std::vector<double> & /*x*/) {
    m_SavedIsClosed = m_IsClosed;
}

void SwitchDevice::RejectStep() {
    m_IsClosed = m_SavedIsClosed;
    m_Resistance = m_IsClosed ? m_Parameters.OnResistance : m_Parameters.OffResistance;
}

bool SwitchDevice::HasSwitched() const {
    return m_IsClosed != m_SavedIsClosed;
}

std::unique_ptr<Device> SwitchDevice::Clone() const {
    return std::make_unique<SwitchDevice>(*this);
}

} // namespace Nodalys
