/**
 * @file    devices.h
 * @brief   The devices Nodalys stamps into the MNA system: one class per element kind, each computing the
 *          currents and the linearized companion model of its physics.
 * @details The stamp conventions follow the modified nodal analysis of Vlach & Singhal, "Computer Methods for Circuit
 *          Analysis and Design", and the companion models of Chua & Lin, "Computer-Aided Analysis of Electronic
 *          Circuits". Nonlinear devices linearize at the current Newton iterate: they stamp their differential
 *          conductance and an equivalent current source, so the assembled system stays linear at every iteration.
 */

#ifndef NODALYS_DEVICES_H
#define NODALYS_DEVICES_H

#include "devices/integration.h"
#include "devices/thermal.h"
#include "mna/mna_system.h"
#include "nodalys/types.h"
#include <complex>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace Nodalys {

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
    virtual ~Device() = default;

    /**
     * @brief   Adds this device's contribution to the system, linearized at x when it is nonlinear.
     * @param[in,out] sys   System to stamp into.
     * @param[in]     x     Current iterate, or the converged solution while a transient commits a time point.
     * @param[in]     time  What the stamp is for: DC, or a transient point at Time with step TimeStep.
     */
    virtual void Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) = 0;

    /**
     * @brief   Stamps this device's small-signal reactive coefficients ($C$ or $-L$, multiplied by $j\omega$ during an
     *          AC sweep) into `reactive.AddA()` and its AC source excitation into `reactive.AddRhs()`.
     * @param[in,out] reactive  System holding the imaginary-part coefficients $C_{\text{AC}}$ and AC excitation
     * $b_{\text{AC}}$.
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

    /// Current entering the device at the given terminal, evaluated at x.
    virtual double TerminalCurrent(int terminal, const std::vector<double> &x) const = 0;

    /// Small-signal complex current entering the device at the given terminal at angular frequency `omega`.
    virtual std::complex<double> AcTerminalCurrent(int terminal, const std::vector<double> &x_dc,
                                                   const std::vector<std::complex<double>> &x_ac,
                                                   double omega) const = 0;

    /**
     * @brief   Output voltage noise power spectral density ($\text{V}^2/\text{Hz}$) contributed by this device at the
     *          operating point `x_dc` and frequency `frequency_hz`, given the adjoint vector `adjoint` (solution of
     *          $Y(j\omega)^T \cdot \lambda = e_{\text{out}}$).
     */
    virtual double OutputNoiseVariance(const std::vector<double> &x_dc,
                                       const std::vector<std::complex<double>> &adjoint, double frequency_hz) const {
        (void)x_dc;
        (void)adjoint;
        (void)frequency_hz;
        return 0.0;
    }

    /// True when the device is linear and time-invariant; nonlinear devices evaluate via AFT in Harmonic Balance.
    virtual bool IsLinear() const { return true; }

    /// False when the device cannot be simulated by Harmonic Balance (e.g., hysteretic switches).
    virtual bool SupportsHarmonicBalance() const { return true; }

    /**
     * @brief   Stamps this nonlinear device's charge-voltage differential capacitances into `sys.AddA()` and
     *          linearized equivalent charge into `sys.AddRhs()` at state `x`, for Harmonic Balance AFT.
     * @param[in,out] sys  System holding $\mathbf{C}_{\text{nl}}(x)$ and $-\mathbf{q}_{\text{eq}}(x)$.
     * @param[in]     x    Time-sample iterate vector.
     */
    virtual void StampCharge(MnaSystem &sys, const std::vector<double> &x) const {
        (void)sys;
        (void)x;
    }

    /// Charge associated with the given terminal at state `x`, for Harmonic Balance current phasors.
    virtual double TerminalCharge(int terminal, const std::vector<double> &x) const {
        (void)terminal;
        (void)x;
        return 0.0;
    }

    /// True when the device carries state between time points (companion models of C, L and junction charges).
    virtual bool HasTransientState() const { return false; }
    /// Saves the state of the accepted point x (called after each converged transient step).
    virtual void AcceptStep(const std::vector<double> &x) { (void)x; }
    /// Restores the state saved before a step that the LTE control rejected.
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
};

/**
 * @class   ResistorDevice
 * @brief   A resistor between two nodes; also the model of a closed or open contact.
 */
class ResistorDevice : public Device {
public:
    ResistorDevice(int node_a, int node_b, double resistance, bool noisy = true);

    double GetResistance() const;
    void SetResistance(double resistance);
    int GetNodeA() const { return m_NodeA; }
    int GetNodeB() const { return m_NodeB; }

    void Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) override;
    double TerminalCurrent(int terminal, const std::vector<double> &x) const override;
    std::complex<double> AcTerminalCurrent(int terminal, const std::vector<double> &x_dc,
                                           const std::vector<std::complex<double>> &x_ac, double omega) const override;
    double OutputNoiseVariance(const std::vector<double> &x_dc, const std::vector<std::complex<double>> &adjoint,
                               double frequency_hz) const override;
    void SetTemperature(const ThermalConditions &thermal) override;

private:
    int m_NodeA;
    int m_NodeB;
    double m_Conductance;
    bool m_Noisy;
    /// $4kT$ of the thermal noise; a resistor keeps its value at any temperature, as no temperature coefficient is set.
    double m_FourKt = FOUR_KT_NOMINAL;
};

/**
 * @class   CapacitorDevice
 * @brief   A capacitor between two nodes with a companion model of the chosen integration method in a transient.
 * @details With the trapezoidal rule, as in Vlach & Singhal, chapter on numerical integration of differential
 *          equations: i(t+h) = (2C/h)(v(t+h) − v(t)) − i(t); see `integration.h` for the other formulas. At the
 *          operating point the capacitor is an open circuit. With an initial condition and uic, the first point starts
 * from it.
 */
class CapacitorDevice : public Device {
public:
    CapacitorDevice(int node_a, int node_b, double capacitance, double initial_voltage, bool has_initial);

    int GetNodeA() const { return m_NodeA; }
    int GetNodeB() const { return m_NodeB; }
    double GetCapacitance() const { return m_Capacitance; }

    void Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) override;
    void StampAc(MnaSystem &reactive, const std::vector<double> &x_dc) const override;
    double TerminalCurrent(int terminal, const std::vector<double> &x) const override;
    std::complex<double> AcTerminalCurrent(int terminal, const std::vector<double> &x_dc,
                                           const std::vector<std::complex<double>> &x_ac, double omega) const override;
    bool HasTransientState() const override;
    void AcceptStep(const std::vector<double> &x) override;
    void RejectStep() override;

private:
    int m_NodeA;
    int m_NodeB;
    double m_Capacitance;
    /// Charge C v and its current at the last accepted points.
    ChargeHistory m_History;
    ChargeHistory m_SavedHistory;
    double m_InitialVoltage;
    bool m_HasInitial;
    /// Companion model of the last stamp: conductance and current source, for the results.
    double m_LastConductance = 0.0;
    double m_EquivalentCurrent = 0.0;
};

/**
 * @class   InductorDevice
 * @brief   An inductor as a branch between two nodes with a companion model of the chosen integration method.
 * @details The branch unknown carries the inductor current, so at the operating point the inductor is an exact
 *          short (v = 0). In a transient with the trapezoidal rule, v(t+h) = (2L/h)(i(t+h) − i(t)) − v(t) stamps a
 *          resistance 2L/h on the branch row.
 */
class InductorDevice : public Device {
public:
    InductorDevice(int node_a, int node_b, int branch, double inductance, double initial_current, bool has_initial);

    int GetNodeA() const { return m_NodeA; }
    int GetNodeB() const { return m_NodeB; }
    int GetBranch() const { return m_Branch; }
    double GetInductance() const { return m_Inductance; }

    void Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) override;
    void StampAc(MnaSystem &reactive, const std::vector<double> &x_dc) const override;
    double TerminalCurrent(int terminal, const std::vector<double> &x) const override;
    std::complex<double> AcTerminalCurrent(int terminal, const std::vector<double> &x_dc,
                                           const std::vector<std::complex<double>> &x_ac, double omega) const override;
    bool HasTransientState() const override;
    void AcceptStep(const std::vector<double> &x) override;
    void RejectStep() override;

private:
    int m_NodeA;
    int m_NodeB;
    int m_Branch;
    double m_Inductance;
    /// Flux L i and its derivative, the voltage, at the last accepted points.
    ChargeHistory m_History;
    ChargeHistory m_SavedHistory;
    double m_InitialCurrent;
    bool m_HasInitial;
};

/**
 * @enum    SourceWaveformKind
 * @brief   Classification of an independent source's waveform for Harmonic Balance validation.
 */
enum class SourceWaveformKind {
    DC,
    SINE,
    PULSE,
    CLOCK,
    NON_PERIODIC
};

/**
 * @struct  SourcePeriodicity
 * @brief   Periodicity metadata attached to independent voltage and current sources.
 */
struct SourcePeriodicity {
    SourceWaveformKind Kind = SourceWaveformKind::DC;
    double Frequency = 0.0;
    double MinEdgeTime = 0.0;
    bool IsSupportedInHb = true;
};

/**
 * @class   VoltageSourceDevice
 * @brief   An independent voltage source between two nodes as a branch unknown.
 * @details Its value comes from a waveform function of time, or from a settable DC level: a DC sweep overrides it
 *          with the sweep value and a live run with the changed value. At the operating point the waveform sources
 *          report the level they start from, like the netlist writes them, so the operating point matches the start
 *          of the transient.
 */
class VoltageSourceDevice : public Device {
public:
    /// Value of the source at the stamped point, given its DC level and the time context.
    using Waveform = std::function<double(double dc_value, const TimeContext &time)>;
    /// Next corner of the waveform strictly after an instant, or infinity; see Device::NextBreakpoint().
    using Breakpoints = std::function<double(double time)>;

    VoltageSourceDevice(int node_a, int node_b, int branch, double dc_value, Waveform waveform,
                        double ac_magnitude = 0.0, SourcePeriodicity periodicity = {});

    double GetDcValue() const;
    void SetDcValue(double value);
    int GetNodeA() const { return m_NodeA; }
    int GetNodeB() const { return m_NodeB; }
    int GetBranch() const { return m_Branch; }
    const SourcePeriodicity &GetPeriodicity() const { return m_Periodicity; }
    void SetBreakpoints(Breakpoints breakpoints);
    void SetSeedCallback(std::function<void(std::uint64_t)> set_seed);

    void Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) override;
    void StampAc(MnaSystem &reactive, const std::vector<double> &x_dc) const override;
    bool SupportsHarmonicBalance() const override;
    double TerminalCurrent(int terminal, const std::vector<double> &x) const override;
    std::complex<double> AcTerminalCurrent(int terminal, const std::vector<double> &x_dc,
                                           const std::vector<std::complex<double>> &x_ac, double omega) const override;
    void SetSourceScale(double scale) override;
    double NextBreakpoint(double time) const override;
    void SetRandomSeed(std::uint64_t seed) override;

private:
    int m_NodeA;
    int m_NodeB;
    int m_Branch;
    double m_DcValue;
    Waveform m_Waveform;
    Breakpoints m_Breakpoints;
    std::function<void(std::uint64_t)> m_SetSeed;
    double m_AcMagnitude;
    SourcePeriodicity m_Periodicity;
    double m_Scale = 1.0;
};

/**
 * @class   CurrentSourceDevice
 * @brief   An independent current source between two nodes, positive from the first node to the second.
 */
class CurrentSourceDevice : public Device {
public:
    CurrentSourceDevice(int node_a, int node_b, double dc_value, VoltageSourceDevice::Waveform waveform,
                        double ac_magnitude = 0.0, SourcePeriodicity periodicity = {});

    double GetDcValue() const;
    void SetDcValue(double value);
    int GetNodeA() const { return m_NodeA; }
    int GetNodeB() const { return m_NodeB; }
    const SourcePeriodicity &GetPeriodicity() const { return m_Periodicity; }
    double SamplePeriodicCurrent(int terminal, double time_seconds) const;
    void SetBreakpoints(VoltageSourceDevice::Breakpoints breakpoints);
    void SetSeedCallback(std::function<void(std::uint64_t)> set_seed);

    void Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) override;
    void StampAc(MnaSystem &reactive, const std::vector<double> &x_dc) const override;
    bool SupportsHarmonicBalance() const override;
    double TerminalCurrent(int terminal, const std::vector<double> &x) const override;
    std::complex<double> AcTerminalCurrent(int terminal, const std::vector<double> &x_dc,
                                           const std::vector<std::complex<double>> &x_ac, double omega) const override;
    void SetSourceScale(double scale) override;
    double NextBreakpoint(double time) const override;
    void SetRandomSeed(std::uint64_t seed) override;

private:
    int m_NodeA;
    int m_NodeB;
    double m_DcValue;
    VoltageSourceDevice::Waveform m_Waveform;
    VoltageSourceDevice::Breakpoints m_Breakpoints;
    std::function<void(std::uint64_t)> m_SetSeed;
    double m_AcMagnitude;
    SourcePeriodicity m_Periodicity;
    double m_Scale = 1.0;
    double m_Value = 0.0;
};

/**
 * @class   VcvsDevice
 * @brief   A voltage-controlled voltage source (SPICE E): v(a) − v(b) = gain·(v(cp) − v(cm)) as a branch.
 */
class VcvsDevice : public Device {
public:
    VcvsDevice(int node_a, int node_b, int control_positive, int control_negative, int branch, double gain);

    int GetNodeA() const { return m_NodeA; }
    int GetNodeB() const { return m_NodeB; }
    int GetControlPositive() const { return m_ControlPositive; }
    int GetControlNegative() const { return m_ControlNegative; }
    int GetBranch() const { return m_Branch; }
    double GetGain() const { return m_Gain; }

    void Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) override;
    double TerminalCurrent(int terminal, const std::vector<double> &x) const override;
    std::complex<double> AcTerminalCurrent(int terminal, const std::vector<double> &x_dc,
                                           const std::vector<std::complex<double>> &x_ac, double omega) const override;

private:
    int m_NodeA;
    int m_NodeB;
    int m_ControlPositive;
    int m_ControlNegative;
    int m_Branch;
    double m_Gain;
};

/**
 * @class   VccsDevice
 * @brief   A voltage-controlled current source (SPICE G): the current from a to b is gain·(v(cp) − v(cm)).
 */
class VccsDevice : public Device {
public:
    VccsDevice(int node_a, int node_b, int control_positive, int control_negative, double gain);

    int GetNodeA() const { return m_NodeA; }
    int GetNodeB() const { return m_NodeB; }
    int GetControlPositive() const { return m_ControlPositive; }
    int GetControlNegative() const { return m_ControlNegative; }
    double GetGain() const { return m_Gain; }

    void Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) override;
    double TerminalCurrent(int terminal, const std::vector<double> &x) const override;
    std::complex<double> AcTerminalCurrent(int terminal, const std::vector<double> &x_dc,
                                           const std::vector<std::complex<double>> &x_ac, double omega) const override;

private:
    int m_NodeA;
    int m_NodeB;
    int m_ControlPositive;
    int m_ControlNegative;
    double m_Gain;
};

/**
 * @class   CccsDevice
 * @brief   A current-controlled current source (SPICE F): the current from a to b is gain times the current of
 *         the controlling branch, which a probe source in series with the controlling terminal carries.
 */
class CccsDevice : public Device {
public:
    CccsDevice(int node_a, int node_b, int control_branch, double gain);

    int GetNodeA() const { return m_NodeA; }
    int GetNodeB() const { return m_NodeB; }
    int ControlBranch() const { return m_ControlBranch; }
    double GetGain() const { return m_Gain; }

    void Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) override;
    double TerminalCurrent(int terminal, const std::vector<double> &x) const override;
    std::complex<double> AcTerminalCurrent(int terminal, const std::vector<double> &x_dc,
                                           const std::vector<std::complex<double>> &x_ac, double omega) const override;

private:
    int m_NodeA;
    int m_NodeB;
    int m_ControlBranch;
    double m_Gain;
};

/**
 * @class   CcvsDevice
 * @brief   A current-controlled voltage source (SPICE H): v(a) − v(b) = gain times the controlling branch current.
 */
class CcvsDevice : public Device {
public:
    CcvsDevice(int node_a, int node_b, int branch, int control_branch, double gain);

    int GetNodeA() const { return m_NodeA; }
    int GetNodeB() const { return m_NodeB; }
    int GetBranch() const { return m_Branch; }
    int ControlBranch() const { return m_ControlBranch; }
    double GetGain() const { return m_Gain; }

    void Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) override;
    double TerminalCurrent(int terminal, const std::vector<double> &x) const override;
    std::complex<double> AcTerminalCurrent(int terminal, const std::vector<double> &x_dc,
                                           const std::vector<std::complex<double>> &x_ac, double omega) const override;

private:
    int m_NodeA;
    int m_NodeB;
    int m_Branch;
    int m_ControlBranch;
    double m_Gain;
};

/**
 * @class   SwitchDevice
 * @brief   A voltage-controlled switch with hysteresis (the SPICE S model and the contact of a relay): closed
 *         between Threshold − Hysteresis and Threshold + Hysteresis, as a resistor of Ron or Roff.
 * @details The state follows the control voltage of the current iterate and keeps itself between the thresholds,
 *         so noise around the threshold does not chatter; the state persists across the points of a transient.
 */
class SwitchDevice : public Device {
public:
    SwitchDevice(int node_a, int node_b, int control_positive, int control_negative, SwitchParameters parameters);

    void Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) override;
    bool IsLinear() const override { return false; }
    bool SupportsHarmonicBalance() const override { return false; }
    double TerminalCurrent(int terminal, const std::vector<double> &x) const override;
    std::complex<double> AcTerminalCurrent(int terminal, const std::vector<double> &x_dc,
                                           const std::vector<std::complex<double>> &x_ac, double omega) const override;
    void AcceptStep(const std::vector<double> &x) override;
    void RejectStep() override;
    bool HasSwitched() const override;

private:
    int m_NodeA;
    int m_NodeB;
    int m_ControlPositive;
    int m_ControlNegative;
    SwitchParameters m_Parameters;
    bool m_IsClosed = false;
    bool m_SavedIsClosed = false;
    double m_Resistance;
};

/**
 * @class   CoupledInductorDevice
 * @brief   Two coupled inductors as a pair of branch unknowns (the transformer windings): v1 = L1 di1/dt + M di2/dt
 *         and v2 = M di1/dt + L2 di2/dt, with the companion built from the fluxes of the windings, which gives
 *         exact shorts at the operating point.
 */
class CoupledInductorDevice : public Device {
public:
    /// primary_a/primary_b are the nodes of the primary branch, secondary_a/secondary_b of the secondary, with the
    /// branch currents flowing from the first node to the second; branch1 and branch2 are the branch unknowns.
    CoupledInductorDevice(int primary_a, int primary_b, int secondary_a, int secondary_b, int branch1, int branch2,
                          double primary_inductance, double secondary_inductance, double mutual);

    void Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) override;
    void StampAc(MnaSystem &reactive, const std::vector<double> &x_dc) const override;
    double TerminalCurrent(int terminal, const std::vector<double> &x) const override;
    std::complex<double> AcTerminalCurrent(int terminal, const std::vector<double> &x_dc,
                                           const std::vector<std::complex<double>> &x_ac, double omega) const override;
    bool HasTransientState() const override;
    void AcceptStep(const std::vector<double> &x) override;
    void RejectStep() override;

private:
    int m_PrimaryA;
    int m_PrimaryB;
    int m_SecondaryA;
    int m_SecondaryB;
    int m_Branch1;
    int m_Branch2;
    double m_L1;
    double m_L2;
    double m_M;
    /// Fluxes of the two windings and their voltages at the last accepted points.
    ChargeHistory m_History1;
    ChargeHistory m_History2;
    ChargeHistory m_SavedHistory1;
    ChargeHistory m_SavedHistory2;
};

/**
 * @class   NodeInitialConditionDevice
 * @brief   Clamps a node to a fixed initial voltage at the start of a transient (SPICE `.ic`), then disconnects.
 * @details Stamps a stiff Norton companion to ground only while `IsTransientStart` is true, leaving operating point
 *          and small-signal analyses unaffected and letting the node evolve freely for $t > 0$.
 */
class NodeInitialConditionDevice : public Device {
public:
    NodeInitialConditionDevice(int node, double voltage);

    void Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) override;
    double TerminalCurrent(int terminal, const std::vector<double> &x) const override;
    std::complex<double> AcTerminalCurrent(int terminal, const std::vector<double> &x_dc,
                                           const std::vector<std::complex<double>> &x_ac, double omega) const override;

private:
    int m_Node;
    double m_Voltage;
};

/**
 * @struct  NodeSeed
 * @brief   One node index and its initial Newton guess for `NodeSeedDevice`.
 */
struct NodeSeed {
    int Node = -1;
    double Voltage = 0.0;
};

/**
 * @class   NodeSeedDevice
 * @brief   Provides an initial Newton guess for bistable subcircuits such as flip-flops without stamping any branch.
 */
class NodeSeedDevice : public Device {
public:
    explicit NodeSeedDevice(std::vector<NodeSeed> seeds);

    void Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) override;
    void SeedOperatingPoint(std::vector<double> &x) const override;
    double TerminalCurrent(int terminal, const std::vector<double> &x) const override;
    std::complex<double> AcTerminalCurrent(int terminal, const std::vector<double> &x_dc,
                                           const std::vector<std::complex<double>> &x_ac, double omega) const override;

private:
    std::vector<NodeSeed> m_Seeds;
};

} // namespace Nodalys

#endif // NODALYS_DEVICES_H
