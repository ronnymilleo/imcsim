/**
 * @file    bjt_device.h
 * @brief   Bipolar transistor of Nodalys, the SPICE Gummel-Poon model.
 */

#ifndef NODALYS_BJT_DEVICE_H
#define NODALYS_BJT_DEVICE_H

#include "devices/devices.h"
#include "nodalys/types.h"

namespace Nodalys {

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
              BjtParameters parameters, bool pnp);

    void Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) override;
    void StampAc(MnaSystem &reactive, const std::vector<double> &x_dc) const override;
    bool IsLinear() const override { return false; }
    void StampCharge(MnaSystem &sys, const std::vector<double> &x) const override;
    bool Limit(std::vector<double> &x_new, const std::vector<double> &x_old) const override;
    void SeedOperatingPoint(std::vector<double> &x) const override;
    double TerminalCurrent(int terminal, const std::vector<double> &x) const override;
    double TerminalCharge(int terminal, const std::vector<double> &x) const override;
    std::complex<double> AcTerminalCurrent(int terminal, const std::vector<double> &x_dc,
                                           const std::vector<std::complex<double>> &x_ac, double omega) const override;
    double OutputNoiseVariance(const std::vector<double> &x_dc, const std::vector<std::complex<double>> &adjoint,
                               double frequency_hz) const override;
    bool HasTransientState() const override;
    void AcceptStep(const std::vector<double> &x) override;
    void RejectStep() override;

private:
    struct Intrinsic {
        double Ic;     ///< DC collector current (entering intrinsic collector).
        double Ib;     ///< DC base current (entering intrinsic base).
        double Ie;     ///< DC emitter current, $-(I_C + I_B)$.
        double GcBe;   ///< $\partial I_C / \partial v_{be}$.
        double GcBc;   ///< $\partial I_C / \partial v_{bc}$.
        double GbBe;   ///< $\partial I_B / \partial v_{be}$.
        double GbBc;   ///< $\partial I_B / \partial v_{bc}$.
        double IfwdQb; ///< $I_{\text{fwd}} / q_b$ for Gummel-Poon forward transit-time charge $Q_{DE} = \tau_F
                       ///< I_{\text{fwd}} / q_b$.
        double
            DIfwdQbDvbe; ///< $\partial (I_{\text{fwd}} / q_b) / \partial v_{be}$ for Gummel-Poon diffusion capacitance.
        double DIfwdQbDvbc; ///< $\partial (I_{\text{fwd}} / q_b) / \partial v_{bc}$ for SPICE `geqcb` base-width
                            ///< modulation transcapacitance.
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
    BjtParameters m_Parameters;
    bool m_Pnp;
    double m_Vt;
    double m_Critical;
    /// Trapezoidal charge histories of the BE and BC junctions.
    ChargeHistory m_HistoryBe, m_SavedHistoryBe;
    ChargeHistory m_HistoryBc, m_SavedHistoryBc;
    double m_Gqbe = 0.0, m_Ieqbe = 0.0, m_Gqbc = 0.0, m_Ieqbc = 0.0;
};

} // namespace Nodalys

#endif // NODALYS_BJT_DEVICE_H
