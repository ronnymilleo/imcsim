/**
 * @file    devices.cpp
 * @brief   Linear and piecewise-linear MNA devices: resistors, capacitors, inductors, independent/controlled sources,
 *          voltage-controlled switches, and coupled inductors (transformers).
 * @details
 * ### MNA Stamp Reference
 * Every device adds its contribution ("stamp") to the system $A \cdot x = \text{rhs}$:
 *
 * 1. **Conductance $G$ between `node_a` and `node_b`**:
 *    Current $i = G \cdot (v_a - v_b)$ leaves `node_a` and enters `node_b`:
 *    ```text
 *                col a    col b
 *      row a  [   +G       -G   ]
 *      row b  [   -G       +G   ]
 *    ```
 *
 * 2. **Independent current source $I$ flowing from `node_a` to `node_b`**:
 *    In KCL ($\sum i_{\text{leaving via branches}} = -\sum i_{\text{sources}}$):
 *    ```text
 *      rhs(a) += -I
 *      rhs(b) += +I
 *    ```
 *
 * 3. **Voltage-defined branch (`branch`) from `node_a` to `node_b`**:
 *    Carries unknown current $i_{\text{branch}}$ (positive from `node_a` to `node_b`) with branch equation
 *    $v_a - v_b - R \cdot i_{\text{branch}} = V_{\text{eq}}$:
 *    ```text
 *                    col a   col b   col branch
 *      row a       [   .       .        +1      ]
 *      row b       [   .       .        -1      ]
 *      row branch  [  +1      -1        -R      ]  =  [ V_eq ]
 *    ```
 */

#include "devices/devices.h"

#include "devices/pn_junction.h"
#include <cmath>
#include <utility>

namespace Nodalys {

namespace {

double NodeVoltage(const std::vector<double> &x, const int index) {
    return index < 0 ? 0.0 : x[static_cast<std::size_t>(index)];
}

/// Voltage drop $v(a) - v(b)$; ground (`index < 0`) is $0\text{ V}$.
double BranchVoltage(const std::vector<double> &x, const int node_a, const int node_b) {
    return NodeVoltage(x, node_a) - NodeVoltage(x, node_b);
}

std::complex<double> AcNodeVoltage(const std::vector<std::complex<double>> &x, const int index) {
    return index < 0 ? std::complex<double>(0.0, 0.0) : x[static_cast<std::size_t>(index)];
}

std::complex<double> AcBranchVoltage(const std::vector<std::complex<double>> &x, const int node_a, const int node_b) {
    return AcNodeVoltage(x, node_a) - AcNodeVoltage(x, node_b);
}

void StampConductance(MnaSystem &sys, const int node_a, const int node_b, const double conductance) {
    sys.AddA(node_a, node_a, conductance);
    sys.AddA(node_b, node_b, conductance);
    sys.AddA(node_a, node_b, -conductance);
    sys.AddA(node_b, node_a, -conductance);
}

/// Stamps a current source pushing `current` from `node_a` into `node_b`.
void StampCurrentSource(MnaSystem &sys, const int node_a, const int node_b, const double current) {
    sys.AddRhs(node_a, -current);
    sys.AddRhs(node_b, current);
}

} // namespace

// ============================================================================
// ResistorDevice
// ============================================================================

ResistorDevice::ResistorDevice(const int node_a, const int node_b, const double resistance, const bool noisy)
    : m_NodeA(node_a), m_NodeB(node_b), m_Conductance(1.0 / resistance), m_Noisy(noisy) {
}

double ResistorDevice::GetResistance() const {
    return 1.0 / m_Conductance;
}

void ResistorDevice::SetResistance(const double resistance) {
    m_Conductance = 1.0 / resistance;
}

void ResistorDevice::Stamp(MnaSystem &sys, const std::vector<double> & /*x*/, const TimeContext & /*time*/) {
    StampConductance(sys, m_NodeA, m_NodeB, m_Conductance);
}

double ResistorDevice::TerminalCurrent(const int terminal, const std::vector<double> &x) const {
    const double current = m_Conductance * BranchVoltage(x, m_NodeA, m_NodeB);
    return terminal == 0 ? current : -current;
}

std::complex<double> ResistorDevice::AcTerminalCurrent(const int terminal, const std::vector<double> & /*x_dc*/,
                                                       const std::vector<std::complex<double>> &x_ac,
                                                       const double /*omega*/) const {
    const std::complex<double> current = m_Conductance * AcBranchVoltage(x_ac, m_NodeA, m_NodeB);
    return terminal == 0 ? current : -current;
}

double ResistorDevice::OutputNoiseVariance(const std::vector<double> & /*x_dc*/,
                                           const std::vector<std::complex<double>> &adjoint,
                                           const double /*frequency_hz*/) const {
    if (!m_Noisy) {
        return 0.0;
    }
    const std::complex<double> transfer = AcBranchVoltage(adjoint, m_NodeA, m_NodeB);
    return m_FourKt * m_Conductance * std::norm(transfer);
}

/**
 * @brief   Moves the thermal noise of the resistor to the temperature of the run.
 * @param[in] thermal  Temperature of the run; the resistance itself does not change.
 */
void ResistorDevice::SetTemperature(const ThermalConditions &thermal) {
    m_FourKt = GetFourKt(thermal);
}

// ============================================================================
// CapacitorDevice
// ============================================================================
// Companion model for i = dq/dt, q = C v, over step h (formulas in integration.cpp):
//   i(t + h) = G_eq * v(t + h) + I_eq,  G_eq = a * C,  I_eq = b
// - Backward Euler (1st step): a = 1 / h,    b = -q(t) / h
// - Trapezoidal rule:          a = 2 / h,    b = -2 q(t) / h - i(t)
// - Gear-2:                    a = a0,       b = a1 q(t) + a2 q(t - hp)
// ============================================================================

CapacitorDevice::CapacitorDevice(const int node_a, const int node_b, const double capacitance,
                                 const double initial_voltage, const bool has_initial)
    : m_NodeA(node_a), m_NodeB(node_b), m_Capacitance(capacitance), m_InitialVoltage(initial_voltage),
      m_HasInitial(has_initial) {
    if (m_HasInitial) {
        m_History.Charge = m_Capacitance * m_InitialVoltage;
    }
}

void CapacitorDevice::Stamp(MnaSystem &sys, const std::vector<double> &, const TimeContext &time) {
    if (!time.IsTransient) {
        m_LastConductance = 0.0;
        m_EquivalentCurrent = 0.0;
        return;
    }
    if (time.UseInitialConditions && m_HasInitial && time.Time == 0.0) {
        // Force v(0) = m_InitialVoltage at t = 0 using a stiff Norton companion
        constexpr double UIC_CONDUCTANCE = 1e9;
        StampConductance(sys, m_NodeA, m_NodeB, UIC_CONDUCTANCE);
        StampCurrentSource(sys, m_NodeA, m_NodeB, -UIC_CONDUCTANCE * m_InitialVoltage);
        m_LastConductance = 0.0;
        m_EquivalentCurrent = 0.0;
        return;
    }
    const Companion companion = MakeCompanion(m_History, time);
    m_LastConductance = companion.Coefficient * m_Capacitance;
    m_EquivalentCurrent = companion.Offset;
    StampConductance(sys, m_NodeA, m_NodeB, m_LastConductance);
    StampCurrentSource(sys, m_NodeA, m_NodeB, m_EquivalentCurrent);
}

void CapacitorDevice::StampAc(MnaSystem &reactive, const std::vector<double> & /*x_dc*/) const {
    StampConductance(reactive, m_NodeA, m_NodeB, m_Capacitance);
}

double CapacitorDevice::TerminalCurrent(const int terminal, const std::vector<double> &x) const {
    const double voltage = BranchVoltage(x, m_NodeA, m_NodeB);
    const double current = m_LastConductance * voltage + m_EquivalentCurrent;
    return terminal == 0 ? current : -current;
}

std::complex<double> CapacitorDevice::AcTerminalCurrent(const int terminal, const std::vector<double> & /*x_dc*/,
                                                        const std::vector<std::complex<double>> &x_ac,
                                                        const double omega) const {
    const std::complex<double> current =
        std::complex<double>(0.0, omega * m_Capacitance) * AcBranchVoltage(x_ac, m_NodeA, m_NodeB);
    return terminal == 0 ? current : -current;
}

bool CapacitorDevice::HasTransientState() const {
    return true;
}

void CapacitorDevice::AcceptStep(const std::vector<double> &x) {
    const double voltage = BranchVoltage(x, m_NodeA, m_NodeB);
    const double current = m_LastConductance * voltage + m_EquivalentCurrent;
    m_History = AdvanceHistory(m_History, m_Capacitance * voltage, current);
    m_SavedHistory = m_History;
}

void CapacitorDevice::RejectStep() {
    m_History = m_SavedHistory;
}

// ============================================================================
// InductorDevice
// ============================================================================
// Companion model for v = dphi/dt, phi = L i, over step h (stamped on the branch row; formulas in integration.cpp):
//   v(t + h) - R_eq * i(t + h) = V_eq,  R_eq = a * L,  V_eq = b
// - DC operating point:        R_eq = 0,        V_eq = 0  (exact short circuit)
// - Backward Euler (1st step): a = 1 / h,    b = -phi(t) / h
// - Trapezoidal rule:          a = 2 / h,    b = -2 phi(t) / h - v(t)
// - Gear-2:                    a = a0,       b = a1 phi(t) + a2 phi(t - hp)
// ============================================================================

InductorDevice::InductorDevice(const int node_a, const int node_b, const int branch, const double inductance,
                               const double initial_current, const bool has_initial)
    : m_NodeA(node_a), m_NodeB(node_b), m_Branch(branch), m_Inductance(inductance), m_InitialCurrent(initial_current),
      m_HasInitial(has_initial) {
    if (m_HasInitial) {
        m_History.Charge = m_Inductance * m_InitialCurrent;
    }
}

void InductorDevice::Stamp(MnaSystem &sys, const std::vector<double> &, const TimeContext &time) {
    sys.AddA(m_NodeA, m_Branch, 1.0);
    sys.AddA(m_NodeB, m_Branch, -1.0);
    sys.AddA(m_Branch, m_NodeA, 1.0);
    sys.AddA(m_Branch, m_NodeB, -1.0);
    double resistance = 0.0;
    double equivalent_voltage = 0.0;
    if (time.IsTransient) {
        if (time.UseInitialConditions && m_HasInitial && time.Time == 0.0) {
            resistance = 1e9;
            equivalent_voltage = -resistance * m_InitialCurrent;
        } else {
            const Companion companion = MakeCompanion(m_History, time);
            resistance = companion.Coefficient * m_Inductance;
            equivalent_voltage = companion.Offset;
        }
    }
    sys.AddA(m_Branch, m_Branch, -resistance);
    sys.AddRhs(m_Branch, equivalent_voltage);
}

void InductorDevice::StampAc(MnaSystem &reactive, const std::vector<double> & /*x_dc*/) const {
    reactive.AddA(m_Branch, m_Branch, -m_Inductance);
}

double InductorDevice::TerminalCurrent(const int terminal, const std::vector<double> &x) const {
    const double current = x[static_cast<std::size_t>(m_Branch)];
    return terminal == 0 ? current : -current;
}

std::complex<double> InductorDevice::AcTerminalCurrent(const int terminal, const std::vector<double> & /*x_dc*/,
                                                       const std::vector<std::complex<double>> &x_ac,
                                                       const double /*omega*/) const {
    const std::complex<double> current = x_ac[static_cast<std::size_t>(m_Branch)];
    return terminal == 0 ? current : -current;
}

bool InductorDevice::HasTransientState() const {
    return true;
}

void InductorDevice::AcceptStep(const std::vector<double> &x) {
    const double current = x[static_cast<std::size_t>(m_Branch)];
    m_History = AdvanceHistory(m_History, m_Inductance * current, BranchVoltage(x, m_NodeA, m_NodeB));
    m_SavedHistory = m_History;
}

void InductorDevice::RejectStep() {
    m_History = m_SavedHistory;
}

// ============================================================================
// VoltageSourceDevice & CurrentSourceDevice
// ============================================================================

VoltageSourceDevice::VoltageSourceDevice(const int node_a, const int node_b, const int branch, const double dc_value,
                                         Waveform waveform, const double ac_magnitude, SourcePeriodicity periodicity)
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

double VoltageSourceDevice::TerminalCurrent(const int terminal, const std::vector<double> &x) const {
    const double current = x[static_cast<std::size_t>(m_Branch)];
    return terminal == 0 ? current : -current;
}

std::complex<double> VoltageSourceDevice::AcTerminalCurrent(const int terminal, const std::vector<double> & /*x_dc*/,
                                                            const std::vector<std::complex<double>> &x_ac,
                                                            const double /*omega*/) const {
    const std::complex<double> current = x_ac[static_cast<std::size_t>(m_Branch)];
    return terminal == 0 ? current : -current;
}

CurrentSourceDevice::CurrentSourceDevice(const int node_a, const int node_b, const double dc_value,
                                         VoltageSourceDevice::Waveform waveform, const double ac_magnitude,
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

double CurrentSourceDevice::TerminalCurrent(const int terminal, const std::vector<double> & /*x*/) const {
    return terminal == 0 ? m_Value : -m_Value;
}

std::complex<double> CurrentSourceDevice::AcTerminalCurrent(const int terminal, const std::vector<double> & /*x_dc*/,
                                                            const std::vector<std::complex<double>> & /*x_ac*/,
                                                            const double /*omega*/) const {
    const std::complex<double> current(m_AcMagnitude, 0.0);
    return terminal == 0 ? current : -current;
}

// ============================================================================
// Controlled Sources: VCVS (E), VCCS (G), CCCS (F), CCVS (H)
// ============================================================================

VcvsDevice::VcvsDevice(const int node_a, const int node_b, const int control_positive, const int control_negative,
                       const int branch, const double gain)
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

std::complex<double> VcvsDevice::AcTerminalCurrent(const int terminal, const std::vector<double> & /*x_dc*/,
                                                   const std::vector<std::complex<double>> &x_ac,
                                                   const double /*omega*/) const {
    const std::complex<double> current = x_ac[static_cast<std::size_t>(m_Branch)];
    return terminal == 0 ? current : -current;
}

VccsDevice::VccsDevice(const int node_a, const int node_b, const int control_positive, const int control_negative,
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

std::complex<double> VccsDevice::AcTerminalCurrent(const int terminal, const std::vector<double> & /*x_dc*/,
                                                   const std::vector<std::complex<double>> &x_ac,
                                                   const double /*omega*/) const {
    const std::complex<double> current = m_Gain * AcBranchVoltage(x_ac, m_ControlPositive, m_ControlNegative);
    return terminal == 0 ? current : -current;
}

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

std::complex<double> CccsDevice::AcTerminalCurrent(const int terminal, const std::vector<double> & /*x_dc*/,
                                                   const std::vector<std::complex<double>> &x_ac,
                                                   const double /*omega*/) const {
    const std::complex<double> current = m_Gain * x_ac[static_cast<std::size_t>(m_ControlBranch)];
    return terminal == 0 ? current : -current;
}

CcvsDevice::CcvsDevice(const int node_a, const int node_b, const int branch, const int control_branch,
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

std::complex<double> CcvsDevice::AcTerminalCurrent(const int terminal, const std::vector<double> & /*x_dc*/,
                                                   const std::vector<std::complex<double>> &x_ac,
                                                   const double /*omega*/) const {
    const std::complex<double> current = x_ac[static_cast<std::size_t>(m_Branch)];
    return terminal == 0 ? current : -current;
}

// ============================================================================
// SwitchDevice
// ============================================================================

SwitchDevice::SwitchDevice(const int node_a, const int node_b, const int control_positive, const int control_negative,
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

std::complex<double> SwitchDevice::AcTerminalCurrent(const int terminal, const std::vector<double> & /*x_dc*/,
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

// ============================================================================
// CoupledInductorDevice (Transformer windings)
// ============================================================================
// Constitutive equations:
//   v1 = L1 * di1/dt + M * di2/dt
//   v2 = M  * di1/dt + L2 * di2/dt
// With the fluxes phi1 = L1*i1 + M*i2 and phi2 = M*i1 + L2*i2, v = dphi/dt discretizes (integration.cpp) as
//   v1(t+h) - a*L1*i1(t+h) - a*M*i2(t+h) = b1
//   v2(t+h) - a*M*i1(t+h) - a*L2*i2(t+h) = b2
// where a and b are the coefficient and the offset of the integration method, b taken from the history of each flux.
// ============================================================================

CoupledInductorDevice::CoupledInductorDevice(const int primary_a, const int primary_b, const int secondary_a,
                                             const int secondary_b, const int branch1, const int branch2,
                                             const double primary_inductance, const double secondary_inductance,
                                             const double mutual)
    : m_PrimaryA(primary_a), m_PrimaryB(primary_b), m_SecondaryA(secondary_a), m_SecondaryB(secondary_b),
      m_Branch1(branch1), m_Branch2(branch2), m_L1(primary_inductance), m_L2(secondary_inductance), m_M(mutual) {
}

void CoupledInductorDevice::Stamp(MnaSystem &sys, const std::vector<double> &, const TimeContext &time) {
    sys.AddA(m_PrimaryA, m_Branch1, 1.0);
    sys.AddA(m_PrimaryB, m_Branch1, -1.0);
    sys.AddA(m_SecondaryA, m_Branch2, 1.0);
    sys.AddA(m_SecondaryB, m_Branch2, -1.0);
    sys.AddA(m_Branch1, m_PrimaryA, 1.0);
    sys.AddA(m_Branch1, m_PrimaryB, -1.0);
    sys.AddA(m_Branch2, m_SecondaryA, 1.0);
    sys.AddA(m_Branch2, m_SecondaryB, -1.0);
    if (time.IsTransient) {
        const Companion first = MakeCompanion(m_History1, time);
        const Companion second = MakeCompanion(m_History2, time);
        const double scale = first.Coefficient;
        sys.AddA(m_Branch1, m_Branch1, -scale * m_L1);
        sys.AddA(m_Branch1, m_Branch2, -scale * m_M);
        sys.AddRhs(m_Branch1, first.Offset);
        sys.AddA(m_Branch2, m_Branch1, -scale * m_M);
        sys.AddA(m_Branch2, m_Branch2, -scale * m_L2);
        sys.AddRhs(m_Branch2, second.Offset);
    }
}

void CoupledInductorDevice::StampAc(MnaSystem &reactive, const std::vector<double> & /*x_dc*/) const {
    reactive.AddA(m_Branch1, m_Branch1, -m_L1);
    reactive.AddA(m_Branch1, m_Branch2, -m_M);
    reactive.AddA(m_Branch2, m_Branch1, -m_M);
    reactive.AddA(m_Branch2, m_Branch2, -m_L2);
}

double CoupledInductorDevice::TerminalCurrent(const int terminal, const std::vector<double> &x) const {
    const int branch = terminal == 0 ? m_Branch1 : m_Branch2;
    return x[static_cast<std::size_t>(branch)];
}

std::complex<double> CoupledInductorDevice::AcTerminalCurrent(const int terminal, const std::vector<double> & /*x_dc*/,
                                                              const std::vector<std::complex<double>> &x_ac,
                                                              const double /*omega*/) const {
    const int branch = terminal == 0 ? m_Branch1 : m_Branch2;
    return x_ac[static_cast<std::size_t>(branch)];
}

bool CoupledInductorDevice::HasTransientState() const {
    return true;
}

void CoupledInductorDevice::AcceptStep(const std::vector<double> &x) {
    const double current1 = x[static_cast<std::size_t>(m_Branch1)];
    const double current2 = x[static_cast<std::size_t>(m_Branch2)];
    m_History1 = AdvanceHistory(m_History1, m_L1 * current1 + m_M * current2, BranchVoltage(x, m_PrimaryA, m_PrimaryB));
    m_History2 =
        AdvanceHistory(m_History2, m_M * current1 + m_L2 * current2, BranchVoltage(x, m_SecondaryA, m_SecondaryB));
    m_SavedHistory1 = m_History1;
    m_SavedHistory2 = m_History2;
}

void CoupledInductorDevice::RejectStep() {
    m_History1 = m_SavedHistory1;
    m_History2 = m_SavedHistory2;
}

NodeInitialConditionDevice::NodeInitialConditionDevice(const int node, const double voltage)
    : m_Node(node), m_Voltage(voltage) {
}

void NodeInitialConditionDevice::Stamp(MnaSystem &sys, const std::vector<double> & /*x*/, const TimeContext &time) {
    if (!time.IsTransientStart) {
        return;
    }
    constexpr double IC_CONDUCTANCE = 1e9;
    StampConductance(sys, m_Node, -1, IC_CONDUCTANCE);
    StampCurrentSource(sys, m_Node, -1, -IC_CONDUCTANCE * m_Voltage);
}

double NodeInitialConditionDevice::TerminalCurrent(const int /*terminal*/, const std::vector<double> & /*x*/) const {
    return 0.0;
}

std::complex<double> NodeInitialConditionDevice::AcTerminalCurrent(const int /*terminal*/,
                                                                   const std::vector<double> & /*x_dc*/,
                                                                   const std::vector<std::complex<double>> & /*x_ac*/,
                                                                   const double /*omega*/) const {
    return {};
}

NodeSeedDevice::NodeSeedDevice(std::vector<NodeSeed> seeds) : m_Seeds(std::move(seeds)) {
}

void NodeSeedDevice::Stamp(MnaSystem & /*sys*/, const std::vector<double> & /*x*/, const TimeContext & /*time*/) {
}

void NodeSeedDevice::SeedOperatingPoint(std::vector<double> &x) const {
    for (const NodeSeed &seed : m_Seeds) {
        if (seed.Node >= 0 && static_cast<std::size_t>(seed.Node) < x.size()) {
            x[static_cast<std::size_t>(seed.Node)] = seed.Voltage;
        }
    }
}

double NodeSeedDevice::TerminalCurrent(const int /*terminal*/, const std::vector<double> & /*x*/) const {
    return 0.0;
}

std::complex<double> NodeSeedDevice::AcTerminalCurrent(const int /*terminal*/, const std::vector<double> & /*x_dc*/,
                                                       const std::vector<std::complex<double>> & /*x_ac*/,
                                                       const double /*omega*/) const {
    return {};
}

} // namespace Nodalys
