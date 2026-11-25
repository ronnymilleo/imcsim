/**
 * @file    independent_sources.cpp
 * @brief   Independent voltage and current sources and their waveform periodicity metadata.
 */

#include "devices/sources/independent_sources.h"

#include "devices/stamp_helpers.h"
#include <cstddef>
#include <utility>

namespace Nodalys {

// ============================================================================
// IndependentSourceDevice
// ============================================================================

/**
 * @brief   Returns the source's DC operating level.
 * @return  DC voltage in Volts or DC current in Amperes.
 */
double IndependentSourceDevice::GetDcValue() const {
    return m_DcValue;
}

/**
 * @brief   Updates the source's DC operating level (used by DC sweeps and live edits).
 * @param[in] value  New DC level in Volts or Amperes.
 */
void IndependentSourceDevice::SetDcValue(const double value) {
    m_DcValue = value;
}

/**
 * @brief   Installs the transient breakpoint callback for this source's waveform.
 * @param[in] breakpoints  Callback returning the next waveform corner strictly after a given time.
 */
void IndependentSourceDevice::SetBreakpoints(BreakpointCallback breakpoints) {
    m_Breakpoints = std::move(breakpoints);
}

/**
 * @brief   Installs the random seed callback for stochastic waveform sources.
 * @param[in] set_seed  Callback resetting the source's RNG stream from a 64-bit seed.
 */
void IndependentSourceDevice::SetSeedCallback(SeedCallback set_seed) {
    m_SetSeed = std::move(set_seed);
}

/**
 * @brief   Installs a factory creating independent callback state when the device is cloned.
 * @param[in] factory  Callback factory invoked by Clone().
 */
void IndependentSourceDevice::SetCallbackFactory(SourceCallbackFactory factory) {
    m_CallbackFactory = std::move(factory);
}

/**
 * @brief   Scales the source output level for the source-stepping homotopy fallback of Newton-Raphson.
 * @param[in] scale  Scale factor in `[0, 1]`.
 */
void IndependentSourceDevice::SetSourceScale(const double scale) {
    m_Scale = scale;
}

double IndependentSourceDevice::NextBreakpoint(const double time) const {
    return m_Breakpoints ? m_Breakpoints(time) : TransientDevice::NextBreakpoint(time);
}

/**
 * @brief   Assigns the deterministic 64-bit random stream seed for transient noise or random waveforms.
 * @param[in] seed  Independent 64-bit stream seed for this source.
 */
void IndependentSourceDevice::SetRandomSeed(const std::uint64_t seed) {
    if (m_SetSeed) {
        m_SetSeed(seed);
    }
}

/**
 * @brief   Constructs the shared base for an independent voltage or current source.
 * @param[in] node_a        Positive terminal MNA node index.
 * @param[in] node_b        Negative terminal MNA node index.
 * @param[in] dc_value      Nominal DC level.
 * @param[in] waveform      Waveform evaluation callback.
 * @param[in] ac_magnitude  Small-signal AC excitation magnitude.
 * @param[in] periodicity   Harmonic Balance periodicity metadata.
 */
IndependentSourceDevice::IndependentSourceDevice(const int node_a,
                                                 const int node_b,
                                                 const double dc_value,
                                                 WaveformCallback waveform,
                                                 const double ac_magnitude,
                                                 SourcePeriodicity periodicity)
    : m_NodeA(node_a), m_NodeB(node_b), m_DcValue(dc_value), m_Waveform(std::move(waveform)),
      m_AcMagnitude(ac_magnitude), m_Periodicity(periodicity) {
}

/**
 * @brief   Evaluates the scaled source value at the given analysis context.
 * @param[in] time  Analysis time context.
 * @return  Scaled source value in Volts or Amperes.
 */
double IndependentSourceDevice::EvaluateValue(const TimeContext &time) const {
    return m_Scale * m_Waveform(m_DcValue, time);
}

/**
 * @brief   Rebuilds stateful waveform callbacks after copying so cloned runs share no mutable RNG state.
 */
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

/**
 * @brief   Constructs an independent current source (`I` element).
 * @param[in] node_a        Positive terminal MNA node index (current leaves here).
 * @param[in] node_b        Negative terminal MNA node index (current enters here).
 * @param[in] dc_value      Nominal DC current in Amperes.
 * @param[in] waveform      Time-domain waveform callback.
 * @param[in] ac_magnitude  Small-signal AC magnitude in Amperes.
 * @param[in] periodicity   Harmonic Balance periodicity metadata.
 */
CurrentSourceDevice::CurrentSourceDevice(const int node_a,
                                         const int node_b,
                                         const double dc_value,
                                         WaveformCallback waveform,
                                         const double ac_magnitude,
                                         SourcePeriodicity periodicity)
    : IndependentSourceDevice(node_a, node_b, dc_value, std::move(waveform), ac_magnitude, periodicity) {
}

/**
 * @brief   Samples this current source's periodic steady-state terminal current for Harmonic Balance.
 * @param[in] terminal      Zero-based terminal index (`0` positive, `1` negative).
 * @param[in] time_seconds  Sample instant within the fundamental period in seconds.
 * @return  Terminal current in Amperes.
 */
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

/**
 * @brief   Constructs an independent voltage source (`V` element).
 * @param[in] node_a        Positive terminal MNA node index.
 * @param[in] node_b        Negative terminal MNA node index.
 * @param[in] branch        Allocated branch-current unknown index.
 * @param[in] dc_value      Nominal DC voltage in Volts.
 * @param[in] waveform      Time-domain waveform callback.
 * @param[in] ac_magnitude  Small-signal AC magnitude in Volts.
 * @param[in] periodicity   Harmonic Balance periodicity metadata.
 */
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
