/**
 * @file    bjt_device.cpp
 * @brief   Bipolar transistor of Nodalys, the SPICE Gummel-Poon model.
 * @details The helpers the nonlinear devices share are in semiconductor_math.h.
 */

#include "devices/bjt_device.h"

#include "devices/pn_junction.h"
#include "devices/semiconductor_math.h"
#include <algorithm>
#include <cmath>

namespace Nodalys {

namespace {

/// Default SPICE junction potential ($VJ = 0.75\text{ V}$) and grading coefficient ($M = 0.33$) used when BJT
/// parameters omit them.
constexpr double BJT_JUNCTION_POTENTIAL = 0.75;
constexpr double BJT_GRADING_COEFFICIENT = 0.33;

} // namespace

BjtDevice::BjtDevice(const int collector, const int base, const int emitter, const int internal_collector,
                     const int internal_base, const int internal_emitter, BjtParameters parameters, const bool pnp)
    : m_Collector(collector), m_Base(base), m_Emitter(emitter),
      m_C(internal_collector >= 0 ? internal_collector : collector), m_B(internal_base >= 0 ? internal_base : base),
      m_E(internal_emitter >= 0 ? internal_emitter : emitter), m_Parameters(parameters), m_Pnp(pnp),
      m_Vt(THERMAL_VOLTAGE), m_Critical(PnCriticalVoltage(std::max(parameters.SaturationCurrent, 1e-30), m_Vt)) {
}

BjtDevice::Intrinsic BjtDevice::Evaluate(const double vbe, const double vbc) const {
    const BjtParameters &p = m_Parameters;
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
    result.IfwdQb = ifwd / qb;
    result.DIfwdQbDvbe = (difwd * qb - ifwd * dqb_dvbe) / (qb * qb);
    result.DIfwdQbDvbc = (-ifwd * dqb_dvbc) / (qb * qb);
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
    const BjtParameters &p = m_Parameters;
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
        const DepletionCharge qbe = EvaluateDepletion(vbe_signed, p.EmitterCapacitance, BJT_JUNCTION_POTENTIAL,
                                                      BJT_GRADING_COEFFICIENT, JUNCTION_FORWARD_LIMIT);
        const DepletionCharge qbc = EvaluateDepletion(vbc_signed, p.CollectorCapacitance, BJT_JUNCTION_POTENTIAL,
                                                      BJT_GRADING_COEFFICIENT, JUNCTION_FORWARD_LIMIT);
        const ChargeCompanion cap_be =
            ChargeModel(qbe.Capacitance + p.TransitTime * dc.DIfwdQbDvbe, qbe.Charge + p.TransitTime * dc.IfwdQb,
                        vbe_signed, m_HistoryBe, time);
        const ChargeCompanion cap_bc = ChargeModel(qbc.Capacitance, qbc.Charge, vbc_signed, m_HistoryBc, time);
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

void BjtDevice::StampAc(MnaSystem &reactive, const std::vector<double> &x_dc) const {
    if (!HasTransientState()) {
        return;
    }
    const BjtParameters &p = m_Parameters;
    const Intrinsic dc = EvaluateAt(x_dc);
    const double vbe = NodeVoltage(x_dc, m_B) - NodeVoltage(x_dc, m_E);
    const double vbc = NodeVoltage(x_dc, m_B) - NodeVoltage(x_dc, m_C);
    const double vbe_signed = m_Pnp ? -vbe : vbe;
    const double vbc_signed = m_Pnp ? -vbc : vbc;
    const DepletionCharge qbe = EvaluateDepletion(vbe_signed, p.EmitterCapacitance, BJT_JUNCTION_POTENTIAL,
                                                  BJT_GRADING_COEFFICIENT, JUNCTION_FORWARD_LIMIT);
    const DepletionCharge qbc = EvaluateDepletion(vbc_signed, p.CollectorCapacitance, BJT_JUNCTION_POTENTIAL,
                                                  BJT_GRADING_COEFFICIENT, JUNCTION_FORWARD_LIMIT);
    StampConductance(reactive, m_B, m_E, qbe.Capacitance + p.TransitTime * dc.DIfwdQbDvbe);
    StampConductance(reactive, m_B, m_C, qbc.Capacitance);
    // SPICE geqcb = d(TF * Ifwd / qb) / dvbc: transcapacitance from vbc into the BE charge (Base -> Emitter)
    const double geqcb = p.TransitTime * dc.DIfwdQbDvbc;
    reactive.AddA(m_B, m_B, geqcb);
    reactive.AddA(m_B, m_C, -geqcb);
    reactive.AddA(m_E, m_B, -geqcb);
    reactive.AddA(m_E, m_C, geqcb);
}

void BjtDevice::StampCharge(MnaSystem &sys, const std::vector<double> &x) const {
    if (!HasTransientState()) {
        return;
    }
    const BjtParameters &p = m_Parameters;
    const Intrinsic dc = EvaluateAt(x);
    const double vbe = NodeVoltage(x, m_B) - NodeVoltage(x, m_E);
    const double vbc = NodeVoltage(x, m_B) - NodeVoltage(x, m_C);
    const double vbe_signed = m_Pnp ? -vbe : vbe;
    const double vbc_signed = m_Pnp ? -vbc : vbc;
    const DepletionCharge qbe = EvaluateDepletion(vbe_signed, p.EmitterCapacitance, BJT_JUNCTION_POTENTIAL,
                                                  BJT_GRADING_COEFFICIENT, JUNCTION_FORWARD_LIMIT);
    const DepletionCharge qbc = EvaluateDepletion(vbc_signed, p.CollectorCapacitance, BJT_JUNCTION_POTENTIAL,
                                                  BJT_GRADING_COEFFICIENT, JUNCTION_FORWARD_LIMIT);

    const double c_be = qbe.Capacitance + p.TransitTime * dc.DIfwdQbDvbe;
    const double c_bc = qbc.Capacitance;
    const double geqcb = p.TransitTime * dc.DIfwdQbDvbc;
    const double sign = m_Pnp ? -1.0 : 1.0;
    const double q_be_phys = sign * (qbe.Charge + p.TransitTime * dc.IfwdQb);
    const double q_bc_phys = sign * qbc.Charge;

    StampConductance(sys, m_B, m_E, c_be);
    sys.AddA(m_B, m_B, geqcb);
    sys.AddA(m_B, m_C, -geqcb);
    sys.AddA(m_E, m_B, -geqcb);
    sys.AddA(m_E, m_C, geqcb);
    StampCurrentSource(sys, m_B, m_E, q_be_phys - c_be * vbe - geqcb * vbc);

    StampNortonBranch(sys, m_B, m_C, c_bc, q_bc_phys, vbc);
}

double BjtDevice::TerminalCharge(const int terminal, const std::vector<double> &x) const {
    if (!HasTransientState()) {
        return 0.0;
    }
    const BjtParameters &p = m_Parameters;
    const Intrinsic dc = EvaluateAt(x);
    const double vbe = NodeVoltage(x, m_B) - NodeVoltage(x, m_E);
    const double vbc = NodeVoltage(x, m_B) - NodeVoltage(x, m_C);
    const double vbe_signed = m_Pnp ? -vbe : vbe;
    const double vbc_signed = m_Pnp ? -vbc : vbc;
    const DepletionCharge qbe = EvaluateDepletion(vbe_signed, p.EmitterCapacitance, BJT_JUNCTION_POTENTIAL,
                                                  BJT_GRADING_COEFFICIENT, JUNCTION_FORWARD_LIMIT);
    const DepletionCharge qbc = EvaluateDepletion(vbc_signed, p.CollectorCapacitance, BJT_JUNCTION_POTENTIAL,
                                                  BJT_GRADING_COEFFICIENT, JUNCTION_FORWARD_LIMIT);
    const double sign = m_Pnp ? -1.0 : 1.0;
    const double q_be = sign * (qbe.Charge + p.TransitTime * dc.IfwdQb);
    const double q_bc = sign * qbc.Charge;
    switch (terminal) {
    case 0: // Collector
        return -q_bc;
    case 1: // Base
        return q_be + q_bc;
    default: // Emitter
        return -q_be;
    }
}

std::complex<double> BjtDevice::AcTerminalCurrent(const int terminal, const std::vector<double> &x_dc,
                                                  const std::vector<std::complex<double>> &x_ac,
                                                  const double omega) const {
    const Intrinsic dc = EvaluateAt(x_dc);
    const std::complex<double> vbe_ac = AcNodeVoltage(x_ac, m_B) - AcNodeVoltage(x_ac, m_E);
    const std::complex<double> vbc_ac = AcNodeVoltage(x_ac, m_B) - AcNodeVoltage(x_ac, m_C);
    const std::complex<double> ic_ac = dc.GcBe * vbe_ac + dc.GcBc * vbc_ac;
    const std::complex<double> ib_ac = dc.GbBe * vbe_ac + dc.GbBc * vbc_ac;

    std::complex<double> iqbe_ac{0.0, 0.0};
    std::complex<double> iqbc_ac{0.0, 0.0};
    if (HasTransientState()) {
        const BjtParameters &p = m_Parameters;
        const double vbe = NodeVoltage(x_dc, m_B) - NodeVoltage(x_dc, m_E);
        const double vbc = NodeVoltage(x_dc, m_B) - NodeVoltage(x_dc, m_C);
        const double vbe_signed = m_Pnp ? -vbe : vbe;
        const double vbc_signed = m_Pnp ? -vbc : vbc;
        const DepletionCharge qbe = EvaluateDepletion(vbe_signed, p.EmitterCapacitance, BJT_JUNCTION_POTENTIAL,
                                                      BJT_GRADING_COEFFICIENT, JUNCTION_FORWARD_LIMIT);
        const DepletionCharge qbc = EvaluateDepletion(vbc_signed, p.CollectorCapacitance, BJT_JUNCTION_POTENTIAL,
                                                      BJT_GRADING_COEFFICIENT, JUNCTION_FORWARD_LIMIT);
        iqbe_ac = std::complex<double>(0.0, omega) * ((qbe.Capacitance + p.TransitTime * dc.DIfwdQbDvbe) * vbe_ac +
                                                      (p.TransitTime * dc.DIfwdQbDvbc) * vbc_ac);
        iqbc_ac = std::complex<double>(0.0, omega * qbc.Capacitance) * vbc_ac;
    }

    switch (terminal) {
    case 0: // Collector
        return ic_ac - iqbc_ac;
    case 1: // Base
        return ib_ac + iqbe_ac + iqbc_ac;
    default: // Emitter
        return -(ic_ac + ib_ac) - iqbe_ac;
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

double BjtDevice::OutputNoiseVariance(const std::vector<double> &x_dc, const std::vector<std::complex<double>> &adjoint,
                                      const double /*frequency_hz*/) const {
    const BjtParameters &p = m_Parameters;
    double variance = 0.0;
    if (m_C != m_Collector && p.CollectorResistance > 0.0) {
        const std::complex<double> rc_tf = AcNodeVoltage(adjoint, m_Collector) - AcNodeVoltage(adjoint, m_C);
        variance += (FOUR_KT_NOMINAL / p.CollectorResistance) * std::norm(rc_tf);
    }
    if (m_B != m_Base && p.BaseResistance > 0.0) {
        const std::complex<double> rb_tf = AcNodeVoltage(adjoint, m_Base) - AcNodeVoltage(adjoint, m_B);
        variance += (FOUR_KT_NOMINAL / p.BaseResistance) * std::norm(rb_tf);
    }
    if (m_E != m_Emitter && p.EmitterResistance > 0.0) {
        const std::complex<double> re_tf = AcNodeVoltage(adjoint, m_Emitter) - AcNodeVoltage(adjoint, m_E);
        variance += (FOUR_KT_NOMINAL / p.EmitterResistance) * std::norm(re_tf);
    }
    const Intrinsic op = EvaluateAt(x_dc);
    const std::complex<double> ic_tf = AcNodeVoltage(adjoint, m_C) - AcNodeVoltage(adjoint, m_E);
    const std::complex<double> ib_tf = AcNodeVoltage(adjoint, m_B) - AcNodeVoltage(adjoint, m_E);
    variance += TWO_Q * std::fabs(op.Ic) * std::norm(ic_tf);
    variance += TWO_Q * std::fabs(op.Ib) * std::norm(ib_tf);
    return variance;
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
    const BjtParameters &p = m_Parameters;
    const Intrinsic dc = EvaluateAt(x);
    const double charge_be = EvaluateDepletion(vbe_signed, p.EmitterCapacitance, BJT_JUNCTION_POTENTIAL,
                                               BJT_GRADING_COEFFICIENT, JUNCTION_FORWARD_LIMIT)
                                 .Charge +
                             p.TransitTime * dc.IfwdQb;
    const double charge_bc = EvaluateDepletion(vbc_signed, p.CollectorCapacitance, BJT_JUNCTION_POTENTIAL,
                                               BJT_GRADING_COEFFICIENT, JUNCTION_FORWARD_LIMIT)
                                 .Charge;
    m_HistoryBe = AdvanceHistory(m_HistoryBe, charge_be, m_Gqbe * vbe_signed + m_Ieqbe);
    m_HistoryBc = AdvanceHistory(m_HistoryBc, charge_bc, m_Gqbc * vbc_signed + m_Ieqbc);
    m_SavedHistoryBe = m_HistoryBe;
    m_SavedHistoryBc = m_HistoryBc;
}

void BjtDevice::RejectStep() {
    m_HistoryBe = m_SavedHistoryBe;
    m_HistoryBc = m_SavedHistoryBc;
}

} // namespace Nodalys
