/**
 * @file    passive_devices.cpp
 * @brief   Linear passive MNA devices: resistors, capacitors, inductors, and coupled inductors.
 */

#include "devices/passive/passive_devices.h"

#include "devices/stamp_helpers.h"
#include <complex>
#include <cstddef>

namespace Nodalys {

// ============================================================================
// CapacitorDevice
// ============================================================================
// Companion model for i = dq/dt, q = C v, over step h (formulas in integration.cpp):
//   i(t + h) = G_eq * v(t + h) + I_eq,  G_eq = a * C,  I_eq = b
// - Backward Euler (1st step): a = 1 / h,    b = -q(t) / h
// - Trapezoidal rule:          a = 2 / h,    b = -2 q(t) / h - i(t)
// - Gear-2:                    a = a0,       b = a1 q(t) + a2 q(t - hp)
// ============================================================================

CapacitorDevice::CapacitorDevice(const int node_a,
                                 const int node_b,
                                 const double capacitance,
                                 const double initial_voltage,
                                 const bool has_initial)
    : m_NodeA(node_a), m_NodeB(node_b), m_Capacitance(capacitance), m_InitialVoltage(initial_voltage),
      m_HasInitial(has_initial) {
    if (m_HasInitial) {
        m_History.Charge = m_Capacitance * m_InitialVoltage;
    }
}

void CapacitorDevice::TakeInitialVoltageFrom(const std::map<int, double> &node_voltages) {
    if (m_HasInitial) {
        return;
    }
    const auto voltage_of = [&node_voltages](const int node) {
        const auto found = node_voltages.find(node);
        return found != node_voltages.end() ? found->second : 0.0;
    };
    const double difference = voltage_of(m_NodeA) - voltage_of(m_NodeB);
    if (difference != 0.0) {
        m_InitialVoltage = difference;
        m_HasInitial = true;
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

std::complex<double> CapacitorDevice::AcTerminalCurrent(const int terminal,
                                                        const std::vector<double> & /*x_dc*/,
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

std::optional<SensitivityDescriptor> CapacitorDevice::DescribeSensitivity() const {
    return SensitivityDescriptor{.Parameter = SensitivityParameter::CAPACITANCE,
                                 .Value = m_Capacitance,
                                 .SubcircuitPrefix = "c.",
                                 .IsGainParameter = false};
}

double CapacitorDevice::DcSensitivityDerivative(const DcAdjointView & /*point*/) const {
    return 0.0;
}

std::complex<double> CapacitorDevice::AcSensitivityDerivative(const AcAdjointView &point) const {
    const std::complex<double> j_omega(0.0, point.Omega);
    return -j_omega * point.AdjointAcross(m_NodeA, m_NodeB) * point.AcAcross(m_NodeA, m_NodeB);
}

std::unique_ptr<Device> CapacitorDevice::Clone() const {
    return std::make_unique<CapacitorDevice>(*this);
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

CoupledInductorDevice::CoupledInductorDevice(const int primary_a,
                                             const int primary_b,
                                             const int secondary_a,
                                             const int secondary_b,
                                             const int branch1,
                                             const int branch2,
                                             const double primary_inductance,
                                             const double secondary_inductance,
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

std::complex<double> CoupledInductorDevice::AcTerminalCurrent(const int terminal,
                                                              const std::vector<double> & /*x_dc*/,
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

std::unique_ptr<Device> CoupledInductorDevice::Clone() const {
    return std::make_unique<CoupledInductorDevice>(*this);
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

InductorDevice::InductorDevice(const int node_a,
                               const int node_b,
                               const int branch,
                               const double inductance,
                               const double initial_current,
                               const bool has_initial)
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

std::complex<double> InductorDevice::AcTerminalCurrent(const int terminal,
                                                       const std::vector<double> & /*x_dc*/,
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

std::optional<SensitivityDescriptor> InductorDevice::DescribeSensitivity() const {
    return SensitivityDescriptor{.Parameter = SensitivityParameter::INDUCTANCE,
                                 .Value = m_Inductance,
                                 .SubcircuitPrefix = "l.",
                                 .IsGainParameter = false};
}

double InductorDevice::DcSensitivityDerivative(const DcAdjointView & /*point*/) const {
    return 0.0;
}

std::complex<double> InductorDevice::AcSensitivityDerivative(const AcAdjointView &point) const {
    const std::complex<double> j_omega(0.0, point.Omega);
    return j_omega * point.AdjointAt(m_Branch) * point.AcAt(m_Branch);
}

std::unique_ptr<Device> InductorDevice::Clone() const {
    return std::make_unique<InductorDevice>(*this);
}

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

std::complex<double> ResistorDevice::AcTerminalCurrent(const int terminal,
                                                       const std::vector<double> & /*x_dc*/,
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

bool ResistorDevice::ApplyLiveChange(const LiveQuantity quantity, const double value) {
    if (quantity != LiveQuantity::RESISTANCE) {
        return false;
    }
    SetResistance(value);
    return true;
}

std::optional<SensitivityDescriptor> ResistorDevice::DescribeSensitivity() const {
    const double resistance = GetResistance();
    if (resistance <= 0.0) {
        return std::nullopt;
    }
    return SensitivityDescriptor{.Parameter = SensitivityParameter::RESISTANCE,
                                 .Value = resistance,
                                 .SubcircuitPrefix = "r.",
                                 .IsGainParameter = false};
}

double ResistorDevice::DcSensitivityDerivative(const DcAdjointView &point) const {
    const double resistance = GetResistance();
    const double vr = point.VoltageAt(m_NodeA) - point.VoltageAt(m_NodeB);
    const double lr = point.AdjointAt(m_NodeA) - point.AdjointAt(m_NodeB);
    return (vr * lr) / (resistance * resistance);
}

std::complex<double> ResistorDevice::AcSensitivityDerivative(const AcAdjointView &point) const {
    const double resistance = GetResistance();
    const std::complex<double> direct = point.AdjointAcross(m_NodeA, m_NodeB) * point.AcAcross(m_NodeA, m_NodeB);
    const std::complex<double> bias = point.BiasAdjointAcross(m_NodeA, m_NodeB) * point.DcAcross(m_NodeA, m_NodeB);
    return (direct + bias) / (resistance * resistance);
}

std::unique_ptr<Device> ResistorDevice::Clone() const {
    return std::make_unique<ResistorDevice>(*this);
}

} // namespace Nodalys
