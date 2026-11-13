/**
 * @file    smooth_logic.h
 * @brief   Smooth continuous C^inf logic gate macromodels with inertial propagation delay and analog output stages.
 */

#ifndef NODALYS_SMOOTH_LOGIC_H
#define NODALYS_SMOOTH_LOGIC_H

#include "devices/devices.h"
#include "nodalys/types.h"

#include <vector>

namespace Nodalys {

/**
 * @class   SmoothLogicDevice
 * @brief   An infinitely differentiable C^inf smooth logic gate device for continuous MNA simulation.
 * @details Solves logic gates directly within the continuous analog Newton-Raphson solver without code models or
 *          discontinuous events. Supports configurable threshold, transition width, inertial delay, and output
 * resistance.
 */
class SmoothLogicDevice : public Device {
public:
    SmoothLogicDevice(LogicGateType type, int output, std::vector<int> inputs, int internal,
                      SmoothLogicParameters params);

    // Simulation
    void Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) override;
    void StampAc(MnaSystem &reactive, const std::vector<double> &x_dc) const override;
    bool IsLinear() const override { return false; }
    void StampCharge(MnaSystem &sys, const std::vector<double> &x) const override;
    bool Limit(std::vector<double> &x_new, const std::vector<double> &x_old) const override;

    // Terminal Currents
    double TerminalCurrent(int terminal, const std::vector<double> &x) const override;
    std::complex<double> AcTerminalCurrent(int terminal, const std::vector<double> &x_dc,
                                           const std::vector<std::complex<double>> &x_ac, double omega) const override;

    // Transient State
    bool HasTransientState() const override;
    void AcceptStep(const std::vector<double> &x) override;
    void RejectStep() override;

private:
    struct LogicEvaluation {
        double TargetVoltage = 0.0;
        std::vector<double> Derivatives;
        double OutputDerivative = 0.0;
    };

    LogicEvaluation Evaluate(const std::vector<double> &x) const;
    double ReadVoltage(const std::vector<double> &x, int node) const;

    // Gate Configuration
    LogicGateType m_Type;
    int m_Output;
    std::vector<int> m_Inputs;
    int m_Internal;
    SmoothLogicParameters m_Params;

    // Internal Dynamic Filter
    double m_FilterConductance = 1.0;
    double m_FilterCapacitance = 0.0;

    // Transient State
    double m_CapConductance = 0.0;
    double m_CapEquivalent = 0.0;
    double m_Voltage = 0.0;
    double m_Current = 0.0;
    double m_SavedVoltage = 0.0;
    double m_SavedCurrent = 0.0;
};

} // namespace Nodalys

#endif // NODALYS_SMOOTH_LOGIC_H
