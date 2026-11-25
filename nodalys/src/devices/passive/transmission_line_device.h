/**
 * @file    transmission_line_device.h
 * @brief   Uniform two-port TEM transmission line: Bergeron delay line in transient and dispersive telegrapher
 * equations in frequency-domain and Harmonic Balance analyses.
 */

#ifndef NODALYS_TRANSMISSION_LINE_DEVICE_H
#define NODALYS_TRANSMISSION_LINE_DEVICE_H

#include "devices/device.h"
#include <deque>
#include <vector>

namespace Nodalys {

/**
 * @class   TransmissionLineDevice
 * @brief   Four-terminal uniform two-port TEM transmission line supporting lossless Bergeron time-domain propagation
 * and exact lossless or skin/dielectric-lossy dispersive telegrapher equations in frequency-domain analyses.
 * @details Port 1 lies between `p1_pos` and `p1_neg` with entering branch current `branch1`; port 2 lies between
 *          `p2_pos` and `p2_neg` with entering branch current `branch2`. Two voltage-scaled even/odd constitutive
 *          equations keep the MNA matrix non-singular at DC (\f$\omega = 0\f$), quarter-wave, half-wave, and full-wave
 *          resonances.
 */
class TransmissionLineDevice : public Device, public FrequencyDevice, public TransientDevice {
public:
    TransmissionLineDevice(int p1_pos,
                           int p1_neg,
                           int p2_pos,
                           int p2_neg,
                           int branch1,
                           int branch2,
                           const TransmissionLineParameters &params);

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

    // FrequencyDevice role
    [[nodiscard]] bool SupportsPoleZero() const override { return false; }

    [[nodiscard]] std::string_view TransientUnsupportedReason() const override;
    void StampFrequencyDelta(const ComplexStamper &stamper, double omega) const override;

    // TransientDevice role
    void AcceptStep(const std::vector<double> &x) override;
    void RejectStep() override;
    [[nodiscard]] double NextBreakpoint(double time) const override;
    void NoteCorner(double time, double max_step) override;
    [[nodiscard]] double MaxTimeStep(double stop_time) const override;

    [[nodiscard]] DeviceRoles GetRoles() override { return {.Frequency = this, .Transient = this}; }

    [[nodiscard]] std::unique_ptr<Device> Clone() const override;

private:
    /// Accepted port voltages and outgoing characteristic waves at one past instant.
    struct HistorySample {
        double Time = 0.0;
        double WaveFromPort1 = 0.0; ///< \f$V_1(t) + Z_0 i_1(t)\f$, arriving at port 2 at \f$t + T_d\f$.
        double WaveFromPort2 = 0.0; ///< \f$V_2(t) + Z_0 i_2(t)\f$, arriving at port 1 at \f$t + T_d\f$.
    };

    /// Dc constitutive coefficients cached so `StampFrequencyDelta()` subtracts the exact DC stamp.
    struct DcCoefficients {
        double VoltageOdd = 1.0;
        double CurrentOdd = 0.0;
        double VoltageEven = 0.0;
        double CurrentEven = 0.0;
    };

    int m_P1Pos;
    int m_P1Neg;
    int m_P2Pos;
    int m_P2Neg;
    int m_Branch1;
    int m_Branch2;
    TransmissionLineParameters m_Params;
    double m_TotalInductance = 0.0;
    double m_TotalCapacitance = 0.0;
    DcCoefficients m_Dc;
    double m_FourKt = FOUR_KT_NOMINAL;

    // Transient Bergeron state
    std::deque<HistorySample> m_History;
    std::deque<double> m_PendingCorners;
    double m_LastStepTime = 0.0;
    bool m_StartedFromUic = false;
    bool m_WaveChangedOnAccept = false;
    double m_PrevSlope1 = 0.0;
    double m_PrevSlope2 = 0.0;
    bool m_HasPrevSlope = false;

    bool DetectWaveKink(double w1, double w2, double &step_dt);
    [[nodiscard]] std::pair<double, double> InterpolatePastWaves(double query_time) const;
    void StampDcBranches(MnaSystem &sys) const;
    void StampKcl(MnaSystem &sys) const;
    void StampShortDelayStep(MnaSystem &sys, double step) const;
    void StampTransientBergeron(MnaSystem &sys, const TimeContext &time);
};

} // namespace Nodalys

#endif // NODALYS_TRANSMISSION_LINE_DEVICE_H
