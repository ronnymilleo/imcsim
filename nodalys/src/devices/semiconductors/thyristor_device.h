/**
 * @file    thyristor_device.h
 * @brief   Thyristor (SCR), TRIAC and GTO of Nodalys: a latching switch fired by gate current and held on by its main
 *          current.
 */

#ifndef NODALYS_THYRISTOR_DEVICE_H
#define NODALYS_THYRISTOR_DEVICE_H

#include "devices/device.h"
#include "nodalys/types.h"

namespace Nodalys {

/**
 * @class   ThyristorDevice
 * @brief   A thyristor (SCR), TRIAC or GTO between its two main terminals and the gate: it blocks until a gate pulse
 *          fires it, then latches on until the main current falls below the holding current with the gate removed.
 * @details The state follows the iterate of the current Newton solve and keeps itself between the firing and the
 *          release, so the latch provides the hysteresis: once released, the device fires again only on a new gate
 *          pulse or on breakover. The on-state is the datasheet line \f$V_{TM} = V_0 + R_T I_T\f$, stamped as
 *          a conductance \f$1/R_T\f$ with an equivalent current source; the off-state is \f$1/R_{off}\f$. The gate
 *          draws \f$(V_{G} - V_{GT})/R_G\f$ above its threshold and fires the device at \f$I_{GT}\f$.
 *
 *          Two second-order effects are optional. A blocking device whose main-terminal voltage rises faster than
 *          `ThyristorParameters::CriticalDvDt` fires without a gate, as the displacement current through the junction
 *          capacitance fires a real one; the slope is read from the last accepted time point to the iterate. After a
 *          release, the device recovers for `ThyristorParameters::TurnOffTime`: while that window runs, a forward
 *          voltage that rises back past the on-state threshold re-fires it, which is the commutation failure of a
 *          forced-commutated circuit; a voltage already above the threshold when the device releases does not, so
 *          the natural commutation of a rectifier still lands.
 *
 *          With `ThyristorParameters::IsBidirectional` the device is a TRIAC: the on-state line and the gate junction
 *          mirror about zero, so it conducts and blocks both ways and the gate fires it in all four quadrants, while
 *          the holding current, the breakover voltage, the critical dv/dt and the recovery apply to the magnitudes.
 *          The main current then carries the sign of the main-terminal voltage: between \f$-V_0\f$ and \f$+V_0\f$
 *          a conducting TRIAC carries only the off-state current, so its line is continuous and has one solution
 *          for every current, which keeps Newton off a reversed one when the voltage crosses zero.
 *
 *          With `ThyristorParameters::GateTurnOffCurrent` above zero the device is a GTO: a one-way main branch like
 *          the SCR's, with a gate junction that mirrors about zero as the TRIAC's does. A conducting GTO whose gate
 *          draws a negative current of magnitude at least \f$I_{GQ}\f$ releases whatever its main current, which
 *          the holding current alone cannot do; a negative gate never fires it. The prediction of the next breakpoint
 *          follows the gate current toward \f$-I_{GQ}\f$ as it follows the main current toward \f$I_H\f$.
 *
 *          Terminal indices: `0 = Anode (MT2)`, `1 = Cathode (MT1)`, `2 = Gate`. The gate is referenced to the
 *          cathode (MT1), through which its current returns.
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
    /// One accepted time point of the main branch: the current the commutation prediction extrapolates from, and
    /// the voltage the dv/dt firing and the recovery re-fire read.
    struct AcceptedPoint {
        double Time = 0.0;        ///< Instant of the point, in seconds.
        double Current = 0.0;     ///< Main current at the point, in Amperes.
        double Voltage = 0.0;     ///< Main-terminal voltage at the point, in Volts.
        double GateCurrent = 0.0; ///< Gate current at the point, in Amperes; the GTO turn-off prediction reads it.
    };

    /// The straight piece of the main branch a voltage falls on, as the current \f$i = G v + I_0\f$ it carries.
    struct BranchLine {
        double Conductance = 0.0; ///< Slope G of the piece, in Siemens.
        double Offset = 0.0;      ///< Current \f$I_0\f$ of the piece at zero volts, in Amperes.
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
    bool m_HasHistory = false;   ///< True once two transient points are behind, which the prediction needs.
    double m_RecoveryEnds = 0.0; ///< End of the turn-off recovery window; the step time below it can re-fire.
    double m_SavedRecoveryEnds = 0.0;
    /// True while the voltage slope of the step has reached the critical dv/dt; it then fires and sustains
    /// conduction for the rest of the step, as the displacement current does while the voltage keeps moving.
    bool m_IsDvDtFired = false;
    bool m_SavedIsDvDtFired = false;

    BranchLine OnStateLine(double voltage) const;
    bool IsGateMirrored() const;
    bool IsGateTurnOff(const std::vector<double> &x) const;
    double PredictHoldingCurrentCrossing() const;
    double PredictGateTurnOffCrossing() const;
    double MainCurrent(const std::vector<double> &x) const;
    double GateCurrent(const std::vector<double> &x) const;
    double Magnitude(double value) const;
    double VoltageSlope(double voltage) const;
    bool IsGateOn(const std::vector<double> &x) const;
    bool IsPastCriticalDvDt(double vak) const;
    bool ShouldFire(const std::vector<double> &x, bool is_recovering) const;
    bool ShouldRelease(const std::vector<double> &x, bool is_recovering) const;
    void UpdateState(const std::vector<double> &x);
    void StampBranch(MnaSystem &sys, const std::vector<double> &x) const;
    void StampGate(MnaSystem &sys, const std::vector<double> &x) const;
};

} // namespace Nodalys

#endif // NODALYS_THYRISTOR_DEVICE_H
