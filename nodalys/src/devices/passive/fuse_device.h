/**
 * @file    fuse_device.h
 * @brief   Fuse of Nodalys: a resistance that heats with the current squared, opens for good and stays open.
 */

#ifndef NODALYS_FUSE_DEVICE_H
#define NODALYS_FUSE_DEVICE_H

#include "devices/device.h"
#include "nodalys/types.h"

namespace Nodalys {

/**
 * @class   FuseDevice
 * @brief   A two-terminal fuse that blows by I squared t, stays blown for the rest of the run and cools between
 *          overloads.
 * @details The element has a thermal state, the heat h in units of what the rated current settles at:
 *          tau dh/dt = i^2 / Ih^2 - h, with tau = I2t / Ih^2. While h stays below `BLOW_HEAT` the resistance is the
 *          cold one. When h reaches it, the fuse opens: its resistance goes from the cold to the blown value, along
 *          a smooth curve over a short time, and never comes back. A transient starts whole, and so do the operating
 *          point and the small-signal analyses, which see the cold resistance. Like the ngspice model, it adds no
 * noise.
 *
 *          The resistance is a function of the time of the step and of the state at its start, so each step is a
 *          linear problem for Newton. The heat advances once the step has converged, by the exact solution of the
 *          equation over the step with the mean of the powers at its two ends.
 */
class FuseDevice : public Device {
public:
    FuseDevice(int node_a, int node_b, FuseParameters parameters);

    void Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) override;
    double TerminalCurrent(int terminal, const std::vector<double> &x) const override;
    std::complex<double> AcTerminalCurrent(int terminal,
                                           const std::vector<double> &x_dc,
                                           const std::vector<std::complex<double>> &x_ac,
                                           double omega) const override;
    void AcceptStep(const std::vector<double> &x) override;
    void RejectStep() override;
    bool HasSwitched() const override;
    double NextBreakpoint(double time) const override;
    [[nodiscard]] std::unique_ptr<Device> Clone() const override;

private:
    /// The state of the element at one instant.
    struct State {
        double Heat = 0.0; ///< Heat of the element, where 1 is what the rated current settles at.
        /// Heat that the current of the instant settles at: the current squared over the rated current squared.
        double Power = 0.0;
        double Time = 0.0; ///< Instant of the state, in seconds.
        bool IsBlown = false;
        double BlowTime = 0.0; ///< Instant the heat reached the blow level, in seconds; meaningful only when blown.
    };

    int m_NodeA;
    int m_NodeB;
    FuseParameters m_Parameters;
    double m_CoolingTimeConstant; ///< Time constant of the heat, I2t over the rated current squared.
    State m_Accepted;             ///< State at the last accepted point.
    State m_Pending;              ///< State the step being solved ends in, worked out by each stamp of it.
    double m_Resistance;          ///< Resistance of the last stamp, which the currents of the results divide by.
    /// True while the stamps are those of a transient step, whose end the accepted state moves to.
    bool m_IsStep = false;

    double ResistanceAt(const State &state, double time) const;
    double PowerOf(const std::vector<double> &x, double resistance) const;
    State Advance(const State &state, double time, double end_power) const;
};

} // namespace Nodalys

#endif // NODALYS_FUSE_DEVICE_H
