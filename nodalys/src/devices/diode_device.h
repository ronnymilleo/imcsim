/**
 * @file    diode_device.h
 * @brief   Diode of Nodalys: the junction with its series resistance and depletion charge.
 */

#ifndef NODALYS_DIODE_DEVICE_H
#define NODALYS_DIODE_DEVICE_H

#include "devices/devices.h"
#include "nodalys/types.h"

namespace Nodalys {

/**
 * @class   DiodeDevice
 * @brief   A diode, Zener diode, or LED: Shockley exponential junction with series resistance $R_S$, reverse breakdown
 *          ($BV, I_{BV}$), and SPICE depletion + transit-time junction charge.
 * @details
 * Topology when $R_S > 0$:
 * ```text
 *   anode o---[ Intrinsic Junction + Charge ]---o internal_node o---[ R_S ]---o cathode
 * ```
 * When $R_S = 0$, `internal` is `-1` and the intrinsic junction connects directly between `anode` and `cathode`.
 */
class DiodeDevice : public Device {
public:
    DiodeDevice(int anode, int cathode, int internal, DiodeParameters parameters);

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
    void SetTemperature(const ThermalConditions &thermal) override;

private:
    struct Junction {
        double Current;
        double Conductance;
    };
    /// Recomputes the parameters at the temperature, from the ones given at the nominal temperature.
    void ApplyTemperature(const ThermalConditions &thermal);
    Junction Evaluate(double voltage) const;
    double Charge(double voltage) const;
    double Capacitance(double voltage) const;
    double VoltageAt(const std::vector<double> &x) const;

    int m_Anode;
    int m_Cathode;
    int m_Internal;
    int m_JunctionNode; ///< Cathode side of the intrinsic junction (`m_Internal` when $R_S > 0$, else `m_Cathode`).
    /// The parameters as the model gives them, at the nominal temperature.
    DiodeParameters m_Given;
    /// The parameters at the temperature of the run, which every evaluation reads.
    DiodeParameters m_Parameters;
    ThermalConditions m_Thermal;
    double m_Vt;
    double m_Critical;
    /// Trapezoidal charge history at the last accepted time point.
    ChargeHistory m_History;
    ChargeHistory m_SavedHistory;
    /// Companion model of the junction capacitance from the most recent transient stamp.
    double m_CapConductance = 0.0;
    double m_CapEquivalent = 0.0;
};

} // namespace Nodalys

#endif // NODALYS_DIODE_DEVICE_H
