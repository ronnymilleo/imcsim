/**
 * @file    mosfet_device.h
 * @brief   Level-1 MOSFET of Nodalys, the SPICE Shichman-Hodges model.
 */

#ifndef NODALYS_MOSFET_DEVICE_H
#define NODALYS_MOSFET_DEVICE_H

#include "devices/device.h"
#include "nodalys/types.h"

namespace Nodalys {

/**
 * @class   MosfetDevice
 * @brief   An N-channel or P-channel MOSFET using the SPICE level-1 (Shichman-Hodges) model with symmetric
 *          source/drain reversal for \f$V_{DS} < 0\f$, body-drain diode, and gate-overlap capacitances.
 * @details
 * Terminal indices: `0 = Drain`, `1 = Gate`, `2 = Source`.
 */
class MosfetDevice : public Device {
public:
    MosfetDevice(int drain,
                 int gate,
                 int source,
                 int internal_drain,
                 int internal_source,
                 MosfetParameters parameters,
                 bool pmos);

    void Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) override;
    void StampAc(MnaSystem &reactive, const std::vector<double> &x_dc) const override;

    bool IsLinear() const override { return false; }

    void StampCharge(MnaSystem &sys, const std::vector<double> &x) const override;
    bool Limit(std::vector<double> &x_new, const std::vector<double> &x_old) const override;
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
    struct Channel {
        /// Magnitude of channel current (\f$\ge 0\f$) in the effective drain-to-source frame.
        double Current;
        /// Transconductance \f$\partial I / \partial v_{gs,\text{eff}} \ge 0\f$.
        double Gm;
        /// Output conductance \f$\partial I / \partial v_{ds,\text{eff}} \ge 0\f$.
        double Gds;
    };

    /// The channel in the frame where the drain sits at the higher potential, after the source and drain swap.
    struct ChannelState {
        /// Node acting as the drain; the intrinsic source when the channel is reversed.
        int Drain;
        /// Node acting as the source; the intrinsic drain when the channel is reversed.
        int Source;
        /// Whether the source and drain swapped roles because \f$v_{ds}\f$ has the wrong sign.
        bool IsReversed;
        /// \f$+1\f$ for NMOS and \f$-1\f$ for PMOS.
        double Polarity;
        /// Current and conductances in the effective frame.
        Channel Conduction;
    };

    /// The two terminals of the body-drain diode, in the direction it conducts.
    struct BodyDiodeNodes {
        int Anode;
        int Cathode;
    };

    int m_Drain;
    int m_Gate;
    int m_Source;
    /// Intrinsic drain node (`internal_drain` if \f$R_D > 0\f$, else `m_Drain`).
    int m_D;
    /// Intrinsic source node (`internal_source` if \f$R_S > 0\f$, else `m_Source`).
    int m_S;
    /// The parameters as the model gives them, at the nominal temperature.
    MosfetParameters m_Given;
    /// The parameters at the temperature of the run, which every evaluation reads.
    MosfetParameters m_Parameters;
    ThermalConditions m_Thermal;
    bool m_Pmos;
    /// \f$\text{KP} \cdot (W / L)\f$.
    double m_Beta;
    double m_Vt;
    /// Gate-source (\f$C_{GSO} \cdot W\f$) and gate-drain (\f$C_{GDO} \cdot W\f$) linear overlap capacitances.
    double m_Cgs;
    double m_Cgd;
    ChargeHistory m_HistoryGs, m_SavedHistoryGs;
    ChargeHistory m_HistoryGd, m_SavedHistoryGd;
    double m_Gcgs = 0.0, m_Ieqgs = 0.0, m_Gcgd = 0.0, m_Ieqgd = 0.0;
    /// `PNJLIM` critical voltage of the body-drain junction diode.
    double m_Critical;

    void ApplyTemperature(const ThermalConditions &thermal);
    Channel Evaluate(double vgs_eff, double vds_eff) const;
    ChannelState EvaluateChannel(const std::vector<double> &x) const;
    BodyDiodeNodes GetBodyDiodeNodes() const;
};

} // namespace Nodalys

#endif // NODALYS_MOSFET_DEVICE_H
