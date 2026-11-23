/**
 * @file    device.h
 * @brief   Abstract base interface for MNA circuit elements in Nodalys.
 * @details The stamp conventions follow the modified nodal analysis of Vlach & Singhal, "Computer Methods for Circuit
 *          Analysis and Design", and the companion models of Chua & Lin, "Computer-Aided Analysis of Electronic
 *          Circuits". Nonlinear devices linearize at the current Newton iterate: they stamp their differential
 *          conductance and an equivalent current source, so the assembled system stays linear at every iteration.
 */

#ifndef NODALYS_DEVICE_H
#define NODALYS_DEVICE_H

#include "devices/integration.h"
#include "devices/thermal.h"
#include "mna/mna_system.h"
#include "nodalys/types.h"
#include <complex>
#include <cstdint>
#include <limits>
#include <memory>
#include <string_view>
#include <vector>

namespace Nodalys {

class IndependentSourceDevice;
class SensitiveDevice;

/**
 * @class   Device
 * @brief   One element between the nodes of the compiled circuit, with the physics of its kind.
 * @details A device knows the unknown indices of its nodes and branches, set by the compiler. The results ask for
 *          the current entering a terminal, which the devices report with the sign convention of the SPICE results:
 *          positive into the probed terminal, and for a two-terminal part positive from its first terminal to its
 *          second.
 */
class Device {
public:
    /**
     * @brief   Destroys the device instance.
     */
    virtual ~Device() = default;

    /**
     * @brief   Adds this device's contribution to the system, linearized at x when it is nonlinear.
     * @param[in,out] sys   System to stamp into.
     * @param[in]     x     Current iterate, or the converged solution while a transient commits a time point.
     * @param[in]     time  What the stamp is for: DC, or a transient point at Time with step TimeStep.
     */
    virtual void Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) = 0;

    /**
     * @brief   Stamps this device's small-signal reactive coefficients (\f$C\f$ or \f$-L\f$, multiplied by
     * \f$j\omega\f$ during an AC sweep) into `reactive.AddA()` and its AC source excitation into `reactive.AddRhs()`.
     * @param[in,out] reactive  System holding the imaginary-part coefficients \f$C_{\text{AC}}\f$ and AC excitation
     *                          \f$b_{\text{AC}}\f$.
     * @param[in]     x_dc      Converged DC operating point around which the circuit is linearized.
     */
    virtual void StampAc(MnaSystem &reactive, const std::vector<double> &x_dc) const {
        (void)reactive;
        (void)x_dc;
    }

    /**
     * @brief   Limits the junction voltages of the candidate solution after a Newton solve (pn-junction limiting).
     * @param[in,out] x_new  Candidate solution of this iteration.
     * @param[in]     x_old  Solution of the previous iteration.
     * @return  True when x_new changed.
     * @note    Kundert, "The Designer's Guide to SPICE and Spectre", section 4.4: limiting the change across a
     *          junction keeps the exponential out of the overflow range and damps Newton oscillations.
     */
    virtual bool Limit(std::vector<double> &x_new, const std::vector<double> &x_old) const {
        (void)x_new;
        (void)x_old;
        return false;
    }

    /**
     * @brief   Computes the current entering the device at the given terminal.
     * @param[in] terminal  Zero-based terminal index in the device's pin order.
     * @param[in] x         Solution vector at which to evaluate the current.
     * @return  Current entering the terminal in Amperes.
     */
    virtual double TerminalCurrent(int terminal, const std::vector<double> &x) const = 0;

    /**
     * @brief   Computes the small-signal complex current entering the device at the given terminal.
     * @param[in] terminal  Zero-based terminal index in the device's pin order.
     * @param[in] x_dc      Converged DC operating point vector.
     * @param[in] x_ac      Small-signal complex phasor solution vector.
     * @param[in] omega     Angular frequency \f$\omega = 2\pi f\f$ in rad/s.
     * @return  Complex current phasor entering the terminal in Amperes.
     */
    virtual std::complex<double> AcTerminalCurrent(int terminal,
                                                   const std::vector<double> &x_dc,
                                                   const std::vector<std::complex<double>> &x_ac,
                                                   double omega) const = 0;

    /**
     * @brief   Computes the output voltage noise power spectral density (\f$\text{V}^2/\text{Hz}\f$) contributed by
     *          this device.
     * @param[in] x_dc          Converged DC operating point vector.
     * @param[in] adjoint       Adjoint vector solving \f$Y(j\omega)^T \cdot \lambda = e_{\text{out}}\f$.
     * @param[in] frequency_hz  Evaluation frequency in Hertz.
     * @return  Output noise variance density in \f$\text{V}^2/\text{Hz}\f$.
     */
    virtual double OutputNoiseVariance(const std::vector<double> &x_dc,
                                       const std::vector<std::complex<double>> &adjoint,
                                       double frequency_hz) const {
        (void)x_dc;
        (void)adjoint;
        (void)frequency_hz;
        return 0.0;
    }

    /**
     * @brief   Reports whether the device is linear and time-invariant.
     * @details Nonlinear devices evaluate via alternating frequency-time (AFT) in Harmonic Balance.
     * @return  True when the device is linear and time-invariant.
     */
    virtual bool IsLinear() const { return true; }

    /**
     * @brief   Reports whether the device can be simulated by Harmonic Balance.
     * @return  False when the device cannot be simulated by Harmonic Balance (e.g., hysteretic switches).
     */
    virtual bool SupportsHarmonicBalance() const { return true; }

    /**
     * @brief   Stamps this nonlinear device's charge-voltage differential capacitances into `sys.AddA()` and
     *          linearized equivalent charge into `sys.AddRhs()` at state `x`, for Harmonic Balance AFT.
     * @param[in,out] sys  System holding \f$\mathbf{C}_{\text{nl}}(x)\f$ and \f$-\mathbf{q}_{\text{eq}}(x)\f$.
     * @param[in]     x    Time-sample iterate vector.
     */
    virtual void StampCharge(MnaSystem &sys, const std::vector<double> &x) const {
        (void)sys;
        (void)x;
    }

    /**
     * @brief   Computes the charge associated with the given terminal at state `x`, for Harmonic Balance current
     *          phasors.
     * @param[in] terminal  Zero-based terminal index in the device's pin order.
     * @param[in] x         State vector at which to evaluate the charge.
     * @return  Terminal charge in Coulombs.
     */
    virtual double TerminalCharge(int terminal, const std::vector<double> &x) const {
        (void)terminal;
        (void)x;
        return 0.0;
    }

    /**
     * @brief   Reports whether the device carries state between transient time points.
     * @return  True for devices with companion-model history (capacitors, inductors, junction charges).
     */
    virtual bool HasTransientState() const { return false; }

    /**
     * @brief   Saves the state of the accepted point `x` after each converged transient step.
     * @param[in] x  Converged solution vector of the accepted time point.
     */
    virtual void AcceptStep(const std::vector<double> &x) { (void)x; }

    /**
     * @brief   Restores the state saved before a step that the LTE control rejected.
     */
    virtual void RejectStep() {}

    /**
     * @brief   Tells whether the discrete state of the device changed on the step just solved.
     * @details A switch that opens or closes inside a step turns the slope of every stiff mode around it: the step
     *          that follows must restart with Backward Euler, which damps those modes, as it does after a corner.
     * @return  True when the state differs from the last accepted point; call before AcceptStep().
     */
    virtual bool HasSwitched() const { return false; }

    /**
     * @brief   Finds the next instant after `time` where the waveform of the device has a corner.
     * @details The transient lands a time point on every corner, so an edge narrower than the time step is never
     *          stepped over, and the step that leaves a corner starts small and uses Backward Euler.
     * @note    The transient asks again after every accepted point, so a device whose corners depend on its state, such
     *          as a fuse that predicts the instant it blows, may move them as the run goes.
     * @param[in] time  Instant already reached.
     * @return  The next corner, strictly after `time`, or infinity when the device has none.
     */
    virtual double NextBreakpoint(double time) const {
        (void)time;
        return std::numeric_limits<double>::infinity();
    }

    /**
     * @brief   Scales the independent sources of the device, for the source-stepping fallback of Newton-Raphson.
     * @param[in] scale  Share of the source values stamped; devices that are not sources ignore it.
     */
    virtual void SetSourceScale(double scale) { (void)scale; }

    /**
     * @brief   Tells why the last stamp of the device cannot be trusted, so a Newton iteration that stamped it is not
     *          taken as converged.
     * @details A behavioral source whose expression has no value at the iterate, such as the square root of a negative
     *          voltage, stamps nothing useful: the solve keeps iterating, and when it fails the message says why.
     * @return  The reason, or an empty text when the last stamp was good.
     */
    virtual std::string_view GetFault() const { return {}; }

    /**
     * @brief   Seeds the initial iterate of a DC operating point solve for nonlinear junctions.
     * @param[in,out] x  The iterate to seed; devices that need an initial bias guess update it.
     */
    virtual void SeedOperatingPoint(std::vector<double> &x) const { (void)x; }

    /**
     * @brief   Moves the device to the temperature of the run, so its models and its thermal noise follow it.
     * @details Every analysis calls it before it solves. A device that does not depend on the temperature ignores it.
     * @param[in] thermal  Temperature of the run and the nominal temperature its parameters are given at.
     */
    virtual void SetTemperature(const ThermalConditions &thermal) { (void)thermal; }

    /**
     * @brief   Sets the random seed for transient noise or random waveform sources.
     * @param[in] seed  Independent 64-bit stream seed for this device.
     */
    virtual void SetRandomSeed(std::uint64_t seed) { (void)seed; }

    /**
     * @brief   Returns the sensitivity capability when this device supports analytical DC/AC sensitivity.
     * @return  Pointer to the SensitiveDevice interface, or nullptr when unsupported.
     */
    [[nodiscard]] virtual const SensitiveDevice *AsSensitive() const { return nullptr; }

    /**
     * @brief   Returns this device as a mutable IndependentSourceDevice when it is one.
     * @return  Pointer to the IndependentSourceDevice view, or nullptr.
     */
    [[nodiscard]] virtual IndependentSourceDevice *AsIndependentSource() { return nullptr; }

    /**
     * @brief   Returns this device as a const IndependentSourceDevice when it is one.
     * @return  Pointer to the const IndependentSourceDevice view, or nullptr.
     */
    [[nodiscard]] virtual const IndependentSourceDevice *AsIndependentSource() const { return nullptr; }

    /**
     * @brief   Applies an interactive live parameter change during transient simulation.
     * @param[in] quantity  Parameter kind to update.
     * @param[in] value     New parameter value in SI units.
     * @return  True if this device supports changing `quantity`.
     */
    virtual bool ApplyLiveChange(LiveQuantity quantity, double value) {
        (void)quantity;
        (void)value;
        return false;
    }

    /**
     * @brief   Creates an independent copy of this device for an isolated simulation run.
     * @details Stateful waveform generators (such as transient noise and random sources) must allocate fresh internal
     *          state in the clone so concurrent runs on the same compiled circuit share no mutable state.
     * @return  A newly owned copy of this device.
     */
    [[nodiscard]] virtual std::unique_ptr<Device> Clone() const = 0;
};

} // namespace Nodalys

#endif // NODALYS_DEVICE_H
