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
    BjtDevice(int collector,
              int base,
              int emitter,
              int internal_collector,
              int internal_base,
              int internal_emitter,
              BjtParameters parameters,
              bool pnp);

    void Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) override;
    void StampAc(MnaSystem &reactive, const std::vector<double> &x_dc) const override;
    bool IsLinear() const override { return false; }
    void StampCharge(MnaSystem &sys, const std::vector<double> &x) const override;
    bool Limit(std::vector<double> &x_new, const std::vector<double> &x_old) const override;
    void SeedOperatingPoint(std::vector<double> &x) const override;
    double TerminalCurrent(int terminal, const std::vector<double> &x) const override;
    double TerminalCharge(int terminal, const std::vector<double> &x) const override;
    std::complex<double> AcTerminalCurrent(int terminal,
                                           const std::vector<double> &x_dc,
                                           const std::vector<std::complex<double>> &x_ac,
                                           double omega) const override;
    double OutputNoiseVariance(const std::vector<double> &x_dc,
                               const std::vector<std::complex<double>> &adjoint,
                               double frequency_hz) const override;
    bool HasTransientState() const override;
    void AcceptStep(const std::vector<double> &x) override;
    void RejectStep() override;
    void SetTemperature(const ThermalConditions &thermal) override;

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
    /// Operating point of the intrinsic transistor at one solution vector, in the polarity of the circuit.
    struct JunctionState {
        double Vbe;       ///< Base-emitter voltage as the circuit sees it.
        double Vbc;       ///< Base-collector voltage as the circuit sees it.
        double VbeSigned; ///< $v_{be}$ flipped for a PNP, so it is forward-positive in both polarities.
        double VbcSigned; ///< $v_{bc}$ flipped for a PNP, so it is forward-positive in both polarities.
        Intrinsic Dc;     ///< Currents and conductances in the polarity of the circuit.
    };
    /// Charges and capacitances of the two junctions in forward-positive polarity, depletion plus transit time.
    struct JunctionCharges {
        double BaseEmitterCharge;        ///< $Q_{BE}$, depletion charge plus $\tau_F I_{\text{fwd}} / q_b$.
        double BaseEmitterCapacitance;   ///< $\partial Q_{BE} / \partial v_{be}$.
        double BaseCollectorCharge;      ///< $Q_{BC}$, depletion charge.
        double BaseCollectorCapacitance; ///< $\partial Q_{BC} / \partial v_{bc}$.
        /// SPICE `geqcb`, $\partial Q_{BE} / \partial v_{bc}$: the base-width modulation transcapacitance.
        double Transcapacitance;
    };
    /// Recomputes the parameters at the temperature, from the ones given at the nominal temperature.
    void ApplyTemperature(const ThermalConditions &thermal);
    /// Evaluates the intrinsic transport equations in NPN polarity ($v_{be}, v_{bc}$ forward-positive).
    Intrinsic Evaluate(double vbe, double vbc) const;
    /// Reads the junction voltages from `x` and evaluates the intrinsic transistor there.
    JunctionState EvaluateState(const std::vector<double> &x) const;
    /// Evaluates the charges of both junctions at a junction state.
    JunctionCharges EvaluateCharges(const JunctionState &state) const;

    int m_Collector;
    int m_Base;
    int m_Emitter;
    int m_C; ///< Intrinsic collector node (`internal_collector` if $R_C > 0$, else `m_Collector`).
    int m_B; ///< Intrinsic base node (`internal_base` if $R_B > 0$, else `m_Base`).
    int m_E; ///< Intrinsic emitter node (`internal_emitter` if $R_E > 0$, else `m_Emitter`).
    /// The parameters as the model gives them, at the nominal temperature.
    BjtParameters m_Given;
    /// The parameters at the temperature of the run, which every evaluation reads.
    BjtParameters m_Parameters;
    ThermalConditions m_Thermal;
    bool m_Pnp;
    /// Potential of both junctions at the temperature of the run, since they share the default $V_{JE} = V_{JC}$.
    double m_JunctionPotential;
    double m_Vt;
    double m_Critical;
    /// Trapezoidal charge histories of the BE and BC junctions.
    ChargeHistory m_HistoryBe, m_SavedHistoryBe;
    ChargeHistory m_HistoryBc, m_SavedHistoryBc;
    double m_Gqbe = 0.0, m_Ieqbe = 0.0, m_Gqbc = 0.0, m_Ieqbc = 0.0;
};

} // namespace Nodalys

#endif // NODALYS_BJT_DEVICE_H
