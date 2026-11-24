/**
 * @file    switch_device.h
 * @brief   Voltage-controlled switch with hysteresis (SPICE S model).
 */

#ifndef NODALYS_SWITCH_DEVICE_H
#define NODALYS_SWITCH_DEVICE_H

#include "devices/device.h"
#include "nodalys/types.h"

namespace Nodalys {

/**
 * @class   SwitchDevice
 * @brief   A voltage-controlled switch with hysteresis (the SPICE S model and the contact of a relay): closed
 *          between Threshold − Hysteresis and Threshold + Hysteresis, as a resistor of Ron or Roff.
 * @details The state follows the control voltage of the current iterate and keeps itself between the thresholds,
 *          so noise around the threshold does not chatter; the state persists across the points of a transient.
 */
class SwitchDevice : public Device, public NonlinearDevice, public TransientDevice {
public:
    SwitchDevice(int node_a, int node_b, int control_positive, int control_negative, SwitchParameters parameters);

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

    [[nodiscard]] DeviceRoles GetRoles() override { return {.Nonlinear = this, .Transient = this}; }

    [[nodiscard]] std::unique_ptr<Device> Clone() const override;

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

} // namespace Nodalys

#endif // NODALYS_SWITCH_DEVICE_H
