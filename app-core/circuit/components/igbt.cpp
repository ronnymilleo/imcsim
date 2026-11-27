/**
 * @file    igbt.cpp
 * @brief   N-channel IGBTs, simulated by Nodalys from a model of a fixed list or from custom datasheet figures.
 */

#include "circuit/components/igbt.h"

#include "circuit/components/model_catalog.h"
#include <array>
#include <memory>

namespace Core {

namespace {

/// Default model first. Typical figures of two classes of trench field-stop IGBT with a co-packaged diode, not of one
/// part number: the gate threshold, the transconductance at the rated current, the on-state line through VCE(sat) at
/// the rated current, the capacitances at VCE = 25 V, the tail as a share of the current and its decay, and the
/// absolute maximum ratings with an ideal heatsink.
constexpr auto IGBT_MODELS = std::to_array<IGBTModel>({
    {"600V 30A",
     "Generic 600 V 30 A trench IGBT with diode, VCE(sat) 1.55 V",
     "IGBT600V30A",
     {.GateThresholdVoltage = 5.0,
      .Transconductance = 15.0,
      .TransconductanceCurrent = 30.0,
      .OnThresholdVoltage = 0.8,
      .OnResistance = 0.025,
      .InputCapacitance = 1.6e-9,
      .OutputCapacitance = 110e-12,
      .ReverseTransferCapacitance = 50e-12,
      .TailFraction = 0.15,
      .TailTimeConstant = 150e-9,
      .DiodeForwardVoltage = 1.6,
      .MaxCollectorEmitterVoltage = 600.0,
      .MaxGateEmitterVoltage = 20.0,
      .MaxCollectorCurrent = 30.0,
      .MaxPower = 200.0},
     ComponentType::IGBT},
    {"1200V 40A",
     "Generic 1200 V 40 A trench IGBT with diode, VCE(sat) 2.1 V, longer tail",
     "IGBT1200V40A",
     {.GateThresholdVoltage = 5.8,
      .Transconductance = 20.0,
      .TransconductanceCurrent = 40.0,
      .OnThresholdVoltage = 0.9,
      .OnResistance = 0.03,
      .InputCapacitance = 2.3e-9,
      .OutputCapacitance = 150e-12,
      .ReverseTransferCapacitance = 100e-12,
      .TailFraction = 0.25,
      .TailTimeConstant = 300e-9,
      .DiodeForwardVoltage = 2.0,
      .MaxCollectorEmitterVoltage = 1200.0,
      .MaxGateEmitterVoltage = 20.0,
      .MaxCollectorCurrent = 40.0,
      .MaxPower = 480.0},
     ComponentType::IGBT},
});

} // namespace

/**
 * @brief   Creates an IGBT with the default model.
 */
IGBT::IGBT() : ModeledComponent(ComponentType::IGBT, GetIGBTModels().front(), "IGBTCUSTOM_") {
}

/**
 * @brief   Copies the component with its name, value and settings.
 * @return  An independent component of the same kind.
 */
std::unique_ptr<Component> IGBT::Clone() const {
    return std::make_unique<IGBT>(*this);
}

/**
 * @brief   Checks whether custom figures can be simulated.
 * @param[in] parameters  Candidate figures.
 * @return  True when the transconductance, its test current, RCE, the tail time constant and the ratings are
 *          positive, V0 and the diode forward voltage are not negative, Cies and Coes are each at least Cres, which is
 *          not negative, and the tail share is from 0 to 1. The gate threshold may be any value.
 */
bool IGBT::IsValidParameters(const IGBTParameters &parameters) {
    const bool is_channel_valid = parameters.Transconductance > 0.0 && parameters.TransconductanceCurrent > 0.0;
    const bool is_line_valid = parameters.OnThresholdVoltage >= 0.0 && parameters.OnResistance > 0.0;
    const bool are_capacitances_valid = parameters.ReverseTransferCapacitance >= 0.0
                                     && parameters.InputCapacitance >= parameters.ReverseTransferCapacitance
                                     && parameters.OutputCapacitance >= parameters.ReverseTransferCapacitance;
    const bool is_tail_valid =
        parameters.TailFraction >= 0.0 && parameters.TailFraction <= 1.0 && parameters.TailTimeConstant > 0.0;
    const bool are_ratings_valid = parameters.MaxCollectorEmitterVoltage > 0.0 && parameters.MaxGateEmitterVoltage > 0.0
                                && parameters.MaxCollectorCurrent > 0.0 && parameters.MaxPower > 0.0;
    return is_channel_valid && is_line_valid && are_capacitances_valid && is_tail_valid
        && parameters.DiodeForwardVoltage >= 0.0 && are_ratings_valid;
}

/**
 * @brief   Tells whether a kind of component is an IGBT, so it can be cast to one.
 * @param[in] type  Kind of component.
 * @return  True for ComponentType::IGBT.
 */
bool IsIGBT(const ComponentType type) {
    return type == ComponentType::IGBT;
}

/**
 * @brief   Lists the ready IGBT models.
 * @return  The models, default first.
 */
std::span<const IGBTModel> GetIGBTModels() {
    return IGBT_MODELS;
}

/**
 * @brief   Finds a ready IGBT model by name.
 * @param[in] type  Kind of component; only ComponentType::IGBT has models.
 * @param[in] name  Model name as in IGBTModel::Name, case sensitive.
 * @return  The model, or nullptr when there is none with that name.
 */
const IGBTModel *FindIGBTModel(const ComponentType type, const std::string_view name) {
    return FindModelByName<IGBTModel>(GetModelsOfType<IGBTModel>(IGBT_MODELS, type), name);
}

/**
 * @brief   Works out the gain of the square-law channel from the transconductance at its test current.
 * @param[in] parameters  Datasheet figures.
 * @return  \f$K = g_{fs}^2 / (2 I_{gfs})\f$, in Amperes per square Volt: the channel carries \f$K/2\,v_{ov}^2\f$,
 *          whose slope at the test current is \f$g_{fs}\f$.
 */
double GetChannelGain(const IGBTParameters &parameters) {
    return parameters.Transconductance * parameters.Transconductance / (2.0 * parameters.TransconductanceCurrent);
}

/**
 * @brief   Returns the SPICE subcircuit name written for an IGBT in exported netlists.
 * @return  Null-terminated subcircuit name `"imcsim_igbt"`.
 */
const char *GetIGBTSubcircuitName() {
    return "imcsim_igbt";
}

/**
 * @brief   Writes the subcircuit that the Netlist window shows for an IGBT.
 * @return  The ".subckt" block, newline included.
 * @details The classic two-device picture of an IGBT: an N-channel MOSFET whose drain draws the base of a PNP, so the
 *          channel current is multiplied by the bipolar part, with the capacitances of the datasheet around them.
 *          `VTO`, `KP` and the capacitances are parameters of the instance.
 * @note    imcsim does not run it: the ngspice backend refuses an IGBT, which Nodalys simulates as one device with the
 *          on-state line and the tail current of its datasheet, so the subcircuit is only in the netlist the Netlist
 *          window shows and exports, and no simulation has checked it.
 */
std::string FormatIGBTSubcircuit() {
    return ".subckt imcsim_igbt c g e params: VTO=5.5 KP=2.5 CGC=50p CGE=1.55n CCE=60p\n"
           "M1 b g e e IMCSIM_IGBT_MN L=1 W=1\n"
           "Q1 e b c IMCSIM_IGBT_QP\n"
           "Cgc g c {CGC}\n"
           "Cge g e {CGE}\n"
           "Cce c e {CCE}\n"
           ".model IMCSIM_IGBT_MN NMOS(LEVEL=1 VTO={VTO} KP={KP})\n"
           ".model IMCSIM_IGBT_QP PNP(IS=1e-12 BF=2)\n"
           ".ends imcsim_igbt\n";
}

} // namespace Core
