/**
 * @file    diode_device.cpp
 * @brief   Diode of Nodalys: the junction with its series resistance and depletion charge.
 * @details The helpers the nonlinear devices share are in semiconductor_math.h.
 */

#include "devices/diode_device.h"

#include "devices/pn_junction.h"
#include "devices/semiconductor_math.h"
#include <algorithm>
#include <cmath>

namespace Nodalys {

DiodeDevice::DiodeDevice(const int anode, const int cathode, const int internal, DiodeParameters parameters)
    : m_Anode(anode), m_Cathode(cathode), m_Internal(internal), m_JunctionNode(internal >= 0 ? internal : cathode),
      m_Given(parameters), m_Parameters(parameters), m_Vt(parameters.EmissionCoefficient * THERMAL_VOLTAGE),
      m_Critical(PnCriticalVoltage(std::max(parameters.SaturationCurrent, 1e-30), m_Vt)) {
    ApplyTemperature(m_Thermal);
}

/**
 * @brief   Scales the saturation current, the junction potential and the junction capacitance with the temperature.
 * @details The breakdown voltage and current keep their values: ngspice solves its breakdown knee again at the new
 *          temperature, but a model without `TCV` keeps the current at the breakdown voltage at $I_{BV}$, which is
 *          what the exponential here is anchored to. The exponent of the junction uses the emission coefficient, as
 *          ngspice divides the growth of $I_S$ by it. The series resistance and the transit time have no coefficient.
 */
void DiodeDevice::ApplyTemperature(const ThermalConditions &thermal) {
    m_Thermal = thermal;
    m_Parameters = m_Given;
    const double log_ratio =
        GetSaturationCurrentLogRatio(thermal, m_Given.EnergyGap, m_Given.SaturationCurrentExponent);
    m_Parameters.SaturationCurrent = m_Given.SaturationCurrent * std::exp(log_ratio / m_Given.EmissionCoefficient);
    m_Parameters.JunctionPotential = GetJunctionPotential(thermal, m_Given.JunctionPotential);
    m_Parameters.JunctionCapacitance = GetJunctionCapacitance(thermal, m_Given.JunctionCapacitance,
                                                              m_Given.JunctionPotential, m_Given.GradingCoefficient);
    m_Vt = m_Given.EmissionCoefficient * GetThermalVoltage(thermal);
    m_Critical = PnCriticalVoltage(std::max(m_Parameters.SaturationCurrent, 1e-30), m_Vt);
}

DiodeDevice::Junction DiodeDevice::Evaluate(const double voltage) const {
    const DiodeParameters &p = m_Parameters;
    // Forward and reverse Shockley current + smooth reverse breakdown current:
    //   I(v) = IS * (exp(v / m_Vt) - 1) - IBV * (exp(-(v + BV) / m_Vt) - exp(-BV / m_Vt))
    // Subtracting exp(-BV / m_Vt) keeps I(0) = 0 exact while avoiding any jump discontinuity at v = -BV.
    const double fwd_exp = Exponential(voltage / m_Vt);
    const double rev_exp = Exponential(-(voltage + p.BreakdownVoltage) / m_Vt);
    const double rev_zero = Exponential(-p.BreakdownVoltage / m_Vt);
    const double current = p.SaturationCurrent * (fwd_exp - 1.0) - p.BreakdownCurrent * (rev_exp - rev_zero);
    const double conductance = (p.SaturationCurrent * fwd_exp + p.BreakdownCurrent * rev_exp) / m_Vt;
    return {current, conductance};
}

double DiodeDevice::Charge(const double voltage) const {
    const DiodeParameters &p = m_Parameters;
    const DepletionCharge depletion = EvaluateDepletion(voltage, p.JunctionCapacitance, p.JunctionPotential,
                                                        p.GradingCoefficient, JUNCTION_FORWARD_LIMIT);
    return depletion.Charge + p.TransitTime * Evaluate(voltage).Current;
}

double DiodeDevice::Capacitance(const double voltage) const {
    const DiodeParameters &p = m_Parameters;
    const DepletionCharge depletion = EvaluateDepletion(voltage, p.JunctionCapacitance, p.JunctionPotential,
                                                        p.GradingCoefficient, JUNCTION_FORWARD_LIMIT);
    return depletion.Capacitance + p.TransitTime * Evaluate(voltage).Conductance;
}

double DiodeDevice::VoltageAt(const std::vector<double> &x) const {
    return NodeVoltage(x, m_Anode) - NodeVoltage(x, m_JunctionNode);
}

void DiodeDevice::Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) {
    StampSeriesResistor(sys, m_Cathode, m_Internal, m_Parameters.SeriesResistance);

    const double voltage = VoltageAt(x);
    const Junction junction = Evaluate(voltage);
    StampNortonBranch(sys, m_Anode, m_JunctionNode, junction.Conductance, junction.Current, voltage);

    m_CapConductance = 0.0;
    m_CapEquivalent = 0.0;
    if (time.IsTransient && HasTransientState()) {
        const ChargeCompanion cap = ChargeModel(Capacitance(voltage), Charge(voltage), voltage, m_History, time);
        m_CapConductance = cap.Conductance;
        m_CapEquivalent = cap.EquivalentCurrent;
        StampConductance(sys, m_Anode, m_JunctionNode, cap.Conductance);
        StampCurrentSource(sys, m_Anode, m_JunctionNode, cap.EquivalentCurrent);
    }
}

bool DiodeDevice::Limit(std::vector<double> &x_new, const std::vector<double> &x_old) const {
    const double voltage_new = VoltageAt(x_new);
    const double voltage_old = VoltageAt(x_old);
    const DiodeParameters &p = m_Parameters;
    // SPICE effective breakdown knee where I_rev(-bv_eff) == IS, so PNJLIM uses m_Critical symmetrically:
    const double bv_eff = p.BreakdownVoltage -
                          m_Vt * std::log(std::max(p.BreakdownCurrent, 1e-30) / std::max(p.SaturationCurrent, 1e-30));
    double limited;
    if (voltage_new >= -0.5 * bv_eff && voltage_old >= -0.5 * bv_eff) {
        limited = PnLimit(voltage_new, voltage_old, m_Critical, m_Vt);
    } else {
        const double magnitude_new = -(voltage_new + bv_eff);
        const double magnitude_old = -(voltage_old + bv_eff);
        limited = -PnLimit(magnitude_new, magnitude_old, m_Critical, m_Vt) - bv_eff;
    }
    const double delta = limited - voltage_new;
    if (delta == 0.0) {
        return false;
    }
    if (m_JunctionNode >= 0) {
        x_new[static_cast<std::size_t>(m_JunctionNode)] -= delta;
    } else if (m_Anode >= 0) {
        x_new[static_cast<std::size_t>(m_Anode)] += delta;
    }
    return true;
}

void DiodeDevice::SeedOperatingPoint(std::vector<double> &) const {
}

double DiodeDevice::TerminalCurrent(const int terminal, const std::vector<double> &x) const {
    const double v = VoltageAt(x);
    const double current = Evaluate(v).Current + m_CapConductance * v + m_CapEquivalent;
    return terminal == 0 ? current : -current;
}

void DiodeDevice::StampAc(MnaSystem &reactive, const std::vector<double> &x_dc) const {
    if (HasTransientState()) {
        StampConductance(reactive, m_Anode, m_JunctionNode, Capacitance(VoltageAt(x_dc)));
    }
}

void DiodeDevice::StampCharge(MnaSystem &sys, const std::vector<double> &x) const {
    if (!HasTransientState()) {
        return;
    }
    const double voltage = VoltageAt(x);
    StampNortonBranch(sys, m_Anode, m_JunctionNode, Capacitance(voltage), Charge(voltage), voltage);
}

double DiodeDevice::TerminalCharge(const int terminal, const std::vector<double> &x) const {
    if (!HasTransientState()) {
        return 0.0;
    }
    const double charge = Charge(VoltageAt(x));
    return terminal == 0 ? charge : -charge;
}

std::complex<double> DiodeDevice::AcTerminalCurrent(const int terminal, const std::vector<double> &x_dc,
                                                    const std::vector<std::complex<double>> &x_ac,
                                                    const double omega) const {
    const double v_dc = VoltageAt(x_dc);
    const double gd = Evaluate(v_dc).Conductance;
    const double cj = HasTransientState() ? Capacitance(v_dc) : 0.0;
    const std::complex<double> v_ac = AcNodeVoltage(x_ac, m_Anode) - AcNodeVoltage(x_ac, m_JunctionNode);
    const std::complex<double> current = std::complex<double>(gd, omega * cj) * v_ac;
    return terminal == 0 ? current : -current;
}

double DiodeDevice::OutputNoiseVariance(const std::vector<double> &x_dc,
                                        const std::vector<std::complex<double>> &adjoint,
                                        const double /*frequency_hz*/) const {
    double variance = 0.0;
    if (m_Internal >= 0 && m_Parameters.SeriesResistance > 0.0) {
        const std::complex<double> rs_tf = AcNodeVoltage(adjoint, m_Internal) - AcNodeVoltage(adjoint, m_Cathode);
        variance += (GetFourKt(m_Thermal) / m_Parameters.SeriesResistance) * std::norm(rs_tf);
    }
    const double vd = VoltageAt(x_dc);
    // In SPICE3f5 / ngspice (`dioload.c`), `DIOcurrent` includes the parallel junction `CKTgmin * vd` term (1e-12 S),
    // which dominates the shot noise of reverse-biased rail-clamping diodes (`dionoise.c`).
    const double current = Evaluate(vd).Current + 1e-12 * vd;
    const std::complex<double> j_tf = AcNodeVoltage(adjoint, m_Anode) - AcNodeVoltage(adjoint, m_JunctionNode);
    variance += TWO_Q * std::fabs(current) * std::norm(j_tf);
    return variance;
}

bool DiodeDevice::HasTransientState() const {
    return m_Parameters.JunctionCapacitance > 0.0 || m_Parameters.TransitTime > 0.0;
}

void DiodeDevice::AcceptStep(const std::vector<double> &x) {
    if (!HasTransientState()) {
        return;
    }
    const double voltage = VoltageAt(x);
    m_History = AdvanceHistory(m_History, Charge(voltage), m_CapConductance * voltage + m_CapEquivalent);
    m_SavedHistory = m_History;
}

void DiodeDevice::RejectStep() {
    m_History = m_SavedHistory;
}

/**
 * @brief   Moves the diode to the temperature of the run.
 * @param[in] thermal  Temperature of the run and the nominal temperature the parameters are given at.
 */
void DiodeDevice::SetTemperature(const ThermalConditions &thermal) {
    ApplyTemperature(thermal);
}

} // namespace Nodalys
