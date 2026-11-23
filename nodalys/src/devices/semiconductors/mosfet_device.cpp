/**
 * @file    mosfet_device.cpp
 * @brief   Level-1 MOSFET of Nodalys, the SPICE Shichman-Hodges model.
 * @details The helpers the nonlinear devices share are in semiconductor_math.h.
 */

#include "devices/semiconductors/mosfet_device.h"

#include "devices/pn_junction.h"
#include "devices/semiconductor_math.h"
#include <algorithm>
#include <cmath>

namespace Nodalys {

namespace {

/// Default SPICE saturation current (\f$I_S = 10^{-14}\text{ A}\f$) of the level-1 MOSFET body-drain diode.
constexpr double MOSFET_BODY_SATURATION_CURRENT = 1e-14;

} // namespace

/**
 * @brief   Constructs an NMOS or PMOS Shichman-Hodges level-1 MOSFET (`M` element).
 * @param[in] drain            External drain MNA node index.
 * @param[in] gate             External gate MNA node index.
 * @param[in] source           External source MNA node index.
 * @param[in] internal_drain   Internal drain MNA node index (negative if \f$R_D = 0\f$).
 * @param[in] internal_source  Internal source MNA node index (negative if \f$R_S = 0\f$).
 * @param[in] parameters       MOSFET model parameters at nominal temperature.
 * @param[in] pmos             True for PMOS polarity, false for NMOS.
 */
MosfetDevice::MosfetDevice(const int drain,
                           const int gate,
                           const int source,
                           const int internal_drain,
                           const int internal_source,
                           MosfetParameters parameters,
                           const bool pmos)
    : m_Drain(drain), m_Gate(gate), m_Source(source), m_D(internal_drain >= 0 ? internal_drain : drain),
      m_S(internal_source >= 0 ? internal_source : source), m_Given(parameters), m_Parameters(parameters), m_Pmos(pmos),
      m_Beta(parameters.Transconductance * parameters.Width / parameters.Length), m_Vt(THERMAL_VOLTAGE),
      m_Cgs(parameters.GateSourceOverlap * parameters.Width), m_Cgd(parameters.GateDrainOverlap * parameters.Width),
      m_Critical(PnCriticalVoltage(MOSFET_BODY_SATURATION_CURRENT, THERMAL_VOLTAGE)) {
    ApplyTemperature(m_Thermal);
}

/**
 * @brief   Scales the transconductance, the threshold voltage and the body-diode thermal voltage with the temperature.
 * @details In SPICE level 1 (`mos1temp.c`), the mobility drops as \f$(T / T_{nom})^{-3/2}\f$ and the threshold voltage
 *          follows half the shift of the surface potential \f$\phi\f$ and the silicon band gap:
 *          \f$V_{TO}(T) = V_{TO} + \frac{1}{2} (s (\phi(T) - \phi) - (E_g(T) - E_g(T_{nom})))\f$ with \f$s = +1\f$ for
 * NMOS and \f$-1\f$ for PMOS. The series resistances and overlap capacitances have no coefficient set, so they stay.
 */
void MosfetDevice::ApplyTemperature(const ThermalConditions &thermal) {
    m_Thermal = thermal;
    m_Parameters = m_Given;
    const double ratio = thermal.TemperatureKelvin / thermal.NominalKelvin;
    m_Parameters.Transconductance = m_Given.Transconductance * std::pow(ratio, -1.5);
    m_Beta = m_Parameters.Transconductance * m_Parameters.Width / m_Parameters.Length;
    const double phi_t = GetJunctionPotential(thermal, m_Given.SurfacePotential);
    const double eg_delta = GetBandGap(thermal.TemperatureKelvin) - GetBandGap(thermal.NominalKelvin);
    const double polarity = m_Pmos ? -1.0 : 1.0;
    m_Parameters.ThresholdVoltage =
        m_Given.ThresholdVoltage + 0.5 * (polarity * (phi_t - m_Given.SurfacePotential) - eg_delta);
    m_Vt = GetThermalVoltage(thermal);
    m_Critical = PnCriticalVoltage(MOSFET_BODY_SATURATION_CURRENT, m_Vt);
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
        result.Gds = m_Beta * (vov - vds_eff) * (1.0 + lambda * vds_eff)
                   + m_Beta * (vov * vds_eff - vds_eff * vds_eff / 2.0) * lambda;
    } else {
        // Saturation region: I = (Beta / 2) * vov^2 * (1 + lambda * vds)
        result.Current = (m_Beta / 2.0) * vov * vov * (1.0 + lambda * vds_eff);
        result.Gm = m_Beta * vov * (1.0 + lambda * vds_eff);
        result.Gds = (m_Beta / 2.0) * vov * vov * lambda;
    }
    return result;
}

/**
 * @brief   Finds which terminal acts as the drain and evaluates the channel in that frame.
 * @details The level-1 equations only hold for \f$v_{ds} \ge 0\f$, so when the intrinsic drain sits on the wrong side
 * of the source (judged on the circuit voltages, with the sign of the polarity) the two swap roles.
 * @param[in] x  Solution vector.
 * @return  The effective drain and source nodes, whether they swapped, and the channel evaluated between them.
 */
MosfetDevice::ChannelState MosfetDevice::EvaluateChannel(const std::vector<double> &x) const {
    const double vg = NodeVoltage(x, m_Gate);
    const double vd = NodeVoltage(x, m_D);
    const double vs = NodeVoltage(x, m_S);

    ChannelState state{};
    state.Polarity = m_Pmos ? -1.0 : 1.0;
    state.IsReversed = (state.Polarity * (vd - vs)) < 0.0;
    state.Drain = state.IsReversed ? m_S : m_D;
    state.Source = state.IsReversed ? m_D : m_S;
    const double vgs_eff = state.Polarity * (vg - NodeVoltage(x, state.Source));
    const double vds_eff = state.Polarity * (NodeVoltage(x, state.Drain) - NodeVoltage(x, state.Source));
    state.Conduction = Evaluate(vgs_eff, vds_eff);
    return state;
}

/**
 * @brief   Finds the terminals of the body-drain diode.
 * @details The body is tied to the external source terminal. For NMOS the anode is the source and the cathode the
 *          drain; for PMOS the anode is the drain and the cathode the source.
 */
MosfetDevice::BodyDiodeNodes MosfetDevice::GetBodyDiodeNodes() const {
    return {.Anode = m_Pmos ? m_D : m_Source, .Cathode = m_Pmos ? m_Source : m_D};
}

void MosfetDevice::Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) {
    StampSeriesResistor(sys, m_Drain, m_D, m_Parameters.DrainResistance);
    StampSeriesResistor(sys, m_Source, m_S, m_Parameters.SourceResistance);

    // 1. Channel conduction (with symmetric source/drain role swap when Vds_eff < 0)
    const ChannelState state = EvaluateChannel(x);
    const Channel &channel = state.Conduction;
    const int d_eff = state.Drain;
    const int s_eff = state.Source;
    const double vg = NodeVoltage(x, m_Gate);
    const double vd_eff = NodeVoltage(x, d_eff);
    const double vs_eff = NodeVoltage(x, s_eff);

    // Small-signal companion between d_eff and s_eff:
    //   I(d_eff -> s_eff) ≈ Gm * (V(Gate) - V(s_eff)) + Gds * (V(d_eff) - V(s_eff)) + ieq
    const double i_channel = state.Polarity * channel.Current;
    const double ieq = i_channel - channel.Gm * (vg - vs_eff) - channel.Gds * (vd_eff - vs_eff);

    StampConductance(sys, d_eff, s_eff, channel.Gds);
    sys.AddA(d_eff, m_Gate, channel.Gm);
    sys.AddA(d_eff, s_eff, -channel.Gm);
    sys.AddA(s_eff, m_Gate, -channel.Gm);
    sys.AddA(s_eff, s_eff, channel.Gm);
    StampCurrentSource(sys, d_eff, s_eff, ieq);

    // 2. Body-drain PN junction diode (body is tied to the external source terminal):
    const BodyDiodeNodes body = GetBodyDiodeNodes();
    const double vbody = NodeVoltage(x, body.Anode) - NodeVoltage(x, body.Cathode);
    const double diode_exp = Exponential(vbody / m_Vt);
    const double diode_current = MOSFET_BODY_SATURATION_CURRENT * (diode_exp - 1.0);
    const double diode_cond = MOSFET_BODY_SATURATION_CURRENT * diode_exp / m_Vt;
    StampNortonBranch(sys, body.Anode, body.Cathode, diode_cond, diode_current, vbody);

    // 3. Linear gate-overlap capacitances (CGSO*W between Gate-Source, CGDO*W between Gate-Drain)
    m_Gcgs = m_Gcgd = m_Ieqgs = m_Ieqgd = 0.0;
    if (time.IsTransient && HasTransientState()) {
        const double vgs = NodeVoltage(x, m_Gate) - NodeVoltage(x, m_Source);
        const double vgd = NodeVoltage(x, m_Gate) - NodeVoltage(x, m_Drain);
        const ChargeCompanion cap_gs = ChargeModel(m_Cgs, m_Cgs * vgs, vgs, m_HistoryGs, time);
        const ChargeCompanion cap_gd = ChargeModel(m_Cgd, m_Cgd * vgd, vgd, m_HistoryGd, time);
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
    const ChannelState state = EvaluateChannel(x);

    // Channel current entering the physical drain terminal m_D
    const double i_d_eff_to_s_eff = state.Polarity * state.Conduction.Current;
    const double channel_into_drain = state.IsReversed ? -i_d_eff_to_s_eff : i_d_eff_to_s_eff;

    // Body diode current entering the physical drain terminal m_D
    const BodyDiodeNodes body = GetBodyDiodeNodes();
    const double vbody = NodeVoltage(x, body.Anode) - NodeVoltage(x, body.Cathode);
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

void MosfetDevice::StampAc(MnaSystem &reactive, const std::vector<double> &) const {
    if (m_Cgs > 0.0) {
        StampConductance(reactive, m_Gate, m_Source, m_Cgs);
    }
    if (m_Cgd > 0.0) {
        StampConductance(reactive, m_Gate, m_Drain, m_Cgd);
    }
}

void MosfetDevice::StampCharge(MnaSystem &sys, const std::vector<double> & /*x*/) const {
    if (m_Cgs > 0.0) {
        StampConductance(sys, m_Gate, m_Source, m_Cgs);
    }
    if (m_Cgd > 0.0) {
        StampConductance(sys, m_Gate, m_Drain, m_Cgd);
    }
}

double MosfetDevice::TerminalCharge(const int terminal, const std::vector<double> &x) const {
    const double q_gs = m_Cgs * (NodeVoltage(x, m_Gate) - NodeVoltage(x, m_Source));
    const double q_gd = m_Cgd * (NodeVoltage(x, m_Gate) - NodeVoltage(x, m_Drain));
    switch (terminal) {
    case 0: // Drain
        return -q_gd;
    case 1: // Gate
        return q_gs + q_gd;
    default: // Source
        return -q_gs;
    }
}

std::complex<double> MosfetDevice::AcTerminalCurrent(const int terminal,
                                                     const std::vector<double> &x_dc,
                                                     const std::vector<std::complex<double>> &x_ac,
                                                     const double omega) const {
    const ChannelState state = EvaluateChannel(x_dc);
    const Channel &channel = state.Conduction;

    const std::complex<double> vg_ac = AcNodeVoltage(x_ac, m_Gate);
    const std::complex<double> vd_eff_ac = AcNodeVoltage(x_ac, state.Drain);
    const std::complex<double> vs_eff_ac = AcNodeVoltage(x_ac, state.Source);
    const std::complex<double> i_d_eff_to_s_eff =
        channel.Gm * (vg_ac - vs_eff_ac) + channel.Gds * (vd_eff_ac - vs_eff_ac);
    const std::complex<double> channel_into_drain = state.IsReversed ? -i_d_eff_to_s_eff : i_d_eff_to_s_eff;

    const BodyDiodeNodes body = GetBodyDiodeNodes();
    const double vbody_dc = NodeVoltage(x_dc, body.Anode) - NodeVoltage(x_dc, body.Cathode);
    const double diode_cond = MOSFET_BODY_SATURATION_CURRENT * Exponential(vbody_dc / m_Vt) / m_Vt;
    const std::complex<double> vbody_ac = AcNodeVoltage(x_ac, body.Anode) - AcNodeVoltage(x_ac, body.Cathode);
    const std::complex<double> diode_into_drain = (m_Pmos ? diode_cond : -diode_cond) * vbody_ac;

    const std::complex<double> igs_ac =
        std::complex<double>(0.0, omega * m_Cgs) * (vg_ac - AcNodeVoltage(x_ac, m_Source));
    const std::complex<double> igd_ac =
        std::complex<double>(0.0, omega * m_Cgd) * (vg_ac - AcNodeVoltage(x_ac, m_Drain));

    switch (terminal) {
    case 0: // Drain
        return channel_into_drain + diode_into_drain - igd_ac;
    case 1: // Gate
        return igs_ac + igd_ac;
    default: // Source
        return -(channel_into_drain + diode_into_drain) - igs_ac;
    }
}

double MosfetDevice::OutputNoiseVariance(const std::vector<double> &x_dc,
                                         const std::vector<std::complex<double>> &adjoint,
                                         const double /*frequency_hz*/) const {
    const MosfetParameters &p = m_Parameters;
    const double four_kt = GetFourKt(m_Thermal);
    double variance = 0.0;
    if (m_D != m_Drain && p.DrainResistance > 0.0) {
        const std::complex<double> rd_tf = AcNodeVoltage(adjoint, m_Drain) - AcNodeVoltage(adjoint, m_D);
        variance += (four_kt / p.DrainResistance) * std::norm(rd_tf);
    }
    if (m_S != m_Source && p.SourceResistance > 0.0) {
        const std::complex<double> rs_tf = AcNodeVoltage(adjoint, m_Source) - AcNodeVoltage(adjoint, m_S);
        variance += (four_kt / p.SourceResistance) * std::norm(rs_tf);
    }
    const Channel channel = EvaluateChannel(x_dc).Conduction;
    const std::complex<double> ch_tf = AcNodeVoltage(adjoint, m_D) - AcNodeVoltage(adjoint, m_S);
    variance += (2.0 / 3.0) * four_kt * std::fabs(channel.Gm) * std::norm(ch_tf);
    return variance;
}

bool MosfetDevice::Limit(std::vector<double> &x_new, const std::vector<double> &x_old) const {
    const BodyDiodeNodes body = GetBodyDiodeNodes();
    const double voltage_new = NodeVoltage(x_new, body.Anode) - NodeVoltage(x_new, body.Cathode);
    const double voltage_old = NodeVoltage(x_old, body.Anode) - NodeVoltage(x_old, body.Cathode);
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
    m_HistoryGs = AdvanceHistory(m_HistoryGs, m_Cgs * vgs, m_Gcgs * vgs + m_Ieqgs);
    m_HistoryGd = AdvanceHistory(m_HistoryGd, m_Cgd * vgd, m_Gcgd * vgd + m_Ieqgd);
    m_SavedHistoryGs = m_HistoryGs;
    m_SavedHistoryGd = m_HistoryGd;
}

void MosfetDevice::RejectStep() {
    m_HistoryGs = m_SavedHistoryGs;
    m_HistoryGd = m_SavedHistoryGd;
}

/**
 * @brief   Moves the MOSFET to the temperature of the run.
 * @param[in] thermal  Temperature of the run and the nominal temperature the parameters are given at.
 */
void MosfetDevice::SetTemperature(const ThermalConditions &thermal) {
    ApplyTemperature(thermal);
}

std::unique_ptr<Device> MosfetDevice::Clone() const {
    return std::make_unique<MosfetDevice>(*this);
}

} // namespace Nodalys
