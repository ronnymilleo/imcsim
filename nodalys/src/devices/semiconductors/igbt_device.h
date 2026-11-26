/**
 * @file    igbt_device.h
 * @brief   IGBT of Nodalys: the conduction of a MOS-gated bipolar switch from its datasheet figures, with the tail
 *          current of its turn-off.
 */

#ifndef NODALYS_IGBT_DEVICE_H
#define NODALYS_IGBT_DEVICE_H

#include "devices/device.h"
#include "nodalys/types.h"

namespace Nodalys {

/**
 * @class   IgbtDevice
 * @brief   The collector-emitter conduction of an N-channel IGBT: the smaller of the channel current the gate allows
 *          and the current of the on-state line, plus the tail current of a turn-off and the off-state leakage.
 * @details Terminal indices: `0 = Collector`, `1 = Gate`, `2 = Emitter`. The gate draws no current here: the compiler
 *          adds the capacitances of the datasheet as capacitors around the device, and the co-packaged diode as a
 *          diode. The two limits are blended smoothly, so the current and its slopes are continuous, which Newton and
 *          the harmonic balance need. The tail is a first-order lag on the conduction current: a stored current
 *          follows the conduction up at once and decays toward it with the tail time constant, and while it is above
 *          the conduction a fraction of the difference keeps flowing, only with the collector above the emitter. The
 *          lag only runs in a transient; every other analysis sees the static conduction.
 */
class IgbtDevice : public Device, public NonlinearDevice, public TransientDevice {
public:
    IgbtDevice(int collector, int gate, int emitter, IgbtParameters parameters);

    void Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) override;
    double TerminalCurrent(int terminal, const std::vector<double> &x) const override;
    std::complex<double> AcTerminalCurrent(int terminal,
                                           const std::vector<double> &x_dc,
                                           const std::vector<std::complex<double>> &x_ac,
                                           double omega) const override;
    bool Limit(std::vector<double> &x_new, const std::vector<double> &x_old) const override;
    void AcceptStep(const std::vector<double> &x) override;

    [[nodiscard]] DeviceRoles GetRoles() override { return {.Nonlinear = this, .Transient = this}; }

    [[nodiscard]] std::unique_ptr<Device> Clone() const override;

private:
    /// Collector current and its slopes at one bias, positive from the collector to the emitter.
    struct Conduction {
        double Current;          ///< Collector current, in Amperes.
        double OutputSlope;      ///< \f$\partial I_C / \partial V_{CE}\f$, in Siemens.
        double Transconductance; ///< \f$\partial I_C / \partial V_{GE}\f$, in Siemens.
    };

    int m_Collector;
    int m_Gate;
    int m_Emitter;
    IgbtParameters m_Parameters;
    double m_ChannelGain;         ///< \f$K = g_{fs}^2 / (2 I_{gfs})\f$, in Amperes per square Volt.
    double m_StoredCurrent = 0.0; ///< Stored current at the last accepted point, in Amperes.
    double m_StepStart = 0.0;     ///< Stored current the step being solved starts from, in Amperes.
    double m_StepDecay = 0.0;     ///< \f$e^{-h/\tau}\f$ of the step being solved; zero outside a transient.

    Conduction EvaluateStatic(double vce, double vge) const;
    Conduction Evaluate(const std::vector<double> &x) const;
};

} // namespace Nodalys

#endif // NODALYS_IGBT_DEVICE_H
