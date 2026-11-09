/**
 * @file    nonlinear.cpp
 * @brief   Nonlinear semiconductor devices of the native solver: diodes, bipolar transistors (Gummel-Poon), and
 *          level-1 MOSFETs (Shichman-Hodges).
 */

#include "native/nonlinear.h"

#include "native/pn_junction.h"
#include <algorithm>
#include <cmath>

namespace Core::Native {

namespace {

/// Maximum exponent argument before clamping `exp(x)` so intermediate iterates during gmin stepping cannot overflow.
constexpr double MAX_EXPONENT = 80.0;

/// Default SPICE junction potential ($VJ = 0.75\text{ V}$), grading coefficient ($M = 0.33$), and forward-bias
/// depletion capacitance cross-over ($FC = 0.5$) used when BJT parameters omit them.
constexpr double BJT_JUNCTION_POTENTIAL = 0.75;
constexpr double BJT_GRADING_COEFFICIENT = 0.33;
constexpr double JUNCTION_FORWARD_LIMIT = 0.5;

/// Default SPICE saturation current ($I_S = 10^{-14}\text{ A}$) of the level-1 MOSFET body-drain diode.
constexpr double MOSFET_BODY_SATURATION_CURRENT = 1e-14;

double Exponential(const double argument) {
    return std::exp(std::min(argument, MAX_EXPONENT));
}

double NodeVoltage(const std::vector<double> &x, const int index) {
    return index < 0 ? 0.0 : x[static_cast<std::size_t>(index)];
}

void StampConductance(MnaSystem &sys, const int node_a, const int node_b, const double conductance) {
    sys.AddA(node_a, node_a, conductance);
    sys.AddA(node_b, node_b, conductance);
    sys.AddA(node_a, node_b, -conductance);
    sys.AddA(node_b, node_a, -conductance);
}

void StampCurrentSource(MnaSystem &sys, const int node_a, const int node_b, const double current) {
    sys.AddRhs(node_a, -current);
    sys.AddRhs(node_b, current);
}

/**
 * @brief   Stamps the Norton companion of a 2-terminal nonlinear branch $I(v)$ linearized at $v_0 = v_a - v_b$:
 *          $I(v) \approx G_{\text{eq}} \cdot v + I_{\text{eq}}$ with $I_{\text{eq}} = I(v_0) - G_{\text{eq}} \cdot v_0$.
 */
void StampNortonBranch(MnaSystem &sys, const int node_a, const int node_b, const double conductance,
                       const double current_at_v0, const double v0) {
    const double ieq = current_at_v0 - conductance * v0;
    StampConductance(sys, node_a, node_b, conductance);
    StampCurrentSource(sys, node_a, node_b, ieq);
}

/// Stamps a series terminal resistor between `external` and `internal` only when an internal node was allocated.
void StampSeriesResistor(MnaSystem &sys, const int external, const int internal, const double resistance) {
    if (internal >= 0 && internal != external && resistance > 0.0) {
        StampConductance(sys, external, internal, 1.0 / resistance);
    }
}

/**
 * @struct  DepletionCharge
 * @brief   SPICE depletion charge $Q(v)$ and capacitance $C(v) = dQ/dv$ of a PN junction.
 * @details Below $v = FC \cdot VJ$, $C(v) = C_{J0} (1 - v/VJ)^{-M}$. Above $FC \cdot VJ$, SPICE switches to a linear
 *          extrapolation of $C(v)$ (quadratic in $Q(v)$) to avoid the singularity at $v = VJ$.
 */
struct DepletionCharge {
    double Charge;
    double Capacitance;
};

DepletionCharge EvaluateDepletion(const double voltage, const double cj0, const double vj, const double m,
                                  const double fc) {
    if (cj0 <= 0.0) {
        return {0.0, 0.0};
    }
    const double charge_factor = cj0 * vj / (1.0 - m);
    if (voltage < fc * vj) {
        const double ratio = std::max(1.0 - voltage / vj, 1e-6);
        return {charge_factor * (1.0 - std::pow(ratio, 1.0 - m)), cj0 * std::pow(ratio, -m)};
    }
    const double fcp = std::pow(1.0 - fc, 1.0 - m);
    const double slope = (1.0 - m) / (vj * (1.0 - fc));
    return {charge_factor * (1.0 - fcp) * (1.0 + slope * (voltage - fc * vj)),
            cj0 * fcp * (1.0 + m * (voltage / vj - fc) / (1.0 - fc))};
}

/**
 * @struct  ChargeCompanion
 * @brief   Linearized Trapezoidal (or Backward Euler) companion for a nonlinear charge $Q(v)$:
 *          $i(t+h) = \frac{\text{factor}}{h} (Q(v) - Q(t)) - i_{\text{past}} \approx G_{\text{cap}} v + I_{\text{eq,cap}}$.
 */
struct ChargeCompanion {
    double Conductance;
    double EquivalentCurrent;
};

ChargeCompanion ChargeModel(const double dQdv, const double charge_at_iterate, const double voltage_iterate,
                            const double charge_previous, const double current_previous, const double step,
                            const bool backward_euler) {
    const double factor = backward_euler ? 1.0 : 2.0;
    const double conductance = factor * dQdv / step;
    const double current_at_iterate =
        factor * (charge_at_iterate - charge_previous) / step - (backward_euler ? 0.0 : current_previous);
    const double equivalent = current_at_iterate - conductance * voltage_iterate;
    return {conductance, equivalent};
}

} // namespace

// ============================================================================
// DiodeDevice
// ============================================================================

DiodeDevice::DiodeDevice(const int anode, const int cathode, const int internal, DiodeParameters parameters)
    : m_Anode(anode), m_Cathode(cathode), m_Internal(internal),
      m_JunctionNode(internal >= 0 ? internal : cathode), m_Parameters(parameters),
      m_Vt(parameters.EmissionCoefficient * THERMAL_VOLTAGE),
      m_Critical(PnCriticalVoltage(std::max(parameters.SaturationCurrent, 1e-30), m_Vt)) {
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
        const ChargeCompanion cap = ChargeModel(Capacitance(voltage), Charge(voltage), voltage, m_Charge,
                                                m_ChargeCurrent, time.TimeStep, time.BackwardEuler);
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
    const double bv_eff =
        p.BreakdownVoltage - m_Vt * std::log(std::max(p.BreakdownCurrent, 1e-30) / std::max(p.SaturationCurrent, 1e-30));
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

void DiodeDevice::SeedOperatingPoint(std::vector<double> &) const {}

double DiodeDevice::TerminalCurrent(const int terminal, const std::vector<double> &x) const {
    const double v = VoltageAt(x);
    const double current = Evaluate(v).Current + m_CapConductance * v + m_CapEquivalent;
    return terminal == 0 ? current : -current;
}

bool DiodeDevice::HasTransientState() const {
    return m_Parameters.JunctionCapacitance > 0.0 || m_Parameters.TransitTime > 0.0;
}

void DiodeDevice::AcceptStep(const std::vector<double> &x) {
    if (!HasTransientState()) {
        return;
    }
    const double voltage = VoltageAt(x);
    m_Charge = Charge(voltage);
    m_ChargeCurrent = m_CapConductance * voltage + m_CapEquivalent;
    m_SavedCharge = m_Charge;
    m_SavedChargeCurrent = m_ChargeCurrent;
}

void DiodeDevice::RejectStep() {
    m_Charge = m_SavedCharge;
    m_ChargeCurrent = m_SavedChargeCurrent;
}

// ============================================================================
// BjtDevice (SPICE Gummel-Poon Model)
// ============================================================================

BjtDevice::BjtDevice(const int collector, const int base, const int emitter, const int internal_collector,
                     const int internal_base, const int internal_emitter, BJTParameters parameters, const bool pnp)
    : m_Collector(collector), m_Base(base), m_Emitter(emitter),
      m_C(internal_collector >= 0 ? internal_collector : collector),
      m_B(internal_base >= 0 ? internal_base : base),
      m_E(internal_emitter >= 0 ? internal_emitter : emitter),
      m_Parameters(parameters), m_Pnp(pnp), m_Vt(THERMAL_VOLTAGE),
      m_Critical(PnCriticalVoltage(std::max(parameters.SaturationCurrent, 1e-30), m_Vt)) {
}

BjtDevice::Intrinsic BjtDevice::Evaluate(const double vbe, const double vbc) const {
    const BJTParameters &p = m_Parameters;
    Intrinsic result{};

    // 1. Forward (BE) and reverse (BC) ideal diode currents:
    //    ifwd = IS * (exp(vbe / Vt) - 1),   irev = IS * (exp(vbc / Vt) - 1)
    const double exp_be = Exponential(vbe / m_Vt);
    const double ifwd = p.SaturationCurrent * (exp_be - 1.0);
    const double difwd = p.SaturationCurrent * exp_be / m_Vt;

    const double exp_bc = Exponential(vbc / m_Vt);
    const double irev = p.SaturationCurrent * (exp_bc - 1.0);
    const double direv = p.SaturationCurrent * exp_bc / m_Vt;

    // 2. Normalized base charge factor qb (Early effect VAF + high-level injection roll-off IKF):
    //    Following SPICE (bjtload.c):
    //      q1 = 1 / (1 - vbc / VAF)
    //      q2 = ifwd / IKF
    //      qb = (q1 / 2) * (1 + sqrt(1 + 4 * q2))
    const double q1 = p.EarlyVoltage != 0.0 ? 1.0 / (1.0 - vbc / p.EarlyVoltage) : 1.0;
    const double dq1_dvbc = p.EarlyVoltage != 0.0 ? (q1 * q1) / p.EarlyVoltage : 0.0;
    double qb = q1;
    double dqb_dvbe = 0.0;
    double dqb_dvbc = dq1_dvbc;
    if (p.ForwardKneeCurrent != 0.0) {
        const double q2 = ifwd / p.ForwardKneeCurrent;
        const double arg = std::max(1e-30, 1.0 + 4.0 * q2);
        const double sqarg = std::sqrt(arg);
        qb = q1 * (1.0 + sqarg) / 2.0;
        dqb_dvbe = q1 * (difwd / (p.ForwardKneeCurrent * sqarg));
        dqb_dvbc = dq1_dvbc * (1.0 + sqarg) / 2.0;
    }

    // 3. Transport current from collector to emitter: icc = (ifwd - irev) / qb
    const double icc = (ifwd - irev) / qb;
    const double dicc_dvbe = (difwd * qb - (ifwd - irev) * dqb_dvbe) / (qb * qb);
    const double dicc_dvbc = (-direv * qb - (ifwd - irev) * dqb_dvbc) / (qb * qb);

    // 4. Base currents: ideal forward/reverse + non-ideal BE recombination leakage (ISE, NE)
    double ibe = ifwd / p.ForwardBeta;
    double dibe = difwd / p.ForwardBeta;
    if (p.LeakageSaturationCurrent > 0.0) {
        const double vt_leak = p.LeakageEmissionCoefficient * m_Vt;
        const double exp_leak = Exponential(vbe / vt_leak);
        ibe += p.LeakageSaturationCurrent * (exp_leak - 1.0);
        dibe += p.LeakageSaturationCurrent * exp_leak / vt_leak;
    }
    const double ibc = irev / p.ReverseBeta;
    const double dibc = direv / p.ReverseBeta;

    // 5. Terminal DC currents and conductances
    result.Ic = icc - ibc;
    result.Ib = ibe + ibc;
    result.Ie = -(result.Ic + result.Ib);
    result.GcBe = dicc_dvbe;
    result.GcBc = dicc_dvbc - dibc;
    result.GbBe = dibe;
    result.GbBc = dibc;
    return result;
}

BjtDevice::Intrinsic BjtDevice::EvaluateAt(const std::vector<double> &x) const {
    const double vbe = NodeVoltage(x, m_B) - NodeVoltage(x, m_E);
    const double vbc = NodeVoltage(x, m_B) - NodeVoltage(x, m_C);
    Intrinsic result = Evaluate(m_Pnp ? -vbe : vbe, m_Pnp ? -vbc : vbc);
    if (m_Pnp) {
        result.Ic = -result.Ic;
        result.Ib = -result.Ib;
        result.Ie = -result.Ie;
    }
    return result;
}

void BjtDevice::Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) {
    const BJTParameters &p = m_Parameters;
    StampSeriesResistor(sys, m_Collector, m_C, p.CollectorResistance);
    StampSeriesResistor(sys, m_Base, m_B, p.BaseResistance);
    StampSeriesResistor(sys, m_Emitter, m_E, p.EmitterResistance);

    const Intrinsic dc = EvaluateAt(x);
    const double vbe = NodeVoltage(x, m_B) - NodeVoltage(x, m_E);
    const double vbc = NodeVoltage(x, m_B) - NodeVoltage(x, m_C);

    // Linearized DC currents around (vbe, vbc):
    //   Ic(v) ≈ GcBe * (vB - vE) + GcBc * (vB - vC) + ieq_c
    //   Ib(v) ≈ GbBe * (vB - vE) + GbBc * (vB - vC) + ieq_b
    //   Ie(v) = -(Ic(v) + Ib(v))
    const double ieq_c = dc.Ic - dc.GcBe * vbe - dc.GcBc * vbc;
    const double ieq_b = dc.Ib - dc.GbBe * vbe - dc.GbBc * vbc;

    // Collector KCL row (m_C)
    sys.AddA(m_C, m_B, dc.GcBe + dc.GcBc);
    sys.AddA(m_C, m_E, -dc.GcBe);
    sys.AddA(m_C, m_C, -dc.GcBc);
    sys.AddRhs(m_C, -ieq_c);

    // Base KCL row (m_B)
    sys.AddA(m_B, m_B, dc.GbBe + dc.GbBc);
    sys.AddA(m_B, m_E, -dc.GbBe);
    sys.AddA(m_B, m_C, -dc.GbBc);
    sys.AddRhs(m_B, -ieq_b);

    // Emitter KCL row (m_E): negative sum of Collector and Base rows
    sys.AddA(m_E, m_B, -(dc.GcBe + dc.GcBc + dc.GbBe + dc.GbBc));
    sys.AddA(m_E, m_E, dc.GcBe + dc.GbBe);
    sys.AddA(m_E, m_C, dc.GcBc + dc.GbBc);
    sys.AddRhs(m_E, ieq_c + ieq_b);

    // Transient junction charges (BE depletion + forward transit time TF; BC depletion)
    m_Gqbe = m_Gqbc = m_Ieqbe = m_Ieqbc = 0.0;
    if (time.IsTransient && HasTransientState()) {
        const double vbe_signed = m_Pnp ? -vbe : vbe;
        const double vbc_signed = m_Pnp ? -vbc : vbc;
        const double exp_be = Exponential(vbe_signed / m_Vt);
        const double ifwd = p.SaturationCurrent * (exp_be - 1.0);
        const double difwd = p.SaturationCurrent * exp_be / m_Vt;
        const DepletionCharge qbe = EvaluateDepletion(vbe_signed, p.EmitterCapacitance, BJT_JUNCTION_POTENTIAL,
                                                      BJT_GRADING_COEFFICIENT, JUNCTION_FORWARD_LIMIT);
        const DepletionCharge qbc = EvaluateDepletion(vbc_signed, p.CollectorCapacitance, BJT_JUNCTION_POTENTIAL,
                                                      BJT_GRADING_COEFFICIENT, JUNCTION_FORWARD_LIMIT);
        const ChargeCompanion cap_be =
            ChargeModel(qbe.Capacitance + p.TransitTime * difwd, qbe.Charge + p.TransitTime * ifwd, vbe_signed,
                        m_Qbe, m_IQbe, time.TimeStep, time.BackwardEuler);
        const ChargeCompanion cap_bc =
            ChargeModel(qbc.Capacitance, qbc.Charge, vbc_signed, m_Qbc, m_IQbc, time.TimeStep, time.BackwardEuler);
        m_Gqbe = cap_be.Conductance;
        m_Ieqbe = cap_be.EquivalentCurrent;
        m_Gqbc = cap_bc.Conductance;
        m_Ieqbc = cap_bc.EquivalentCurrent;

        const double sign = m_Pnp ? -1.0 : 1.0;
        StampConductance(sys, m_B, m_E, m_Gqbe);
        StampCurrentSource(sys, m_B, m_E, sign * m_Ieqbe);
        StampConductance(sys, m_B, m_C, m_Gqbc);
        StampCurrentSource(sys, m_B, m_C, sign * m_Ieqbc);
    }
}

double BjtDevice::TerminalCurrent(const int terminal, const std::vector<double> &x) const {
    const Intrinsic dc = EvaluateAt(x);
    const double vbe = NodeVoltage(x, m_B) - NodeVoltage(x, m_E);
    const double vbc = NodeVoltage(x, m_B) - NodeVoltage(x, m_C);
    const double sign = m_Pnp ? -1.0 : 1.0;
    // Capacitive currents flowing from Base to Emitter (iqbe) and from Base to Collector (iqbc)
    const double iqbe = m_Gqbe * vbe + sign * m_Ieqbe;
    const double iqbc = m_Gqbc * vbc + sign * m_Ieqbc;
    switch (terminal) {
    case 0: // Collector
        return dc.Ic - iqbc;
    case 1: // Base
        return dc.Ib + iqbe + iqbc;
    default: // Emitter
        return dc.Ie - iqbe;
    }
}

bool BjtDevice::Limit(std::vector<double> &x_new, const std::vector<double> &x_old) const {
    bool changed = false;
    for (const auto &[node_pos, node_neg] : {std::pair{m_B, m_E}, std::pair{m_B, m_C}}) {
        double voltage_new = NodeVoltage(x_new, node_pos) - NodeVoltage(x_new, node_neg);
        double voltage_old = NodeVoltage(x_old, node_pos) - NodeVoltage(x_old, node_neg);
        if (m_Pnp) {
            voltage_new = -voltage_new;
            voltage_old = -voltage_old;
        }
        const double limited = PnLimit(voltage_new, voltage_old, m_Critical, m_Vt);
        const double delta = limited - voltage_new;
        if (delta != 0.0) {
            const double signed_delta = m_Pnp ? -delta : delta;
            if (node_neg >= 0) {
                x_new[static_cast<std::size_t>(node_neg)] -= signed_delta;
                changed = true;
            } else if (node_pos >= 0) {
                x_new[static_cast<std::size_t>(node_pos)] += signed_delta;
                changed = true;
            }
        }
    }
    return changed;
}

void BjtDevice::SeedOperatingPoint(std::vector<double> &x) const {
    const double vb = NodeVoltage(x, m_B);
    const double ve = NodeVoltage(x, m_E);
    if (vb == 0.0 && ve == 0.0) {
        constexpr double SEED_VBE = 0.6;
        if (m_E >= 0) {
            x[static_cast<std::size_t>(m_E)] = m_Pnp ? SEED_VBE : -SEED_VBE;
        } else if (m_B >= 0) {
            x[static_cast<std::size_t>(m_B)] = m_Pnp ? -SEED_VBE : SEED_VBE;
        }
    }
}

bool BjtDevice::HasTransientState() const {
    return m_Parameters.TransitTime > 0.0 || m_Parameters.EmitterCapacitance > 0.0 ||
           m_Parameters.CollectorCapacitance > 0.0;
}

void BjtDevice::AcceptStep(const std::vector<double> &x) {
    if (!HasTransientState()) {
        return;
    }
    const double vbe = NodeVoltage(x, m_B) - NodeVoltage(x, m_E);
    const double vbc = NodeVoltage(x, m_B) - NodeVoltage(x, m_C);
    const double vbe_signed = m_Pnp ? -vbe : vbe;
    const double vbc_signed = m_Pnp ? -vbc : vbc;
    const BJTParameters &p = m_Parameters;
    const double ifwd = p.SaturationCurrent * (Exponential(vbe_signed / m_Vt) - 1.0);
    m_Qbe = EvaluateDepletion(vbe_signed, p.EmitterCapacitance, BJT_JUNCTION_POTENTIAL, BJT_GRADING_COEFFICIENT,
                              JUNCTION_FORWARD_LIMIT)
                .Charge +
            p.TransitTime * ifwd;
    m_Qbc = EvaluateDepletion(vbc_signed, p.CollectorCapacitance, BJT_JUNCTION_POTENTIAL, BJT_GRADING_COEFFICIENT,
                              JUNCTION_FORWARD_LIMIT)
                .Charge;
    m_IQbe = m_Gqbe * vbe_signed + m_Ieqbe;
    m_IQbc = m_Gqbc * vbc_signed + m_Ieqbc;
    m_SavedQbe = m_Qbe;
    m_SavedIQbe = m_IQbe;
    m_SavedQbc = m_Qbc;
    m_SavedIQbc = m_IQbc;
}

void BjtDevice::RejectStep() {
    m_Qbe = m_SavedQbe;
    m_IQbe = m_SavedIQbe;
    m_Qbc = m_SavedQbc;
    m_IQbc = m_SavedIQbc;
}

// ============================================================================
// MosfetDevice (SPICE Level-1 Shichman-Hodges Model)
// ============================================================================

MosfetDevice::MosfetDevice(const int drain, const int gate, const int source, const int internal_drain,
                           const int internal_source, MOSFETParameters parameters, const bool pmos)
    : m_Drain(drain), m_Gate(gate), m_Source(source),
      m_D(internal_drain >= 0 ? internal_drain : drain),
      m_S(internal_source >= 0 ? internal_source : source),
      m_Parameters(parameters), m_Pmos(pmos),
      m_Beta(parameters.Transconductance * parameters.Width / parameters.Length), m_Vt(THERMAL_VOLTAGE),
      m_Cgs(parameters.GateSourceOverlap * parameters.Width), m_Cgd(parameters.GateDrainOverlap * parameters.Width),
      m_Critical(PnCriticalVoltage(MOSFET_BODY_SATURATION_CURRENT, THERMAL_VOLTAGE)) {
}

MosfetDevice::Channel MosfetDevice::Evaluate(const double vgs_eff, const double vds_eff) const {
    const double threshold = m_Pmos ? -m_Parameters.ThresholdVoltage : m_Parameters.ThresholdVoltage;
    const double lambda = m_Parameters.ChannelModulation;
    Channel result{};
    const double vov = vgs_eff - threshold;
    if (vov <= 0.0 || vds_eff <= 0.0) {
        return result; // Cutoff region
    }
    if (vds_eff < vov) {
        // Linear (triode) region: I = Beta * (vov * vds - vds^2 / 2) * (1 + lambda * vds)
        result.Current = m_Beta * (vov * vds_eff - vds_eff * vds_eff / 2.0) * (1.0 + lambda * vds_eff);
        result.Gm = m_Beta * vds_eff * (1.0 + lambda * vds_eff);
        result.Gds = m_Beta * (vov - vds_eff) * (1.0 + lambda * vds_eff) +
                     m_Beta * (vov * vds_eff - vds_eff * vds_eff / 2.0) * lambda;
    } else {
        // Saturation region: I = (Beta / 2) * vov^2 * (1 + lambda * vds)
        result.Current = (m_Beta / 2.0) * vov * vov * (1.0 + lambda * vds_eff);
        result.Gm = m_Beta * vov * (1.0 + lambda * vds_eff);
        result.Gds = (m_Beta / 2.0) * vov * vov * lambda;
    }
    return result;
}

double MosfetDevice::Vgs(const std::vector<double> &x) const {
    const double vg = NodeVoltage(x, m_Gate);
    const double vs = NodeVoltage(x, m_S);
    return m_Pmos ? vs - vg : vg - vs;
}

double MosfetDevice::Vds(const std::vector<double> &x) const {
    const double vd = NodeVoltage(x, m_D);
    const double vs = NodeVoltage(x, m_S);
    return m_Pmos ? vs - vd : vd - vs;
}

void MosfetDevice::Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) {
    StampSeriesResistor(sys, m_Drain, m_D, m_Parameters.DrainResistance);
    StampSeriesResistor(sys, m_Source, m_S, m_Parameters.SourceResistance);

    const double vg = NodeVoltage(x, m_Gate);
    const double vd = NodeVoltage(x, m_D);
    const double vs = NodeVoltage(x, m_S);

    // 1. Channel conduction (with symmetric source/drain role swap when Vds_eff < 0):
    //    `pol` is +1 for NMOS and -1 for PMOS.
    const double pol = m_Pmos ? -1.0 : 1.0;
    const bool reversed = (pol * (vd - vs)) < 0.0;
    const int d_eff = reversed ? m_S : m_D;
    const int s_eff = reversed ? m_D : m_S;
    const double vd_eff = NodeVoltage(x, d_eff);
    const double vs_eff = NodeVoltage(x, s_eff);

    const double vgs_eff = pol * (vg - vs_eff);
    const double vds_eff = pol * (vd_eff - vs_eff);
    const Channel channel = Evaluate(vgs_eff, vds_eff);

    // Small-signal companion between d_eff and s_eff:
    //   I(d_eff -> s_eff) ≈ Gm * (V(Gate) - V(s_eff)) + Gds * (V(d_eff) - V(s_eff)) + ieq
    const double i_channel = pol * channel.Current;
    const double ieq = i_channel - channel.Gm * (vg - vs_eff) - channel.Gds * (vd_eff - vs_eff);

    StampConductance(sys, d_eff, s_eff, channel.Gds);
    sys.AddA(d_eff, m_Gate, channel.Gm);
    sys.AddA(d_eff, s_eff, -channel.Gm);
    sys.AddA(s_eff, m_Gate, -channel.Gm);
    sys.AddA(s_eff, s_eff, channel.Gm);
    StampCurrentSource(sys, d_eff, s_eff, ieq);

    // 2. Body-drain PN junction diode (body is tied to the external source terminal):
    //    For NMOS, anode = Source, cathode = Drain. For PMOS, anode = Drain, cathode = Source.
    const int body_anode = m_Pmos ? m_D : m_Source;
    const int body_cathode = m_Pmos ? m_Source : m_D;
    const double vbody = NodeVoltage(x, body_anode) - NodeVoltage(x, body_cathode);
    const double diode_exp = Exponential(vbody / m_Vt);
    const double diode_current = MOSFET_BODY_SATURATION_CURRENT * (diode_exp - 1.0);
    const double diode_cond = MOSFET_BODY_SATURATION_CURRENT * diode_exp / m_Vt;
    StampNortonBranch(sys, body_anode, body_cathode, diode_cond, diode_current, vbody);

    // 3. Linear gate-overlap capacitances (CGSO*W between Gate-Source, CGDO*W between Gate-Drain)
    m_Gcgs = m_Gcgd = m_Ieqgs = m_Ieqgd = 0.0;
    if (time.IsTransient && HasTransientState()) {
        const double vgs = NodeVoltage(x, m_Gate) - NodeVoltage(x, m_Source);
        const double vgd = NodeVoltage(x, m_Gate) - NodeVoltage(x, m_Drain);
        const ChargeCompanion cap_gs =
            ChargeModel(m_Cgs, m_Cgs * vgs, vgs, m_Cgs * m_Vgs, m_Igs, time.TimeStep, time.BackwardEuler);
        const ChargeCompanion cap_gd =
            ChargeModel(m_Cgd, m_Cgd * vgd, vgd, m_Cgd * m_Vgd, m_Igd, time.TimeStep, time.BackwardEuler);
        m_Gcgs = cap_gs.Conductance;
        m_Ieqgs = cap_gs.EquivalentCurrent;
        m_Gcgd = cap_gd.Conductance;
        m_Ieqgd = cap_gd.EquivalentCurrent;
        StampConductance(sys, m_Gate, m_Source, m_Gcgs);
        StampCurrentSource(sys, m_Gate, m_Source, m_Ieqgs);
        StampConductance(sys, m_Gate, m_Drain, m_Gcgd);
        StampCurrentSource(sys, m_Gate, m_Drain, m_Ieqgd);
    }
}

double MosfetDevice::TerminalCurrent(const int terminal, const std::vector<double> &x) const {
    const double vg = NodeVoltage(x, m_Gate);
    const double vd = NodeVoltage(x, m_D);
    const double vs = NodeVoltage(x, m_S);

    const double pol = m_Pmos ? -1.0 : 1.0;
    const bool reversed = (pol * (vd - vs)) < 0.0;
    const int d_eff = reversed ? m_S : m_D;
    const int s_eff = reversed ? m_D : m_S;
    const double vgs_eff = pol * (vg - NodeVoltage(x, s_eff));
    const double vds_eff = pol * (NodeVoltage(x, d_eff) - NodeVoltage(x, s_eff));
    const Channel channel = Evaluate(vgs_eff, vds_eff);

    // Channel current entering the physical drain terminal m_D
    const double i_d_eff_to_s_eff = pol * channel.Current;
    const double channel_into_drain = reversed ? -i_d_eff_to_s_eff : i_d_eff_to_s_eff;

    // Body diode current entering the physical drain terminal m_D
    const int body_anode = m_Pmos ? m_D : m_Source;
    const int body_cathode = m_Pmos ? m_Source : m_D;
    const double vbody = NodeVoltage(x, body_anode) - NodeVoltage(x, body_cathode);
    const double diode_current = MOSFET_BODY_SATURATION_CURRENT * (Exponential(vbody / m_Vt) - 1.0);
    const double diode_into_drain = m_Pmos ? diode_current : -diode_current;

    // Gate overlap capacitive currents (flowing from Gate to Source and from Gate to Drain)
    const double vgs = NodeVoltage(x, m_Gate) - NodeVoltage(x, m_Source);
    const double vgd = NodeVoltage(x, m_Gate) - NodeVoltage(x, m_Drain);
    const double igs = m_Gcgs * vgs + m_Ieqgs;
    const double igd = m_Gcgd * vgd + m_Ieqgd;

    switch (terminal) {
    case 0: // Drain
        return channel_into_drain + diode_into_drain - igd;
    case 1: // Gate
        return igs + igd;
    default: // Source
        return -(channel_into_drain + diode_into_drain) - igs;
    }
}

bool MosfetDevice::Limit(std::vector<double> &x_new, const std::vector<double> &x_old) const {
    const int body_anode = m_Pmos ? m_D : m_Source;
    const int body_cathode = m_Pmos ? m_Source : m_D;
    const double voltage_new = NodeVoltage(x_new, body_anode) - NodeVoltage(x_new, body_cathode);
    const double voltage_old = NodeVoltage(x_old, body_anode) - NodeVoltage(x_old, body_cathode);
    const double limited = PnLimit(voltage_new, voltage_old, m_Critical, m_Vt);
    const double delta = limited - voltage_new;
    if (delta == 0.0 || m_D < 0) {
        return false;
    }
    x_new[static_cast<std::size_t>(m_D)] += m_Pmos ? delta : -delta;
    return true;
}

bool MosfetDevice::HasTransientState() const {
    return m_Cgs > 0.0 || m_Cgd > 0.0;
}

void MosfetDevice::AcceptStep(const std::vector<double> &x) {
    if (!HasTransientState()) {
        return;
    }
    const double vgs = NodeVoltage(x, m_Gate) - NodeVoltage(x, m_Source);
    const double vgd = NodeVoltage(x, m_Gate) - NodeVoltage(x, m_Drain);
    m_Vgs = vgs;
    m_Vgd = vgd;
    m_Igs = m_Gcgs * vgs + m_Ieqgs;
    m_Igd = m_Gcgd * vgd + m_Ieqgd;
    m_SavedVgs = m_Vgs;
    m_SavedIgs = m_Igs;
    m_SavedVgd = m_Vgd;
    m_SavedIgd = m_Igd;
}

void MosfetDevice::RejectStep() {
    m_Vgs = m_SavedVgs;
    m_Igs = m_SavedIgs;
    m_Vgd = m_SavedVgd;
    m_Igd = m_SavedIgd;
}

void MosfetDevice::SeedOperatingPoint(std::vector<double> &) const {}

} // namespace Core::Native
