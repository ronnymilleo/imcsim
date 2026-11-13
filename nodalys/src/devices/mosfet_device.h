/**
 * @file    mosfet_device.h
 * @brief   Level-1 MOSFET of Nodalys, the SPICE Shichman-Hodges model.
 */

#ifndef NODALYS_MOSFET_DEVICE_H
#define NODALYS_MOSFET_DEVICE_H

#include "devices/devices.h"
#include "nodalys/types.h"

namespace Nodalys {

/**
 * @class   MosfetDevice
 * @brief   An N-channel or P-channel MOSFET using the SPICE level-1 (Shichman-Hodges) model with symmetric
 *          source/drain reversal for $V_{DS} < 0$, body-drain diode, and gate-overlap capacitances.
 * @details
 * Terminal indices: `0 = Drain`, `1 = Gate`, `2 = Source`.
 */
class MosfetDevice : public Device {
public:
    MosfetDevice(int drain, int gate, int source, int internal_drain, int internal_source, MosfetParameters parameters,
                 bool pmos);

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
    MosfetParameters m_Parameters;
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

} // namespace Nodalys

#endif // NODALYS_MOSFET_DEVICE_H
