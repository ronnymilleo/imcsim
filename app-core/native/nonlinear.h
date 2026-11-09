/**
 * @file    nonlinear.h
 * @brief   Nonlinear semiconductor devices of the native solver: diodes (`DiodeDevice`), bipolar junction transistors
 *          (`BjtDevice`, Gummel-Poon model), and MOSFETs (`MosfetDevice`, Shichman-Hodges level-1 model).
 * @details
 * ### How Nonlinear Devices Are Solved in MNA
 * At each Newton-Raphson iteration $k$, every nonlinear branch $I(v)$ is linearized around the current iterate $v^{(k)}$
 * via its first-order Taylor expansion (Norton equivalent companion model):
 *
 * $$I(v) \approx I(v^{(k)}) + G_{\text{eq}} \cdot (v - v^{(k)}) = G_{\text{eq}} \cdot v + I_{\text{eq}}$$
 *
 * where:
 * - $G_{\text{eq}} = \left.\frac{\partial I}{\partial v}\right|_{v^{(k)}}$ is the differential conductance (stamped into $A$),
 * - $I_{\text{eq}} = I(v^{(k)}) - G_{\text{eq}} \cdot v^{(k)}$ is the equivalent independent current source (stamped into $\text{rhs}$).
 *
 * ```text
 *         node_a o----+----[ G_eq ]----+----o node_b
 *                     |                |
 *                     +---(  I_eq  )---+
 *                            --->
 * ```
 *
 * After solving the linear system for $x^{(k+1)}$, each device's `Limit()` method applies SPICE's `PNJLIM` damping
 * (`pn_junction.h`) so forward-biased junction voltages cannot jump by more than a few thermal voltages $V_T$ in a
 * single iteration, preventing exponential overflow.
 */

#ifndef IMCSIM_NATIVE_NONLINEAR_H
#define IMCSIM_NATIVE_NONLINEAR_H

#include "components/bjt.h"
#include "components/diode.h"
#include "components/mosfet.h"
#include "native/devices.h"

namespace Core::Native {

/**
 * @class   DiodeDevice
 * @brief   A diode, Zener diode, or LED: Shockley exponential junction with series resistance $R_S$, reverse breakdown
 *          ($BV, I_{BV}$), and SPICE depletion + transit-time junction charge.
 * @details
 * Topology when $R_S > 0$:
 * ```text
 *   anode o---[ Intrinsic Junction + Charge ]---o internal_node o---[ R_S ]---o cathode
 * ```
 * When $R_S = 0$, `internal` is `-1` and the intrinsic junction connects directly between `anode` and `cathode`.
 */
class DiodeDevice : public Device {
public:
    DiodeDevice(int anode, int cathode, int internal, DiodeParameters parameters);

    void Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) override;
    void StampAc(MnaSystem &reactive, const std::vector<double> &x_dc) const override;
    bool Limit(std::vector<double> &x_new, const std::vector<double> &x_old) const override;
    void SeedOperatingPoint(std::vector<double> &x) const override;
    double TerminalCurrent(int terminal, const std::vector<double> &x) const override;
    std::complex<double> AcTerminalCurrent(int terminal, const std::vector<double> &x_dc,
                                           const std::vector<std::complex<double>> &x_ac, double omega) const override;
    double OutputNoiseVariance(const std::vector<double> &x_dc, const std::vector<std::complex<double>> &adjoint,
                               double frequency_hz) const override;
    bool HasTransientState() const override;
    void AcceptStep(const std::vector<double> &x) override;
    void RejectStep() override;

private:
    struct Junction {
        double Current;
        double Conductance;
    };
    Junction Evaluate(double voltage) const;
    double Charge(double voltage) const;
    double Capacitance(double voltage) const;
    double VoltageAt(const std::vector<double> &x) const;

    int m_Anode;
    int m_Cathode;
    int m_Internal;
    int m_JunctionNode; ///< Cathode side of the intrinsic junction (`m_Internal` when $R_S > 0$, else `m_Cathode`).
    DiodeParameters m_Parameters;
    double m_Vt;
    double m_Critical;
    /// Trapezoidal charge history at the last accepted time point.
    double m_Charge = 0.0;
    double m_ChargeCurrent = 0.0;
    double m_SavedCharge = 0.0;
    double m_SavedChargeCurrent = 0.0;
    /// Companion model of the junction capacitance from the most recent transient stamp.
    double m_CapConductance = 0.0;
    double m_CapEquivalent = 0.0;
};

/**
 * @class   BjtDevice
 * @brief   An NPN or PNP bipolar transistor using the SPICE Gummel-Poon transport model.
 * @details
 * Terminal indices: `0 = Collector`, `1 = Base`, `2 = Emitter`.
 * Series resistances $R_C, R_B, R_E$ place internal nodes (`m_C, m_B, m_E`) between the external pins and the
 * intrinsic transistor when nonzero.
 */
class BjtDevice : public Device {
public:
    BjtDevice(int collector, int base, int emitter, int internal_collector, int internal_base, int internal_emitter,
              BJTParameters parameters, bool pnp);

    void Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) override;
    void StampAc(MnaSystem &reactive, const std::vector<double> &x_dc) const override;
    bool Limit(std::vector<double> &x_new, const std::vector<double> &x_old) const override;
    void SeedOperatingPoint(std::vector<double> &x) const override;
    double TerminalCurrent(int terminal, const std::vector<double> &x) const override;
    std::complex<double> AcTerminalCurrent(int terminal, const std::vector<double> &x_dc,
                                           const std::vector<std::complex<double>> &x_ac, double omega) const override;
    double OutputNoiseVariance(const std::vector<double> &x_dc, const std::vector<std::complex<double>> &adjoint,
                               double frequency_hz) const override;
    bool HasTransientState() const override;
    void AcceptStep(const std::vector<double> &x) override;
    void RejectStep() override;

private:
    struct Intrinsic {
        double Ic;   ///< DC collector current (entering intrinsic collector).
        double Ib;   ///< DC base current (entering intrinsic base).
        double Ie;   ///< DC emitter current, $-(I_C + I_B)$.
        double GcBe; ///< $\partial I_C / \partial v_{be}$.
        double GcBc; ///< $\partial I_C / \partial v_{bc}$.
        double GbBe; ///< $\partial I_B / \partial v_{be}$.
        double GbBc; ///< $\partial I_B / \partial v_{bc}$.
        double IfwdQb;      ///< $I_{\text{fwd}} / q_b$ for Gummel-Poon forward transit-time charge $Q_{DE} = \tau_F I_{\text{fwd}} / q_b$.
        double DIfwdQbDvbe; ///< $\partial (I_{\text{fwd}} / q_b) / \partial v_{be}$ for Gummel-Poon diffusion capacitance.
        double DIfwdQbDvbc; ///< $\partial (I_{\text{fwd}} / q_b) / \partial v_{bc}$ for SPICE `geqcb` base-width modulation transcapacitance.
    };
    /// Evaluates the intrinsic transport equations in NPN polarity ($v_{be}, v_{bc}$ forward-positive).
    Intrinsic Evaluate(double vbe, double vbc) const;
    /// Evaluates the intrinsic transistor at state vector `x`, accounting for NPN/PNP polarity.
    Intrinsic EvaluateAt(const std::vector<double> &x) const;

    int m_Collector;
    int m_Base;
    int m_Emitter;
    int m_C; ///< Intrinsic collector node (`internal_collector` if $R_C > 0$, else `m_Collector`).
    int m_B; ///< Intrinsic base node (`internal_base` if $R_B > 0$, else `m_Base`).
    int m_E; ///< Intrinsic emitter node (`internal_emitter` if $R_E > 0$, else `m_Emitter`).
    BJTParameters m_Parameters;
    bool m_Pnp;
    double m_Vt;
    double m_Critical;
    /// Trapezoidal charge histories of the BE and BC junctions.
    double m_Qbe = 0.0, m_IQbe = 0.0, m_SavedQbe = 0.0, m_SavedIQbe = 0.0;
    double m_Qbc = 0.0, m_IQbc = 0.0, m_SavedQbc = 0.0, m_SavedIQbc = 0.0;
    double m_Gqbe = 0.0, m_Ieqbe = 0.0, m_Gqbc = 0.0, m_Ieqbc = 0.0;
};

/**
 * @class   MosfetDevice
 * @brief   An N-channel or P-channel MOSFET using the SPICE level-1 (Shichman-Hodges) model with symmetric
 *          source/drain reversal for $V_{DS} < 0$, body-drain diode, and gate-overlap capacitances.
 * @details
 * Terminal indices: `0 = Drain`, `1 = Gate`, `2 = Source`.
 */
class MosfetDevice : public Device {
public:
    MosfetDevice(int drain, int gate, int source, int internal_drain, int internal_source, MOSFETParameters parameters,
                 bool pmos);

    void Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) override;
    void StampAc(MnaSystem &reactive, const std::vector<double> &x_dc) const override;
    bool Limit(std::vector<double> &x_new, const std::vector<double> &x_old) const override;
    void SeedOperatingPoint(std::vector<double> &x) const override;
    double TerminalCurrent(int terminal, const std::vector<double> &x) const override;
    std::complex<double> AcTerminalCurrent(int terminal, const std::vector<double> &x_dc,
                                           const std::vector<std::complex<double>> &x_ac, double omega) const override;
    double OutputNoiseVariance(const std::vector<double> &x_dc, const std::vector<std::complex<double>> &adjoint,
                               double frequency_hz) const override;
    bool HasTransientState() const override;
    void AcceptStep(const std::vector<double> &x) override;
    void RejectStep() override;

private:
    struct Channel {
        double Current; ///< Magnitude of channel current ($\ge 0$) in the effective drain-to-source frame.
        double Gm;      ///< Transconductance $\partial I / \partial v_{gs,\text{eff}} \ge 0$.
        double Gds;     ///< Output conductance $\partial I / \partial v_{ds,\text{eff}} \ge 0$.
    };
    /// Evaluates the level-1 channel equations for nonnegative effective $v_{ds} \ge 0$.
    Channel Evaluate(double vgs_eff, double vds_eff) const;
    double Vgs(const std::vector<double> &x) const;
    double Vds(const std::vector<double> &x) const;

    int m_Drain;
    int m_Gate;
    int m_Source;
    int m_D; ///< Intrinsic drain node (`internal_drain` if $R_D > 0$, else `m_Drain`).
    int m_S; ///< Intrinsic source node (`internal_source` if $R_S > 0$, else `m_Source`).
    MOSFETParameters m_Parameters;
    bool m_Pmos;
    double m_Beta; ///< $\text{KP} \cdot (W / L)$.
    double m_Vt;
    /// Gate-source ($C_{GSO} \cdot W$) and gate-drain ($C_{GDO} \cdot W$) linear overlap capacitances.
    double m_Cgs;
    double m_Cgd;
    double m_Vgs = 0.0, m_Igs = 0.0, m_SavedVgs = 0.0, m_SavedIgs = 0.0;
    double m_Vgd = 0.0, m_Igd = 0.0, m_SavedVgd = 0.0, m_SavedIgd = 0.0;
    double m_Gcgs = 0.0, m_Ieqgs = 0.0, m_Gcgd = 0.0, m_Ieqgd = 0.0;
    double m_Critical; ///< `PNJLIM` critical voltage of the body-drain junction diode.
};

} // namespace Core::Native

#endif // IMCSIM_NATIVE_NONLINEAR_H
