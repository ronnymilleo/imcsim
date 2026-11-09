/**
 * @file    devices.h
 * @brief   The devices the native solver stamps into the MNA system: one class per element kind, each computing the
 *          currents and the linearized companion model of its physics.
 * @details The stamp conventions follow the modified nodal analysis of Vlach & Singhal, "Computer Methods for Circuit
 *          Analysis and Design", and the companion models of Chua & Lin, "Computer-Aided Analysis of Electronic
 *          Circuits". Nonlinear devices linearize at the current Newton iterate: they stamp their differential
 *          conductance and an equivalent current source, so the assembled system stays linear at every iteration.
 */

#ifndef IMCSIM_NATIVE_DEVICES_H
#define IMCSIM_NATIVE_DEVICES_H

#include "components/voltage_controlled_switch.h"
#include "native/mna_system.h"
#include <complex>
#include <functional>
#include <string>
#include <vector>

namespace Core::Native {

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
     * @param[in,out] reactive  System holding the imaginary-part coefficients $C_{\text{AC}}$ and AC excitation $b_{\text{AC}}$.
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
                                       const std::vector<std::complex<double>> &adjoint,
                                       double frequency_hz) const {
        (void)x_dc;
        (void)adjoint;
        (void)frequency_hz;
        return 0.0;
    }

    /// True when the device carries state between time points (companion models of C, L and junction charges).
    virtual bool HasTransientState() const { return false; }
    /// Saves the state of the accepted point x (called after each converged transient step).
    virtual void AcceptStep(const std::vector<double> &x) { (void)x; }
    /// Restores the state saved before a step that the LTE control rejected.
    virtual void RejectStep() {}

    /**
     * @brief   Scales the independent sources of the device, for the source-stepping fallback of Newton-Raphson.
     * @param[in] scale  Share of the source values stamped; devices that are not sources ignore it.
     */
    virtual void SetSourceScale(double scale) { (void)scale; }

    /**
     * @brief   Seeds the initial iterate of a DC operating point solve for nonlinear junctions.
     * @param[in,out] x  The iterate to seed; devices that need an initial bias guess update it.
     */
    virtual void SeedOperatingPoint(std::vector<double> &x) const { (void)x; }
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

    void Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) override;
    double TerminalCurrent(int terminal, const std::vector<double> &x) const override;
    std::complex<double> AcTerminalCurrent(int terminal, const std::vector<double> &x_dc,
                                           const std::vector<std::complex<double>> &x_ac,
                                           double omega) const override;
    double OutputNoiseVariance(const std::vector<double> &x_dc, const std::vector<std::complex<double>> &adjoint,
                               double frequency_hz) const override;

private:
    int m_NodeA;
    int m_NodeB;
    double m_Conductance;
    bool m_Noisy;
};

/**
 * @class   CapacitorDevice
 * @brief   A capacitor between two nodes with a trapezoidal companion model in a transient.
 * @details Trapezoidal integration, as in Vlach & Singhal, chapter on numerical integration of differential
 *          equations: i(t+h) = (2C/h)(v(t+h) − v(t)) − i(t). At the operating point the capacitor is an open
 *          circuit. With an initial condition and uic, the first point starts from it.
 */
class CapacitorDevice : public Device {
public:
    CapacitorDevice(int node_a, int node_b, double capacitance, double initial_voltage, bool has_initial);

    void Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) override;
    void StampAc(MnaSystem &reactive, const std::vector<double> &x_dc) const override;
    double TerminalCurrent(int terminal, const std::vector<double> &x) const override;
    std::complex<double> AcTerminalCurrent(int terminal, const std::vector<double> &x_dc,
                                           const std::vector<std::complex<double>> &x_ac,
                                           double omega) const override;
    bool HasTransientState() const override;
    void AcceptStep(const std::vector<double> &x) override;
    void RejectStep() override;

private:
    int m_NodeA;
    int m_NodeB;
    double m_Capacitance;
    /// State at the last accepted point: voltage and current, trapezoidal history.
    double m_Voltage = 0.0;
    double m_Current = 0.0;
    double m_SavedVoltage = 0.0;
    double m_SavedCurrent = 0.0;
    double m_InitialVoltage;
    bool m_HasInitial;
    /// Companion model of the last stamp: conductance 2C/h and current source, for the results.
    double m_LastConductance = 0.0;
    double m_EquivalentCurrent = 0.0;
};

/**
 * @class   InductorDevice
 * @brief   An inductor as a branch between two nodes with a trapezoidal companion model.
 * @details The branch unknown carries the inductor current, so at the operating point the inductor is an exact
 *          short (v = 0). In a transient, v(t+h) = (2L/h)(i(t+h) − i(t)) − v(t) stamps a resistance 2L/h on the
 *          branch row.
 */
class InductorDevice : public Device {
public:
    InductorDevice(int node_a, int node_b, int branch, double inductance, double initial_current, bool has_initial);

    void Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) override;
    void StampAc(MnaSystem &reactive, const std::vector<double> &x_dc) const override;
    double TerminalCurrent(int terminal, const std::vector<double> &x) const override;
    std::complex<double> AcTerminalCurrent(int terminal, const std::vector<double> &x_dc,
                                           const std::vector<std::complex<double>> &x_ac,
                                           double omega) const override;
    bool HasTransientState() const override;
    void AcceptStep(const std::vector<double> &x) override;
    void RejectStep() override;

private:
    int m_NodeA;
    int m_NodeB;
    int m_Branch;
    double m_Inductance;
    /// State at the last accepted point: current and voltage, trapezoidal history.
    double m_Current = 0.0;
    double m_Voltage = 0.0;
    double m_SavedCurrent = 0.0;
    double m_SavedVoltage = 0.0;
    double m_InitialCurrent;
    bool m_HasInitial;
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

    VoltageSourceDevice(int node_a, int node_b, int branch, double dc_value, Waveform waveform,
                        double ac_magnitude = 0.0);

    double GetDcValue() const;
    void SetDcValue(double value);
    int NodeA() const { return m_NodeA; }
    int NodeB() const { return m_NodeB; }
    int Branch() const { return m_Branch; }

    void Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) override;
    void StampAc(MnaSystem &reactive, const std::vector<double> &x_dc) const override;
    double TerminalCurrent(int terminal, const std::vector<double> &x) const override;
    std::complex<double> AcTerminalCurrent(int terminal, const std::vector<double> &x_dc,
                                           const std::vector<std::complex<double>> &x_ac,
                                           double omega) const override;
    void SetSourceScale(double scale) override;

private:
    int m_NodeA;
    int m_NodeB;
    int m_Branch;
    double m_DcValue;
    Waveform m_Waveform;
    double m_AcMagnitude;
    double m_Scale = 1.0;
};

/**
 * @class   CurrentSourceDevice
 * @brief   An independent current source between two nodes, positive from the first node to the second.
 */
class CurrentSourceDevice : public Device {
public:
    CurrentSourceDevice(int node_a, int node_b, double dc_value, VoltageSourceDevice::Waveform waveform,
                        double ac_magnitude = 0.0);

    double GetDcValue() const;
    void SetDcValue(double value);
    int NodeA() const { return m_NodeA; }
    int NodeB() const { return m_NodeB; }

    void Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) override;
    void StampAc(MnaSystem &reactive, const std::vector<double> &x_dc) const override;
    double TerminalCurrent(int terminal, const std::vector<double> &x) const override;
    std::complex<double> AcTerminalCurrent(int terminal, const std::vector<double> &x_dc,
                                           const std::vector<std::complex<double>> &x_ac,
                                           double omega) const override;
    void SetSourceScale(double scale) override;

private:
    int m_NodeA;
    int m_NodeB;
    double m_DcValue;
    VoltageSourceDevice::Waveform m_Waveform;
    double m_AcMagnitude;
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

    void Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) override;
    double TerminalCurrent(int terminal, const std::vector<double> &x) const override;
    std::complex<double> AcTerminalCurrent(int terminal, const std::vector<double> &x_dc,
                                           const std::vector<std::complex<double>> &x_ac,
                                           double omega) const override;

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

    void Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) override;
    double TerminalCurrent(int terminal, const std::vector<double> &x) const override;
    std::complex<double> AcTerminalCurrent(int terminal, const std::vector<double> &x_dc,
                                           const std::vector<std::complex<double>> &x_ac,
                                           double omega) const override;

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

    void Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) override;
    double TerminalCurrent(int terminal, const std::vector<double> &x) const override;
    std::complex<double> AcTerminalCurrent(int terminal, const std::vector<double> &x_dc,
                                           const std::vector<std::complex<double>> &x_ac,
                                           double omega) const override;

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

    void Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) override;
    double TerminalCurrent(int terminal, const std::vector<double> &x) const override;
    std::complex<double> AcTerminalCurrent(int terminal, const std::vector<double> &x_dc,
                                           const std::vector<std::complex<double>> &x_ac,
                                           double omega) const override;

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
    double TerminalCurrent(int terminal, const std::vector<double> &x) const override;
    std::complex<double> AcTerminalCurrent(int terminal, const std::vector<double> &x_dc,
                                           const std::vector<std::complex<double>> &x_ac,
                                           double omega) const override;
    void AcceptStep(const std::vector<double> &x) override;
    void RejectStep() override;

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
 *         and v2 = M di1/dt + L2 di2/dt, with the trapezoidal companion built from the inverse inductance matrix,
 *         which stays well-behaved for coupling below 1 and gives exact shorts at the operating point.
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
                                           const std::vector<std::complex<double>> &x_ac,
                                           double omega) const override;
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
    /// Trapezoidal histories: branch currents and branch voltages at the last accepted point.
    double m_I1 = 0.0, m_I2 = 0.0, m_V1 = 0.0, m_V2 = 0.0;
    double m_SavedI1 = 0.0, m_SavedI2 = 0.0, m_SavedV1 = 0.0, m_SavedV2 = 0.0;
};

} // namespace Core::Native

#endif // IMCSIM_NATIVE_DEVICES_H
