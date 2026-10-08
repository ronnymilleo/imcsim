/**
 * @file    op_amp.h
 * @brief   Operational amplifiers: an ideal one with a gain-bandwidth product, and Boyle macromodels of real parts.
 */

#ifndef IMCSIM_OP_AMP_H
#define IMCSIM_OP_AMP_H

#include "component.h"
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Core {

/**
 * @struct  OpAmpParameters
 * @brief   Datasheet specifications of an op-amp, from which its Boyle macromodel is built.
 * @details Headrooms are how close the output gets to each supply pin. The supply current is the quiescent
 *          current the part draws from its supply pins.
 */
struct OpAmpParameters {
    double OpenLoopGainDecibels = 106.0;
    double GainBandwidth = 1e6;
    // Volts per microsecond, as datasheets give it
    double SlewRate = 0.5;
    double PhaseMarginDegrees = 60.0;
    double InputBiasCurrent = 80e-9;
    double CommonModeRejectionDecibels = 90.0;
    double OutputResistance = 75.0;
    double ShortCircuitCurrent = 25e-3;
    double PositiveHeadroom = 1.0;
    double NegativeHeadroom = 1.0;
    double SupplyCurrent = 1.7e-3;
};

/**
 * @struct  OpAmpModel
 * @brief   A ready op-amp model: the ideal amplifier, or a real part built as a Boyle macromodel.
 * @details Name identifies the model in files and in the editor; SpiceName names its subcircuit. The ideal model
 *          only uses the gain-bandwidth product each op-amp sets for itself; its parameters are where a custom
 *          op-amp starts from.
 */
struct OpAmpModel {
    const char *Name;
    const char *Description;
    const char *SpiceName;
    bool Ideal;
    OpAmpParameters Parameters;
};

inline constexpr const char *CustomOpAmpModelName = "Custom";

/**
 * @class   OpAmp
 * @brief   An operational amplifier with output, non-inverting input (+), inverting input (-), V+ and V- terminals,
 *          in that order.
 * @details It starts ideal: no input current, no output resistance, 100 dB of gain rolling off at a gain-bandwidth
 *          product of its own, and an output that saturates within about 10 mV of its supply pins, which only carry
 *          a small clamp current. A ready model or custom specifications instead make it a Boyle macromodel, with
 *          slew rate, input bias current, output resistance, current limit, an output swing short of the supplies
 *          and a supply current. It has no numeric value.
 */
class OpAmp : public Component {
public:
    OpAmp();
    ~OpAmp() override = default;

    // Ready model
    const OpAmpModel *GetModel() const;
    void SetModel(const OpAmpModel &model);
    bool IsIdeal() const;
    double GetIdealBandwidth() const;
    void SetIdealBandwidth(double bandwidth);

    // Custom parameters
    bool IsCustom() const;
    void SetCustom();
    void SetCustomParameters(const OpAmpParameters &parameters);
    bool IsValidParameters(const OpAmpParameters &parameters) const;

    // Simulation
    const OpAmpParameters &GetParameters() const;
    const char *GetModelName() const;
    std::string GetSpiceModelName() const;

private:
    // Null when the op-amp uses its custom parameters
    const OpAmpModel *m_Model;
    OpAmpParameters m_CustomParameters;
    double m_IdealBandwidth;
};

// Models
std::span<const OpAmpModel> GetOpAmpModels(ComponentType type);
const OpAmpModel *FindOpAmpModel(ComponentType type, std::string_view name);
bool IsValidIdealBandwidth(double bandwidth);

// Netlist
std::string FormatIdealOpAmp(const OpAmp &op_amp, const std::vector<int> &nodes, int internal_node);
std::string FormatIdealOpAmpModel();
const char *GetIdealOpAmpModelName();
std::string FormatOpAmpSubcircuit(std::string_view name, const OpAmpParameters &parameters);

} // namespace Core

#endif // IMCSIM_OP_AMP_H
