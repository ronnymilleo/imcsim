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

#include "native/devices.h"

#include <cmath>
#include <utility>

namespace Core::Native {

namespace {

double NodeVoltage(const std::vector<double> &x, const int index) {
    return index < 0 ? 0.0 : x[static_cast<std::size_t>(index)];
}

/// Voltage drop $v(a) - v(b)$; ground (`index < 0`) is $0\text{ V}$.
double BranchVoltage(const std::vector<double> &x, const int node_a, const int node_b) {
    return NodeVoltage(x, node_a) - NodeVoltage(x, node_b);
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

ResistorDevice::ResistorDevice(const int node_a, const int node_b, const double resistance)
    : m_NodeA(node_a), m_NodeB(node_b), m_Conductance(1.0 / resistance) {
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

// ============================================================================
// CapacitorDevice
// ============================================================================
// Companion model for i = C dv/dt over step h:
//   i(t + h) = G_eq * v(t + h) + I_eq
// - Backward Euler (1st step): G_eq = C / h,    I_eq = -G_eq * v(t)
// - Trapezoidal rule:          G_eq = 2C / h,   I_eq = -G_eq * v(t) - i(t)
// ============================================================================

CapacitorDevice::CapacitorDevice(const int node_a, const int node_b, const double capacitance,
                                 const double initial_voltage, const bool has_initial)
    : m_NodeA(node_a), m_NodeB(node_b), m_Capacitance(capacitance), m_InitialVoltage(initial_voltage),
      m_HasInitial(has_initial) {
    if (m_HasInitial) {
        m_Voltage = m_InitialVoltage;
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
    m_LastConductance = time.BackwardEuler ? m_Capacitance / time.TimeStep : 2.0 * m_Capacitance / time.TimeStep;
    m_EquivalentCurrent = -m_LastConductance * m_Voltage - (time.BackwardEuler ? 0.0 : m_Current);
    StampConductance(sys, m_NodeA, m_NodeB, m_LastConductance);
    StampCurrentSource(sys, m_NodeA, m_NodeB, m_EquivalentCurrent);
}

double CapacitorDevice::TerminalCurrent(const int terminal, const std::vector<double> &x) const {
    const double voltage = BranchVoltage(x, m_NodeA, m_NodeB);
    const double current = m_LastConductance * voltage + m_EquivalentCurrent;
    return terminal == 0 ? current : -current;
}

bool CapacitorDevice::HasTransientState() const {
    return true;
}

void CapacitorDevice::AcceptStep(const std::vector<double> &x) {
    const double voltage = BranchVoltage(x, m_NodeA, m_NodeB);
    m_Current = m_LastConductance * voltage + m_EquivalentCurrent;
    m_Voltage = voltage;
    m_SavedVoltage = m_Voltage;
    m_SavedCurrent = m_Current;
}

void CapacitorDevice::RejectStep() {
    m_Voltage = m_SavedVoltage;
    m_Current = m_SavedCurrent;
}

// ============================================================================
// InductorDevice
// ============================================================================
// Companion model for v = L di/dt over step h (stamped on the branch row):
//   v(t + h) - R_eq * i(t + h) = V_eq
// - DC operating point:        R_eq = 0,        V_eq = 0  (exact short circuit)
// - Backward Euler (1st step): R_eq = L / h,    V_eq = -R_eq * i(t)
// - Trapezoidal rule:          R_eq = 2L / h,   V_eq = -R_eq * i(t) - v(t)
// ============================================================================

InductorDevice::InductorDevice(const int node_a, const int node_b, const int branch, const double inductance,
                               const double initial_current, const bool has_initial)
    : m_NodeA(node_a), m_NodeB(node_b), m_Branch(branch), m_Inductance(inductance), m_InitialCurrent(initial_current),
      m_HasInitial(has_initial) {
    if (m_HasInitial) {
        m_Current = m_InitialCurrent;
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
            resistance = time.BackwardEuler ? m_Inductance / time.TimeStep : 2.0 * m_Inductance / time.TimeStep;
            equivalent_voltage = time.BackwardEuler ? -resistance * m_Current : -(resistance * m_Current + m_Voltage);
        }
    }
    sys.AddA(m_Branch, m_Branch, -resistance);
    sys.AddRhs(m_Branch, equivalent_voltage);
}

double InductorDevice::TerminalCurrent(const int terminal, const std::vector<double> &x) const {
    const double current = x[static_cast<std::size_t>(m_Branch)];
    return terminal == 0 ? current : -current;
}

bool InductorDevice::HasTransientState() const {
    return true;
}

void InductorDevice::AcceptStep(const std::vector<double> &x) {
    m_Current = x[static_cast<std::size_t>(m_Branch)];
    m_Voltage = BranchVoltage(x, m_NodeA, m_NodeB);
    m_SavedCurrent = m_Current;
    m_SavedVoltage = m_Voltage;
}

void InductorDevice::RejectStep() {
    m_Current = m_SavedCurrent;
    m_Voltage = m_SavedVoltage;
}

// ============================================================================
// VoltageSourceDevice & CurrentSourceDevice
// ============================================================================

VoltageSourceDevice::VoltageSourceDevice(const int node_a, const int node_b, const int branch, const double dc_value,
                                         Waveform waveform)
    : m_NodeA(node_a), m_NodeB(node_b), m_Branch(branch), m_DcValue(dc_value), m_Waveform(std::move(waveform)) {
}

double VoltageSourceDevice::GetDcValue() const {
    return m_DcValue;
}

void VoltageSourceDevice::SetDcValue(const double value) {
    m_DcValue = value;
}

void VoltageSourceDevice::Stamp(MnaSystem &sys, const std::vector<double> & /*x*/, const TimeContext &time) {
    sys.AddA(m_NodeA, m_Branch, 1.0);
    sys.AddA(m_NodeB, m_Branch, -1.0);
    sys.AddA(m_Branch, m_NodeA, 1.0);
    sys.AddA(m_Branch, m_NodeB, -1.0);
    sys.AddRhs(m_Branch, m_Scale * m_Waveform(m_DcValue, time));
}

void VoltageSourceDevice::SetSourceScale(const double scale) {
    m_Scale = scale;
}

double VoltageSourceDevice::TerminalCurrent(const int terminal, const std::vector<double> &x) const {
    const double current = x[static_cast<std::size_t>(m_Branch)];
    return terminal == 0 ? current : -current;
}

CurrentSourceDevice::CurrentSourceDevice(const int node_a, const int node_b, const double dc_value,
                                         VoltageSourceDevice::Waveform waveform)
    : m_NodeA(node_a), m_NodeB(node_b), m_DcValue(dc_value), m_Waveform(std::move(waveform)) {
}

double CurrentSourceDevice::GetDcValue() const {
    return m_DcValue;
}

void CurrentSourceDevice::SetDcValue(const double value) {
    m_DcValue = value;
}

void CurrentSourceDevice::Stamp(MnaSystem &sys, const std::vector<double> & /*x*/, const TimeContext &time) {
    m_Value = m_Scale * m_Waveform(m_DcValue, time);
    StampCurrentSource(sys, m_NodeA, m_NodeB, m_Value);
}

void CurrentSourceDevice::SetSourceScale(const double scale) {
    m_Scale = scale;
}

double CurrentSourceDevice::TerminalCurrent(const int terminal, const std::vector<double> & /*x*/) const {
    return terminal == 0 ? m_Value : -m_Value;
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

void SwitchDevice::AcceptStep(const std::vector<double> & /*x*/) {
    m_SavedIsClosed = m_IsClosed;
}

void SwitchDevice::RejectStep() {
    m_IsClosed = m_SavedIsClosed;
    m_Resistance = m_IsClosed ? m_Parameters.OnResistance : m_Parameters.OffResistance;
}

// ============================================================================
// CoupledInductorDevice (Transformer windings)
// ============================================================================
// Constitutive equations:
//   v1 = L1 * di1/dt + M * di2/dt
//   v2 = M  * di1/dt + L2 * di2/dt
// Discretized with Trapezoidal rule (scale = 2/h) or Backward Euler (scale = 1/h):
//   v1(t+h) - scale*L1*i1(t+h) - scale*M*i2(t+h) = -scale*(L1*i1(t) + M*i2(t)) - v1(t)
//   v2(t+h) - scale*M*i1(t+h) - scale*L2*i2(t+h) = -scale*(M*i1(t) + L2*i2(t)) - v2(t)
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
        const double factor = time.BackwardEuler ? 1.0 : 2.0;
        const double scale = factor / time.TimeStep;
        sys.AddA(m_Branch1, m_Branch1, -scale * m_L1);
        sys.AddA(m_Branch1, m_Branch2, -scale * m_M);
        sys.AddRhs(m_Branch1, -scale * (m_L1 * m_I1 + m_M * m_I2) - (time.BackwardEuler ? 0.0 : m_V1));
        sys.AddA(m_Branch2, m_Branch1, -scale * m_M);
        sys.AddA(m_Branch2, m_Branch2, -scale * m_L2);
        sys.AddRhs(m_Branch2, -scale * (m_M * m_I1 + m_L2 * m_I2) - (time.BackwardEuler ? 0.0 : m_V2));
    }
}

double CoupledInductorDevice::TerminalCurrent(const int terminal, const std::vector<double> &x) const {
    const int branch = terminal == 0 ? m_Branch1 : m_Branch2;
    return x[static_cast<std::size_t>(branch)];
}

bool CoupledInductorDevice::HasTransientState() const {
    return true;
}

void CoupledInductorDevice::AcceptStep(const std::vector<double> &x) {
    m_I1 = x[static_cast<std::size_t>(m_Branch1)];
    m_I2 = x[static_cast<std::size_t>(m_Branch2)];
    m_V1 = BranchVoltage(x, m_PrimaryA, m_PrimaryB);
    m_V2 = BranchVoltage(x, m_SecondaryA, m_SecondaryB);
    m_SavedI1 = m_I1;
    m_SavedI2 = m_I2;
    m_SavedV1 = m_V1;
    m_SavedV2 = m_V2;
}

void CoupledInductorDevice::RejectStep() {
    m_I1 = m_SavedI1;
    m_I2 = m_SavedI2;
    m_V1 = m_SavedV1;
    m_V2 = m_SavedV2;
}

} // namespace Core::Native
