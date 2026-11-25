/**
 * @file    thyristor_device.h
 * @brief   Thyristor (SCR) of Nodalys: a latching switch fired by gate current and held on by its anode current.
 */

#ifndef NODALYS_THYRISTOR_DEVICE_H
#define NODALYS_THYRISTOR_DEVICE_H

#include "devices/device.h"
#include "nodalys/types.h"

namespace Nodalys {

/**
 * @class   ThyristorDevice
 * @brief   A thyristor (SCR) between anode, cathode and gate: it blocks forward voltage until a gate pulse fires it,
 *          then latches on until the anode current falls below the holding current with the gate removed.
 * @details The state follows the iterate of the current Newton solve and keeps itself between the firing and the
 *          release, so the latch provides the hysteresis: once released, the device fires again only on a new gate
 *          pulse or on forward breakover. The on-state is the datasheet line \f$V_{TM} = V_0 + R_T I_T\f$, stamped as
 *          a conductance \f$1/R_T\f$ with an equivalent current source; the off-state is \f$1/R_{off}\f$. The gate
 *          draws \f$(V_{GK} - V_{GT})/R_G\f$ above its threshold and fires the device at \f$I_{GT}\f$.
 *
 *          Terminal indices: `0 = Anode`, `1 = Cathode`, `2 = Gate`.
 */
class ThyristorDevice : public Device, public NonlinearDevice, public TransientDevice {
public:
    ThyristorDevice(int anode, int cathode, int gate, ThyristorParameters parameters);

    void Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) override;

    bool SupportsHarmonicBalance() const override { return false; }

    double TerminalCurrent(int terminal, const std::vector<double> &x) const override;
    std::complex<double> AcTerminalCurrent(int terminal,
                                           const std::vector<double> &x_dc,
                                           const std::vector<std::complex<double>> &x_ac,
                                           double omega) const override;
    void AcceptStep(const std::vector<double> &x) override;
    void RejectStep() override;
    bool HasSwitched() const override;
    double NextBreakpoint(double time) const override;

    [[nodiscard]] DeviceRoles GetRoles() override { return {.Nonlinear = this, .Transient = this}; }

    [[nodiscard]] std::unique_ptr<Device> Clone() const override;

private:
    /// One accepted time point of the anode current, which the commutation prediction extrapolates from.
    struct AcceptedPoint {
        double Time = 0.0;    ///< Instant of the point, in seconds.
        double Current = 0.0; ///< Anode current at the point, in Amperes.
    };

    int m_Anode;
    int m_Cathode;
    int m_Gate;
    ThyristorParameters m_Parameters;
    bool m_IsConducting = false;
    bool m_SavedIsConducting = false;
    /// True while the stamps are those of a transient step, whose end the accepted history moves to.
    bool m_IsStep = false;
    double m_StampTime = 0.0; ///< Instant of the step being stamped, which AcceptStep() records.
    AcceptedPoint m_Accepted;
    AcceptedPoint m_Previous;
    bool m_HasHistory = false; ///< True once two transient points are behind, which the prediction needs.

    double AnodeCurrent(const std::vector<double> &x) const;
    double GateCurrent(const std::vector<double> &x) const;
    void UpdateState(const std::vector<double> &x);
    void StampBranch(MnaSystem &sys) const;
    void StampGate(MnaSystem &sys, const std::vector<double> &x) const;
};

} // namespace Nodalys

#endif // NODALYS_THYRISTOR_DEVICE_H
