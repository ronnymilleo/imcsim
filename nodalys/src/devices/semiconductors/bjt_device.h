/**
 * @file    bjt_device.h
 * @brief   Bipolar transistor of Nodalys, the SPICE Gummel-Poon model.
 */

#ifndef NODALYS_BJT_DEVICE_H
#define NODALYS_BJT_DEVICE_H

#include "devices/device.h"
#include "nodalys/types.h"

namespace Nodalys {

/**
 * @class   BjtDevice
 * @brief   An NPN or PNP bipolar transistor using the SPICE Gummel-Poon transport model.
 * @details
 * Terminal indices: `0 = Collector`, `1 = Base`, `2 = Emitter`.
 * Series resistances \f$R_C, R_B, R_E\f$ place internal nodes (`m_C, m_B, m_E`) between the external pins and the
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
    [[nodiscard]] std::unique_ptr<Device> Clone() const override;

private:
    struct Intrinsic {
        /// DC collector current (entering intrinsic collector).
        double Ic;
        /// DC base current (entering intrinsic base).
        double Ib;
        /// DC emitter current, \f$-(I_C + I_B)\f$.
        double Ie;
        /// \f$\partial I_C / \partial v_{be}\f$.
        double GcBe;
        /// \f$\partial I_C / \partial v_{bc}\f$.
        double GcBc;
        /// \f$\partial I_B / \partial v_{be}\f$.
        double GbBe;
        /// \f$\partial I_B / \partial v_{bc}\f$.
        double GbBc;
        /// \f$I_{\text{fwd}} / q_b\f$ for Gummel-Poon forward transit-time charge \f$Q_{DE} = \tau_F I_{\text{fwd}} /
        /// q_b\f$.
        double IfwdQb;
        /// \f$\partial (I_{\text{fwd}} / q_b) / \partial v_{be}\f$ for Gummel-Poon diffusion capacitance.
        double DIfwdQbDvbe;
        /// \f$\partial (I_{\text{fwd}} / q_b) / \partial v_{bc}\f$ for SPICE `geqcb` base-width modulation
        /// transcapacitance.
        double DIfwdQbDvbc;
    };

    /// Operating point of the intrinsic transistor at one solution vector, in the polarity of the circuit.
    struct JunctionState {
        /// Base-emitter voltage as the circuit sees it.
        double Vbe;
        /// Base-collector voltage as the circuit sees it.
        double Vbc;
        /// \f$v_{be}\f$ flipped for a PNP, so it is forward-positive in both polarities.
        double VbeSigned;
        /// \f$v_{bc}\f$ flipped for a PNP, so it is forward-positive in both polarities.
        double VbcSigned;
        /// Currents and conductances in the polarity of the circuit.
        Intrinsic Dc;
    };

    /// Charges and capacitances of the two junctions in forward-positive polarity, depletion plus transit time.
    struct JunctionCharges {
        /// \f$Q_{BE}\f$, depletion charge plus \f$\tau_F I_{\text{fwd}} / q_b\f$.
        double BaseEmitterCharge;
        /// \f$\partial Q_{BE} / \partial v_{be}\f$.
        double BaseEmitterCapacitance;
        /// \f$Q_{BC}\f$, depletion charge.
        double BaseCollectorCharge;
        /// \f$\partial Q_{BC} / \partial v_{bc}\f$.
        double BaseCollectorCapacitance;
        /// SPICE `geqcb`, \f$\partial Q_{BE} / \partial v_{bc}\f$: the base-width modulation transcapacitance.
        double Transcapacitance;
    };

    int m_Collector;
    int m_Base;
    int m_Emitter;
    /// Intrinsic collector node (`internal_collector` if \f$R_C > 0\f$, else `m_Collector`).
    int m_C;
    /// Intrinsic base node (`internal_base` if \f$R_B > 0\f$, else `m_Base`).
    int m_B;
    /// Intrinsic emitter node (`internal_emitter` if \f$R_E > 0\f$, else `m_Emitter`).
    int m_E;
    /// The parameters as the model gives them, at the nominal temperature.
    BjtParameters m_Given;
    /// The parameters at the temperature of the run, which every evaluation reads.
    BjtParameters m_Parameters;
    ThermalConditions m_Thermal;
    bool m_Pnp;
    /// Potential of both junctions at the temperature of the run, since they share the default \f$V_{JE} = V_{JC}\f$.
    double m_JunctionPotential;
    double m_Vt;
    double m_Critical;
    /// Trapezoidal charge histories of the BE and BC junctions.
    ChargeHistory m_HistoryBe, m_SavedHistoryBe;
    ChargeHistory m_HistoryBc, m_SavedHistoryBc;
    double m_Gqbe = 0.0, m_Ieqbe = 0.0, m_Gqbc = 0.0, m_Ieqbc = 0.0;

    void ApplyTemperature(const ThermalConditions &thermal);
    Intrinsic Evaluate(double vbe, double vbc) const;
    JunctionState EvaluateState(const std::vector<double> &x) const;
    JunctionCharges EvaluateCharges(const JunctionState &state) const;
};

} // namespace Nodalys

#endif // NODALYS_BJT_DEVICE_H
