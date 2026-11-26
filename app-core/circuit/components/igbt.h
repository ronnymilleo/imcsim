/**
 * @file    igbt.h
 * @brief   N-channel IGBTs, simulated by Nodalys from a model of a fixed list or from custom datasheet figures.
 */

#ifndef IMCSIM_IGBT_H
#define IMCSIM_IGBT_H

#include "circuit/components/component.h"
#include "circuit/components/modeled_component.h"
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace Core {

/**
 * @struct  IGBTParameters
 * @brief   Datasheet figures of an N-channel IGBT, in SI units.
 * @details The collector current is the smaller of what the channel lets through, \f$K/2\,(V_{GE} - V_{GE(th)})^2\f$
 *          with \f$K = g_{fs}^2 / (2 I_{gfs})\f$, and what the on-state line \f$V_{CE(sat)} = V_0 + R_{CE} I_C\f$
 *          carries. The capacitances are those of the datasheet, constant, and the tail current of a turn-off starts
 *          at a share of the current that fell and decays with its time constant. A co-packaged diode, from emitter to
 *          collector, is given by its forward voltage at the rated current; zero leaves it out. The ratings are not
 *          part of the model: the simulator checks the results against them.
 */
struct IGBTParameters {
    double GateThresholdVoltage = 5.0;          ///< \f$V_{GE(th)}\f$, in Volts.
    double Transconductance = 15.0;             ///< \f$g_{fs}\f$ at its test current, in Siemens.
    double TransconductanceCurrent = 30.0;      ///< \f$I_{gfs}\f$: collector current \f$g_{fs}\f$ is given at, in A.
    double OnThresholdVoltage = 0.8;            ///< \f$V_0\f$ of the on-state line, in Volts.
    double OnResistance = 0.025;                ///< \f$R_{CE}\f$: slope of the on-state line, in Ohms.
    double InputCapacitance = 1.6e-9;           ///< \f$C_{ies}\f$, in Farads.
    double OutputCapacitance = 110e-12;         ///< \f$C_{oes}\f$, in Farads.
    double ReverseTransferCapacitance = 50e-12; ///< \f$C_{res}\f$, the Miller capacitance, in Farads.
    double TailFraction = 0.15;                 ///< Share of the falling current the tail starts at, 0 to 1.
    double TailTimeConstant = 150e-9;           ///< Time constant of the tail, in seconds.
    /// Forward voltage of the co-packaged diode at the rated collector current, in Volts; zero for none.
    double DiodeForwardVoltage = 1.6;
    // Datasheet ratings
    double MaxCollectorEmitterVoltage = 600.0; ///< \f$V_{CES}\f$, in Volts.
    double MaxGateEmitterVoltage = 20.0;       ///< \f$V_{GES}\f$, in Volts.
    double MaxCollectorCurrent = 30.0;         ///< Continuous \f$I_C\f$, in Amperes.
    double MaxPower = 200.0;                   ///< \f$P_{tot}\f$ with an ideal heatsink, in Watts.
};

/**
 * @struct  IGBTModel
 * @brief   A ready IGBT model.
 * @details Name identifies the model in files and in the editor; SpiceName names it in the netlist the Netlist window
 *          shows.
 */
struct IGBTModel {
    const char *Name;
    const char *Description;
    const char *SpiceName;
    IGBTParameters Parameters;
    ComponentType Type;
};

inline constexpr const char *CUSTOM_IGBT_MODEL_NAME = CUSTOM_MODEL_NAME;

/**
 * @class   IGBT
 * @brief   An N-channel IGBT with collector, gate and emitter terminals, in that order.
 * @details It has no numeric value: a ready model or its own custom figures set its behavior. Only Nodalys simulates
 *          it, as a native device with the capacitances and the diode around it; ngspice refuses it, and the netlist
 *          shows it as an instance of the imcsim_igbt subcircuit, which no run checks.
 */
class IGBT : public ModeledComponent<IGBTModel, IGBTParameters> {
public:
    IGBT();
    ~IGBT() override = default;

    // Copying
    std::unique_ptr<Component> Clone() const override;

    // Custom parameters
    static bool IsValidParameters(const IGBTParameters &parameters);
};

// IGBTs
bool IsIGBT(ComponentType type);
std::span<const IGBTModel> GetIGBTModels();
const IGBTModel *FindIGBTModel(ComponentType type, std::string_view name);
double GetChannelGain(const IGBTParameters &parameters);

// Netlist
const char *GetIGBTSubcircuitName();
std::string FormatIGBTSubcircuit();

} // namespace Core

#endif // IMCSIM_IGBT_H
