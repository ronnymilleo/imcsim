/**
 * @file    independent_sources.cpp
 * @brief   Independent voltage and current sources and their waveform periodicity metadata.
 */

#include "devices/independent_sources.h"

#include "devices/stamp_helpers.h"
#include <cstddef>
#include <utility>

namespace Nodalys {

// ============================================================================
// VoltageSourceDevice
// ============================================================================

VoltageSourceDevice::VoltageSourceDevice(const int node_a,
                                         const int node_b,
                                         const int branch,
                                         const double dc_value,
                                         Waveform waveform,
                                         const double ac_magnitude,
                                         SourcePeriodicity periodicity)
    : m_NodeA(node_a), m_NodeB(node_b), m_Branch(branch), m_DcValue(dc_value), m_Waveform(std::move(waveform)),
      m_AcMagnitude(ac_magnitude), m_Periodicity(periodicity) {
}

double VoltageSourceDevice::GetDcValue() const {
    return m_DcValue;
}

void VoltageSourceDevice::SetDcValue(const double value) {
    m_DcValue = value;
}

void VoltageSourceDevice::SetBreakpoints(Breakpoints breakpoints) {
    m_Breakpoints = std::move(breakpoints);
}

void VoltageSourceDevice::SetSeedCallback(std::function<void(std::uint64_t)> set_seed) {
    m_SetSeed = std::move(set_seed);
}

void VoltageSourceDevice::SetCallbackFactory(CallbackFactory factory) {
    m_CallbackFactory = std::move(factory);
}

void VoltageSourceDevice::Stamp(MnaSystem &sys, const std::vector<double> & /*x*/, const TimeContext &time) {
    sys.AddA(m_NodeA, m_Branch, 1.0);
    sys.AddA(m_NodeB, m_Branch, -1.0);
    sys.AddA(m_Branch, m_NodeA, 1.0);
    sys.AddA(m_Branch, m_NodeB, -1.0);
    sys.AddRhs(m_Branch, m_Scale * m_Waveform(m_DcValue, time));
}

void VoltageSourceDevice::StampAc(MnaSystem &reactive, const std::vector<double> & /*x_dc*/) const {
    reactive.AddRhs(m_Branch, m_AcMagnitude);
}

bool VoltageSourceDevice::SupportsHarmonicBalance() const {
    return m_Periodicity.IsSupportedInHb;
}

double VoltageSourceDevice::TerminalCurrent(const int terminal, const std::vector<double> &x) const {
    const double current = x[static_cast<std::size_t>(m_Branch)];
    return terminal == 0 ? current : -current;
}

std::complex<double> VoltageSourceDevice::AcTerminalCurrent(const int terminal,
                                                            const std::vector<double> & /*x_dc*/,
                                                            const std::vector<std::complex<double>> &x_ac,
                                                            const double /*omega*/) const {
    const std::complex<double> current = x_ac[static_cast<std::size_t>(m_Branch)];
    return terminal == 0 ? current : -current;
}

void VoltageSourceDevice::SetSourceScale(const double scale) {
    m_Scale = scale;
}

double VoltageSourceDevice::NextBreakpoint(const double time) const {
    return m_Breakpoints ? m_Breakpoints(time) : Device::NextBreakpoint(time);
}

void VoltageSourceDevice::SetRandomSeed(const std::uint64_t seed) {
    if (m_SetSeed) {
        m_SetSeed(seed);
    }
}

std::unique_ptr<Device> VoltageSourceDevice::Clone() const {
    auto copy = std::make_unique<VoltageSourceDevice>(*this);
    if (m_CallbackFactory) {
        CallbackSet callbacks = m_CallbackFactory();
        copy->m_Waveform = std::move(callbacks.Evaluate);
        copy->m_Breakpoints = std::move(callbacks.NextBreakpoint);
        copy->m_SetSeed = std::move(callbacks.SetSeed);
    }
    return copy;
}

// ============================================================================
// CurrentSourceDevice
// ============================================================================

CurrentSourceDevice::CurrentSourceDevice(const int node_a,
                                         const int node_b,
                                         const double dc_value,
                                         VoltageSourceDevice::Waveform waveform,
                                         const double ac_magnitude,
                                         SourcePeriodicity periodicity)
    : m_NodeA(node_a), m_NodeB(node_b), m_DcValue(dc_value), m_Waveform(std::move(waveform)),
      m_AcMagnitude(ac_magnitude), m_Periodicity(periodicity) {
}

double CurrentSourceDevice::GetDcValue() const {
    return m_DcValue;
}

void CurrentSourceDevice::SetDcValue(const double value) {
    m_DcValue = value;
}

double CurrentSourceDevice::SamplePeriodicCurrent(const int terminal, const double time_seconds) const {
    const TimeContext steady_state{.IsPeriodicSteadyState = true, .Time = time_seconds};
    const double current = m_Scale * m_Waveform(m_DcValue, steady_state);
    return terminal == 0 ? current : -current;
}

void CurrentSourceDevice::SetBreakpoints(VoltageSourceDevice::Breakpoints breakpoints) {
    m_Breakpoints = std::move(breakpoints);
}

void CurrentSourceDevice::SetSeedCallback(std::function<void(std::uint64_t)> set_seed) {
    m_SetSeed = std::move(set_seed);
}

void CurrentSourceDevice::SetCallbackFactory(VoltageSourceDevice::CallbackFactory factory) {
    m_CallbackFactory = std::move(factory);
}

void CurrentSourceDevice::Stamp(MnaSystem &sys, const std::vector<double> & /*x*/, const TimeContext &time) {
    m_Value = m_Scale * m_Waveform(m_DcValue, time);
    StampCurrentSource(sys, m_NodeA, m_NodeB, m_Value);
}

void CurrentSourceDevice::StampAc(MnaSystem &reactive, const std::vector<double> & /*x_dc*/) const {
    StampCurrentSource(reactive, m_NodeA, m_NodeB, m_AcMagnitude);
}

bool CurrentSourceDevice::SupportsHarmonicBalance() const {
    return m_Periodicity.IsSupportedInHb;
}

double CurrentSourceDevice::TerminalCurrent(const int terminal, const std::vector<double> & /*x*/) const {
    return terminal == 0 ? m_Value : -m_Value;
}

std::complex<double> CurrentSourceDevice::AcTerminalCurrent(const int terminal,
                                                            const std::vector<double> & /*x_dc*/,
                                                            const std::vector<std::complex<double>> & /*x_ac*/,
                                                            const double /*omega*/) const {
    const std::complex<double> current(m_AcMagnitude, 0.0);
    return terminal == 0 ? current : -current;
}

void CurrentSourceDevice::SetSourceScale(const double scale) {
    m_Scale = scale;
}

double CurrentSourceDevice::NextBreakpoint(const double time) const {
    return m_Breakpoints ? m_Breakpoints(time) : Device::NextBreakpoint(time);
}

void CurrentSourceDevice::SetRandomSeed(const std::uint64_t seed) {
    if (m_SetSeed) {
        m_SetSeed(seed);
    }
}

std::unique_ptr<Device> CurrentSourceDevice::Clone() const {
    auto copy = std::make_unique<CurrentSourceDevice>(*this);
    if (m_CallbackFactory) {
        VoltageSourceDevice::CallbackSet callbacks = m_CallbackFactory();
        copy->m_Waveform = std::move(callbacks.Evaluate);
        copy->m_Breakpoints = std::move(callbacks.NextBreakpoint);
        copy->m_SetSeed = std::move(callbacks.SetSeed);
    }
    return copy;
}

} // namespace Nodalys
