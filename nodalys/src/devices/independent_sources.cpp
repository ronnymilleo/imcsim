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
// IndependentSourceDevice
// ============================================================================

double IndependentSourceDevice::GetDcValue() const {
    return m_DcValue;
}

void IndependentSourceDevice::SetDcValue(const double value) {
    m_DcValue = value;
}

void IndependentSourceDevice::SetBreakpoints(BreakpointCallback breakpoints) {
    m_Breakpoints = std::move(breakpoints);
}

void IndependentSourceDevice::SetSeedCallback(SeedCallback set_seed) {
    m_SetSeed = std::move(set_seed);
}

void IndependentSourceDevice::SetCallbackFactory(SourceCallbackFactory factory) {
    m_CallbackFactory = std::move(factory);
}

bool IndependentSourceDevice::SupportsHarmonicBalance() const {
    return m_Periodicity.IsSupportedInHb;
}

void IndependentSourceDevice::SetSourceScale(const double scale) {
    m_Scale = scale;
}

double IndependentSourceDevice::NextBreakpoint(const double time) const {
    return m_Breakpoints ? m_Breakpoints(time) : Device::NextBreakpoint(time);
}

void IndependentSourceDevice::SetRandomSeed(const std::uint64_t seed) {
    if (m_SetSeed) {
        m_SetSeed(seed);
    }
}

IndependentSourceDevice::IndependentSourceDevice(const int node_a,
                                                 const int node_b,
                                                 const double dc_value,
                                                 WaveformCallback waveform,
                                                 const double ac_magnitude,
                                                 SourcePeriodicity periodicity)
    : m_NodeA(node_a), m_NodeB(node_b), m_DcValue(dc_value), m_Waveform(std::move(waveform)),
      m_AcMagnitude(ac_magnitude), m_Periodicity(periodicity) {
}

double IndependentSourceDevice::EvaluateValue(const TimeContext &time) const {
    return m_Scale * m_Waveform(m_DcValue, time);
}

void IndependentSourceDevice::RefreshClonedCallbacks() {
    if (m_CallbackFactory) {
        SourceCallbacks callbacks = m_CallbackFactory();
        m_Waveform = std::move(callbacks.Evaluate);
        m_Breakpoints = std::move(callbacks.NextBreakpoint);
        m_SetSeed = std::move(callbacks.SetSeed);
    }
}

// ============================================================================
// CurrentSourceDevice
// ============================================================================

CurrentSourceDevice::CurrentSourceDevice(const int node_a,
                                         const int node_b,
                                         const double dc_value,
                                         WaveformCallback waveform,
                                         const double ac_magnitude,
                                         SourcePeriodicity periodicity)
    : IndependentSourceDevice(node_a, node_b, dc_value, std::move(waveform), ac_magnitude, periodicity) {
}

double CurrentSourceDevice::SamplePeriodicCurrent(const int terminal, const double time_seconds) const {
    const TimeContext steady_state{.IsPeriodicSteadyState = true, .Time = time_seconds};
    const double current = EvaluateValue(steady_state);
    return terminal == 0 ? current : -current;
}

void CurrentSourceDevice::Stamp(MnaSystem &sys, const std::vector<double> & /*x*/, const TimeContext &time) {
    m_Value = EvaluateValue(time);
    StampCurrentSource(sys, GetNodeA(), GetNodeB(), m_Value);
}

void CurrentSourceDevice::StampAc(MnaSystem &reactive, const std::vector<double> & /*x_dc*/) const {
    StampCurrentSource(reactive, GetNodeA(), GetNodeB(), GetAcMagnitude());
}

double CurrentSourceDevice::TerminalCurrent(const int terminal, const std::vector<double> & /*x*/) const {
    return terminal == 0 ? m_Value : -m_Value;
}

std::complex<double> CurrentSourceDevice::AcTerminalCurrent(const int terminal,
                                                            const std::vector<double> & /*x_dc*/,
                                                            const std::vector<std::complex<double>> & /*x_ac*/,
                                                            const double /*omega*/) const {
    const std::complex<double> current(GetAcMagnitude(), 0.0);
    return terminal == 0 ? current : -current;
}

std::optional<SensitivityDescriptor> CurrentSourceDevice::DescribeSensitivity() const {
    return SensitivityDescriptor{.Parameter = SensitivityParameter::CURRENT,
                                 .Value = GetDcValue(),
                                 .SubcircuitPrefix = "i.",
                                 .IsGainParameter = false};
}

double CurrentSourceDevice::DcSensitivityDerivative(const DcAdjointView &point) const {
    return point.AdjointAt(GetNodeB()) - point.AdjointAt(GetNodeA());
}

std::complex<double> CurrentSourceDevice::AcSensitivityDerivative(const AcAdjointView &point) const {
    return point.BiasAdjointAcross(GetNodeB(), GetNodeA());
}

std::unique_ptr<Device> CurrentSourceDevice::Clone() const {
    auto copy = std::make_unique<CurrentSourceDevice>(*this);
    copy->RefreshClonedCallbacks();
    return copy;
}

// ============================================================================
// VoltageSourceDevice
// ============================================================================

VoltageSourceDevice::VoltageSourceDevice(const int node_a,
                                         const int node_b,
                                         const int branch,
                                         const double dc_value,
                                         WaveformCallback waveform,
                                         const double ac_magnitude,
                                         SourcePeriodicity periodicity)
    : IndependentSourceDevice(node_a, node_b, dc_value, std::move(waveform), ac_magnitude, periodicity),
      m_Branch(branch) {
}

void VoltageSourceDevice::Stamp(MnaSystem &sys, const std::vector<double> & /*x*/, const TimeContext &time) {
    sys.AddA(GetNodeA(), m_Branch, 1.0);
    sys.AddA(GetNodeB(), m_Branch, -1.0);
    sys.AddA(m_Branch, GetNodeA(), 1.0);
    sys.AddA(m_Branch, GetNodeB(), -1.0);
    sys.AddRhs(m_Branch, EvaluateValue(time));
}

void VoltageSourceDevice::StampAc(MnaSystem &reactive, const std::vector<double> & /*x_dc*/) const {
    reactive.AddRhs(m_Branch, GetAcMagnitude());
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

bool VoltageSourceDevice::ApplyLiveChange(const LiveQuantity quantity, const double value) {
    if (quantity != LiveQuantity::DC_VOLTAGE) {
        return false;
    }
    SetDcValue(value);
    return true;
}

std::optional<SensitivityDescriptor> VoltageSourceDevice::DescribeSensitivity() const {
    return SensitivityDescriptor{.Parameter = SensitivityParameter::VOLTAGE,
                                 .Value = GetDcValue(),
                                 .SubcircuitPrefix = "v.",
                                 .IsGainParameter = false};
}

double VoltageSourceDevice::DcSensitivityDerivative(const DcAdjointView &point) const {
    return point.AdjointAt(m_Branch);
}

std::complex<double> VoltageSourceDevice::AcSensitivityDerivative(const AcAdjointView &point) const {
    return point.BiasAdjointAt(m_Branch);
}

std::unique_ptr<Device> VoltageSourceDevice::Clone() const {
    auto copy = std::make_unique<VoltageSourceDevice>(*this);
    copy->RefreshClonedCallbacks();
    return copy;
}

} // namespace Nodalys
