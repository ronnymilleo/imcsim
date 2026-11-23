/**
 * @file    passive_devices.h
 * @brief   Linear passive MNA devices: resistors, capacitors, inductors, and coupled inductors.
 */

#ifndef NODALYS_PASSIVE_DEVICES_H
#define NODALYS_PASSIVE_DEVICES_H

#include "devices/device.h"
#include "devices/sensitivity.h"
#include <map>

namespace Nodalys {

/**
 * @class   CapacitorDevice
 * @brief   A capacitor between two nodes with a companion model of the chosen integration method in a transient.
 * @details With the trapezoidal rule, as in Vlach & Singhal, chapter on numerical integration of differential
 *          equations: i(t+h) = (2C/h)(v(t+h) − v(t)) − i(t); see `integration.h` for the other formulas. At the
 *          operating point the capacitor is an open circuit. With an initial condition and uic, the first point starts
 *          from it.
 */
class CapacitorDevice : public Device, public SensitiveDevice {
public:
    CapacitorDevice(int node_a, int node_b, double capacitance, double initial_voltage, bool has_initial);

    int GetNodeA() const { return m_NodeA; }

    int GetNodeB() const { return m_NodeB; }

    double GetCapacitance() const { return m_Capacitance; }

    /// A capacitor with no IC= of its own starts at the difference of the initial voltages of its nodes, as in SPICE.
    void TakeInitialVoltageFrom(const std::map<int, double> &node_voltages);

    void Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) override;
    void StampAc(MnaSystem &reactive, const std::vector<double> &x_dc) const override;
    double TerminalCurrent(int terminal, const std::vector<double> &x) const override;
    std::complex<double> AcTerminalCurrent(int terminal,
                                           const std::vector<double> &x_dc,
                                           const std::vector<std::complex<double>> &x_ac,
                                           double omega) const override;
    bool HasTransientState() const override;
    void AcceptStep(const std::vector<double> &x) override;
    void RejectStep() override;

    [[nodiscard]] const SensitiveDevice *AsSensitive() const override { return this; }

    [[nodiscard]] std::optional<SensitivityDescriptor> DescribeSensitivity() const override;
    [[nodiscard]] double DcSensitivityDerivative(const DcAdjointView &point) const override;
    [[nodiscard]] std::complex<double> AcSensitivityDerivative(const AcAdjointView &point) const override;
    [[nodiscard]] std::unique_ptr<Device> Clone() const override;

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
 * @class   CoupledInductorDevice
 * @brief   Two coupled inductors as a pair of branch unknowns (the transformer windings): v1 = L1 di1/dt + M di2/dt
 *          and v2 = M di1/dt + L2 di2/dt, with the companion built from the fluxes of the windings, which gives
 *          exact shorts at the operating point.
 */
class CoupledInductorDevice : public Device {
public:
    /// primary_a/primary_b are the nodes of the primary branch, secondary_a/secondary_b of the secondary, with the
    /// branch currents flowing from the first node to the second; branch1 and branch2 are the branch unknowns.
    CoupledInductorDevice(int primary_a,
                          int primary_b,
                          int secondary_a,
                          int secondary_b,
                          int branch1,
                          int branch2,
                          double primary_inductance,
                          double secondary_inductance,
                          double mutual);

    void Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) override;
    void StampAc(MnaSystem &reactive, const std::vector<double> &x_dc) const override;
    double TerminalCurrent(int terminal, const std::vector<double> &x) const override;
    std::complex<double> AcTerminalCurrent(int terminal,
                                           const std::vector<double> &x_dc,
                                           const std::vector<std::complex<double>> &x_ac,
                                           double omega) const override;
    bool HasTransientState() const override;
    void AcceptStep(const std::vector<double> &x) override;
    void RejectStep() override;
    [[nodiscard]] std::unique_ptr<Device> Clone() const override;

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
 * @class   InductorDevice
 * @brief   An inductor as a branch between two nodes with a companion model of the chosen integration method.
 * @details The branch unknown carries the inductor current, so at the operating point the inductor is an exact
 *          short (v = 0). In a transient with the trapezoidal rule, v(t+h) = (2L/h)(i(t+h) − i(t)) − v(t) stamps a
 *          resistance 2L/h on the branch row.
 */
class InductorDevice : public Device, public SensitiveDevice {
public:
    InductorDevice(int node_a, int node_b, int branch, double inductance, double initial_current, bool has_initial);

    int GetNodeA() const { return m_NodeA; }

    int GetNodeB() const { return m_NodeB; }

    int GetBranch() const { return m_Branch; }

    double GetInductance() const { return m_Inductance; }

    void Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) override;
    void StampAc(MnaSystem &reactive, const std::vector<double> &x_dc) const override;
    double TerminalCurrent(int terminal, const std::vector<double> &x) const override;
    std::complex<double> AcTerminalCurrent(int terminal,
                                           const std::vector<double> &x_dc,
                                           const std::vector<std::complex<double>> &x_ac,
                                           double omega) const override;
    bool HasTransientState() const override;
    void AcceptStep(const std::vector<double> &x) override;
    void RejectStep() override;

    [[nodiscard]] const SensitiveDevice *AsSensitive() const override { return this; }

    [[nodiscard]] std::optional<SensitivityDescriptor> DescribeSensitivity() const override;
    [[nodiscard]] double DcSensitivityDerivative(const DcAdjointView &point) const override;
    [[nodiscard]] std::complex<double> AcSensitivityDerivative(const AcAdjointView &point) const override;
    [[nodiscard]] std::unique_ptr<Device> Clone() const override;

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
 * @class   ResistorDevice
 * @brief   A resistor between two nodes; also the model of a closed or open contact.
 */
class ResistorDevice : public Device, public SensitiveDevice {
public:
    ResistorDevice(int node_a, int node_b, double resistance, bool noisy = true);

    double GetResistance() const;
    void SetResistance(double resistance);

    int GetNodeA() const { return m_NodeA; }

    int GetNodeB() const { return m_NodeB; }

    void Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) override;
    double TerminalCurrent(int terminal, const std::vector<double> &x) const override;
    std::complex<double> AcTerminalCurrent(int terminal,
                                           const std::vector<double> &x_dc,
                                           const std::vector<std::complex<double>> &x_ac,
                                           double omega) const override;
    double OutputNoiseVariance(const std::vector<double> &x_dc,
                               const std::vector<std::complex<double>> &adjoint,
                               double frequency_hz) const override;
    void SetTemperature(const ThermalConditions &thermal) override;

    [[nodiscard]] const SensitiveDevice *AsSensitive() const override { return this; }

    bool ApplyLiveChange(LiveQuantity quantity, double value) override;
    [[nodiscard]] std::optional<SensitivityDescriptor> DescribeSensitivity() const override;
    [[nodiscard]] double DcSensitivityDerivative(const DcAdjointView &point) const override;
    [[nodiscard]] std::complex<double> AcSensitivityDerivative(const AcAdjointView &point) const override;
    [[nodiscard]] std::unique_ptr<Device> Clone() const override;

private:
    int m_NodeA;
    int m_NodeB;
    double m_Conductance;
    bool m_Noisy;
    /// $4kT$ of the thermal noise; a resistor keeps its value at any temperature, as no temperature coefficient is set.
    double m_FourKt = FOUR_KT_NOMINAL;
};

} // namespace Nodalys

#endif // NODALYS_PASSIVE_DEVICES_H
