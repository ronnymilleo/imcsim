/**
 * @file    schematic_file.cpp
 * @brief   Saves schematics to JSON and loads them back, reporting the parts that could not be read.
 */

#include "document/schematic_file.h"

#include "components/bjt.h"
#include "components/clock.h"
#include "components/controlled_source.h"
#include "components/diode.h"
#include "components/logic_input.h"
#include "components/logic_output.h"
#include "components/logic_part.h"
#include "components/mosfet.h"
#include "components/net_label.h"
#include "components/op_amp.h"
#include "components/potentiometer.h"
#include "components/relay.h"
#include "components/source.h"
#include "components/subcircuit.h"
#include "components/switch.h"
#include "components/transformer.h"
#include "components/voltage_controlled_switch.h"
#include "document/subcircuit_pins.h"
#include "editing/connectivity.h"
#include "editing/element_factory.h"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <array>
#include <format>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <utility>

namespace GUI {

namespace {

constexpr std::string_view FORMAT_NAME = "imcsim-schematic";
constexpr int FORMAT_VERSION = 1;

/**
 * @struct  ParameterKey
 * @brief   JSON key of one member of a parameter set, such as Core::PulseParameters, and the member itself.
 */
template <typename Parameters> struct ParameterKey {
    const char *Name;
    double Parameters::*Value;
};

/// The sine keys sit in the element object itself, where the first files with AC sources put them.
constexpr auto AC_KEYS = std::to_array<ParameterKey<Core::ACParameters>>({
    {"amplitude", &Core::ACParameters::Amplitude},
    {"frequency", &Core::ACParameters::Frequency},
    {"offset", &Core::ACParameters::Offset},
    {"ac_magnitude", &Core::ACParameters::Magnitude},
});

constexpr auto SWITCH_KEYS = std::to_array<ParameterKey<Core::SwitchParameters>>({
    {"threshold", &Core::SwitchParameters::Threshold},
    {"hysteresis", &Core::SwitchParameters::Hysteresis},
    {"on_resistance", &Core::SwitchParameters::OnResistance},
    {"off_resistance", &Core::SwitchParameters::OffResistance},
});

constexpr auto RELAY_KEYS = std::to_array<ParameterKey<Core::RelayParameters>>({
    {"coil_resistance", &Core::RelayParameters::CoilResistance},
    {"coil_inductance", &Core::RelayParameters::CoilInductance},
    {"pull_in", &Core::RelayParameters::PullInVoltage},
    {"drop_out", &Core::RelayParameters::DropOutVoltage},
    {"contact_resistance", &Core::RelayParameters::ContactResistance},
});

constexpr auto TRANSFORMER_KEYS = std::to_array<ParameterKey<Core::TransformerParameters>>({
    {"turns_ratio", &Core::TransformerParameters::TurnsRatio},
    {"coupling", &Core::TransformerParameters::Coupling},
    {"winding_resistance", &Core::TransformerParameters::WindingResistance},
});

constexpr auto LOGIC_KEYS = std::to_array<ParameterKey<Core::LogicParameters>>({
    {"logic_level", &Core::LogicParameters::LogicLevel},
    {"propagation_delay", &Core::LogicParameters::PropagationDelay},
});

constexpr auto CLOCK_KEYS = std::to_array<ParameterKey<Core::ClockParameters>>({
    {"frequency", &Core::ClockParameters::Frequency},
    {"duty_cycle", &Core::ClockParameters::DutyCycle},
    {"logic_level", &Core::ClockParameters::LogicLevel},
    {"start_delay", &Core::ClockParameters::StartDelay},
});

constexpr auto LOGIC_INPUT_KEYS = std::to_array<ParameterKey<Core::LogicInputParameters>>({
    {"logic_level", &Core::LogicInputParameters::LogicLevel},
});

constexpr auto LOGIC_OUTPUT_KEYS = std::to_array<ParameterKey<Core::LogicOutputParameters>>({
    {"logic_level", &Core::LogicOutputParameters::LogicLevel},
});

constexpr auto PULSE_KEYS = std::to_array<ParameterKey<Core::PulseParameters>>({
    {"low", &Core::PulseParameters::Low},
    {"high", &Core::PulseParameters::High},
    {"delay", &Core::PulseParameters::Delay},
    {"rise", &Core::PulseParameters::RiseTime},
    {"fall", &Core::PulseParameters::FallTime},
    {"width", &Core::PulseParameters::Width},
    {"period", &Core::PulseParameters::Period},
});

/// Named after the SPICE EXP parameters.
constexpr auto EXP_KEYS = std::to_array<ParameterKey<Core::EXPParameters>>({
    {"v1", &Core::EXPParameters::InitialValue},
    {"v2", &Core::EXPParameters::PeakValue},
    {"td1", &Core::EXPParameters::RiseDelay},
    {"tau1", &Core::EXPParameters::RiseTimeConstant},
    {"td2", &Core::EXPParameters::FallDelay},
    {"tau2", &Core::EXPParameters::FallTimeConstant},
});

constexpr auto SFFM_KEYS = std::to_array<ParameterKey<Core::SFFMParameters>>({
    {"offset", &Core::SFFMParameters::Offset},
    {"amplitude", &Core::SFFMParameters::Amplitude},
    {"carrier", &Core::SFFMParameters::CarrierFrequency},
    {"mdi", &Core::SFFMParameters::ModulationIndex},
    {"signal", &Core::SFFMParameters::SignalFrequency},
});

constexpr auto AM_KEYS = std::to_array<ParameterKey<Core::AMParameters>>({
    {"amplitude", &Core::AMParameters::Amplitude},
    {"offset", &Core::AMParameters::Offset},
    {"mod_frequency", &Core::AMParameters::ModulatingFrequency},
    {"carrier", &Core::AMParameters::CarrierFrequency},
    {"delay", &Core::AMParameters::Delay},
});

/// Named after the SPICE diode parameters.
constexpr auto DIODE_KEYS = std::to_array<ParameterKey<Core::DiodeParameters>>({
    {"is", &Core::DiodeParameters::SaturationCurrent},
    {"n", &Core::DiodeParameters::EmissionCoefficient},
    {"rs", &Core::DiodeParameters::SeriesResistance},
    {"bv", &Core::DiodeParameters::BreakdownVoltage},
    {"ibv", &Core::DiodeParameters::BreakdownCurrent},
    {"cjo", &Core::DiodeParameters::JunctionCapacitance},
    {"vj", &Core::DiodeParameters::JunctionPotential},
    {"m", &Core::DiodeParameters::GradingCoefficient},
    {"tt", &Core::DiodeParameters::TransitTime},
});

constexpr auto BJT_KEYS = std::to_array<ParameterKey<Core::BJTParameters>>({
    {"is", &Core::BJTParameters::SaturationCurrent},
    {"bf", &Core::BJTParameters::ForwardBeta},
    {"br", &Core::BJTParameters::ReverseBeta},
    {"vaf", &Core::BJTParameters::EarlyVoltage},
    {"ikf", &Core::BJTParameters::ForwardKneeCurrent},
    {"ise", &Core::BJTParameters::LeakageSaturationCurrent},
    {"ne", &Core::BJTParameters::LeakageEmissionCoefficient},
    {"rb", &Core::BJTParameters::BaseResistance},
    {"rc", &Core::BJTParameters::CollectorResistance},
    {"re", &Core::BJTParameters::EmitterResistance},
    {"cje", &Core::BJTParameters::EmitterCapacitance},
    {"cjc", &Core::BJTParameters::CollectorCapacitance},
    {"tf", &Core::BJTParameters::TransitTime},
    {"vceo", &Core::BJTParameters::MaxCollectorEmitterVoltage},
    {"icmax", &Core::BJTParameters::MaxCollectorCurrent},
    {"pmax", &Core::BJTParameters::MaxPower},
});

constexpr auto MOSFET_KEYS = std::to_array<ParameterKey<Core::MOSFETParameters>>({
    {"vto", &Core::MOSFETParameters::ThresholdVoltage},
    {"kp", &Core::MOSFETParameters::Transconductance},
    {"lambda", &Core::MOSFETParameters::ChannelModulation},
    {"rd", &Core::MOSFETParameters::DrainResistance},
    {"rs", &Core::MOSFETParameters::SourceResistance},
    {"cgso", &Core::MOSFETParameters::GateSourceOverlap},
    {"cgdo", &Core::MOSFETParameters::GateDrainOverlap},
    {"w", &Core::MOSFETParameters::Width},
    {"l", &Core::MOSFETParameters::Length},
    {"vdsmax", &Core::MOSFETParameters::MaxDrainSourceVoltage},
    {"vgsmax", &Core::MOSFETParameters::MaxGateSourceVoltage},
    {"idmax", &Core::MOSFETParameters::MaxDrainCurrent},
    {"pmax", &Core::MOSFETParameters::MaxPower},
});

constexpr auto OP_AMP_KEYS = std::to_array<ParameterKey<Core::OpAmpParameters>>({
    {"avol_db", &Core::OpAmpParameters::OpenLoopGainDecibels},
    {"gbw", &Core::OpAmpParameters::GainBandwidth},
    {"slew_v_per_us", &Core::OpAmpParameters::SlewRate},
    {"phase_margin", &Core::OpAmpParameters::PhaseMarginDegrees},
    {"ib", &Core::OpAmpParameters::InputBiasCurrent},
    {"cmrr_db", &Core::OpAmpParameters::CommonModeRejectionDecibels},
    {"rout", &Core::OpAmpParameters::OutputResistance},
    {"isc", &Core::OpAmpParameters::ShortCircuitCurrent},
    {"headroom_positive", &Core::OpAmpParameters::PositiveHeadroom},
    {"headroom_negative", &Core::OpAmpParameters::NegativeHeadroom},
    {"iq", &Core::OpAmpParameters::SupplyCurrent},
});

constexpr auto TRANSIENT_KEYS = std::to_array<ParameterKey<Core::TransientSettings>>({
    {"stop", &Core::TransientSettings::StopTime},
    {"step", &Core::TransientSettings::TimeStep},
});

/// The points per decade are an integer, so they are written and read apart.
constexpr auto AC_SWEEP_KEYS = std::to_array<ParameterKey<Core::ACSweepSettings>>({
    {"start", &Core::ACSweepSettings::StartFrequency},
    {"stop", &Core::ACSweepSettings::StopFrequency},
});

constexpr auto PARAMETER_STEP_KEYS = std::to_array<ParameterKey<ParameterStepSettings>>({
    {"start", &ParameterStepSettings::Start},
    {"stop", &ParameterStepSettings::Stop},
    {"step", &ParameterStepSettings::Step},
});

constexpr auto SWEEP_RANGE_KEYS = std::to_array<ParameterKey<Core::SweepRange>>({
    {"start", &Core::SweepRange::Start},
    {"stop", &Core::SweepRange::Stop},
    {"step", &Core::SweepRange::Step},
});

/**
 * @brief   Readers return no value when the key is missing or holds another type, so a damaged file never throws.
 */
std::optional<int> ReadInt(const nlohmann::json &object, const char *key) {
    const auto entry = object.find(key);
    if (entry == object.end() || !entry->is_number_integer()) {
        return std::nullopt;
    }
    return entry->get<int>();
}

std::optional<double> ReadNumber(const nlohmann::json &object, const char *key) {
    const auto entry = object.find(key);
    if (entry == object.end() || !entry->is_number()) {
        return std::nullopt;
    }
    return entry->get<double>();
}

std::optional<std::string> ReadString(const nlohmann::json &object, const char *key) {
    const auto entry = object.find(key);
    if (entry == object.end() || !entry->is_string()) {
        return std::nullopt;
    }
    return entry->get<std::string>();
}

std::optional<bool> ReadBool(const nlohmann::json &object, const char *key) {
    const auto entry = object.find(key);
    if (entry == object.end() || !entry->is_boolean()) {
        return std::nullopt;
    }
    return entry->get<bool>();
}

std::optional<GridPoint> ReadPointValue(const nlohmann::json &value) {
    if (!value.is_array() || value.size() != 2 || !value[0].is_number_integer() || !value[1].is_number_integer()) {
        return std::nullopt;
    }
    return GridPoint{value[0].get<int>(), value[1].get<int>()};
}

std::optional<GridPoint> ReadPoint(const nlohmann::json &object, const char *key) {
    const auto entry = object.find(key);
    if (entry == object.end()) {
        return std::nullopt;
    }
    return ReadPointValue(*entry);
}

template <typename Parameters, std::size_t Count>
void WriteParameters(const std::array<ParameterKey<Parameters>, Count> &keys, const Parameters &parameters,
                     nlohmann::json &object) {
    for (const ParameterKey<Parameters> &key : keys) {
        object[key.Name] = parameters.*key.Value;
    }
}

/**
 * @brief   One waveform of a source, such as its pulse, under its own key so the sine keys in the element object stay
 *          apart.
 */
template <typename Parameters, std::size_t Count>
void WriteSourceParameters(const std::array<ParameterKey<Parameters>, Count> &keys, const Parameters &parameters,
                           const char *name, nlohmann::json &object) {
    nlohmann::json values = nlohmann::json::object();
    WriteParameters(keys, parameters, values);
    object[name] = values;
}

/**
 * @brief   Missing keys keep the value the parameters already have; a key with something other than a number is an
 *          error.
 */
template <typename Parameters, std::size_t Count>
std::expected<void, std::string> ReadParameters(const std::array<ParameterKey<Parameters>, Count> &keys,
                                                const nlohmann::json &object, Parameters &parameters) {
    for (const ParameterKey<Parameters> &key : keys) {
        if (!object.contains(key.Name)) {
            continue;
        }
        const std::optional<double> value = ReadNumber(object, key.Name);
        if (!value) {
            return std::unexpected(std::format("has no valid {}", key.Name));
        }
        parameters.*key.Value = *value;
    }
    return {};
}

/**
 * @brief   One waveform of a source; nothing to read when the file has no key for it.
 */
template <typename Parameters, std::size_t Count>
std::expected<void, std::string> ReadSourceParameters(const nlohmann::json &object, const char *name,
                                                      const std::array<ParameterKey<Parameters>, Count> &keys,
                                                      Parameters &parameters) {
    const auto entry = object.find(name);
    if (entry == object.end()) {
        return {};
    }
    if (!entry->is_object()) {
        return std::unexpected(std::format("has a {} that is not an object", name));
    }
    return ReadParameters(keys, *entry, parameters);
}

/**
 * @brief   Parts whose parameters are all there is to them save them as one object.
 */
template <typename Part, typename Parameters, std::size_t Count>
void WritePartParameters(const Part &part, const std::array<ParameterKey<Parameters>, Count> &keys,
                         nlohmann::json &object) {
    nlohmann::json parameters = nlohmann::json::object();
    WriteParameters(keys, part.GetParameters(), parameters);
    object["parameters"] = parameters;
}

/**
 * @brief   Missing parameters keep those the part starts with.
 */
template <typename Part, typename Parameters, std::size_t Count>
std::expected<void, std::string> ReadPartParameters(const nlohmann::json &object, Part &part,
                                                    const std::array<ParameterKey<Parameters>, Count> &keys) {
    Parameters parameters = part.GetParameters();
    if (const auto parameters_object = object.find("parameters"); parameters_object != object.end()) {
        if (!parameters_object->is_object()) {
            return std::unexpected("has parameters that are not an object");
        }
        if (const auto read = ReadParameters(keys, *parameters_object, parameters); !read) {
            return read;
        }
    }
    if (!Part::IsValidParameters(parameters)) {
        return std::unexpected("has parameters out of range");
    }
    part.SetParameters(parameters);
    return {};
}

/**
 * @brief   Parts with a model save its name, plus their parameters when custom.
 */
template <typename Part, typename Parameters, std::size_t Count>
void WriteModelChoice(const Part &part, const std::array<ParameterKey<Parameters>, Count> &keys,
                      nlohmann::json &object) {
    // A library model lives in the document, which the part names
    if (part.HasLibraryModel()) {
        object["library_model"] = part.GetLibraryModel();
        return;
    }
    object["model"] = part.GetModelName();
    if (part.IsCustom()) {
        nlohmann::json parameters = nlohmann::json::object();
        WriteParameters(keys, part.GetParameters(), parameters);
        object["parameters"] = parameters;
    }
}

/**
 * @brief   Parts with a ready model or custom parameters; the ideal op-amp also keeps its bandwidth.
 */
void WriteModelFields(const Core::Component &component, nlohmann::json &object) {
    const Core::ComponentType type = component.GetType();
    if (Core::IsDiode(type)) {
        WriteModelChoice(static_cast<const Core::Diode &>(component), DIODE_KEYS, object);
    } else if (Core::IsBJT(type)) {
        WriteModelChoice(static_cast<const Core::BJT &>(component), BJT_KEYS, object);
    } else if (Core::IsMOSFET(type)) {
        WriteModelChoice(static_cast<const Core::MOSFET &>(component), MOSFET_KEYS, object);
    } else if (type == Core::ComponentType::OP_AMP) {
        const auto &op_amp = static_cast<const Core::OpAmp &>(component);
        WriteModelChoice(op_amp, OP_AMP_KEYS, object);
        if (op_amp.IsIdeal()) {
            object["gbw"] = op_amp.GetIdealBandwidth();
        }
    }
}

/**
 * @brief   The state or figures of the other parts that have any.
 */
void WriteKindFields(const Core::Component &component, nlohmann::json &object) {
    const Core::ComponentType type = component.GetType();
    if (Core::IsCurrentControlled(type)) {
        object["control"] = static_cast<const Core::ControlledSource &>(component).GetControllingCurrent();
    } else if (type == Core::ComponentType::SUBCIRCUIT) {
        const auto &instance = static_cast<const Core::SubcircuitInstance &>(component);
        object["definition"] = instance.GetDefinitionName();
        if (!instance.GetParameters().empty()) {
            object["parameters"] = instance.GetParameters();
        }
    } else if (type == Core::ComponentType::NET_LABEL || Core::IsSubcircuitPin(type)) {
        // A label or pin marker with no name yet is saved without the key
        if (const std::string &net = static_cast<const Core::NetLabel &>(component).GetNet(); !net.empty()) {
            object["net"] = net;
        }
    } else if (Core::IsSwitch(type)) {
        object["closed"] = static_cast<const Core::Switch &>(component).IsClosed();
    } else if (type == Core::ComponentType::LOGIC_INPUT) {
        object["high"] = static_cast<const Core::LogicInput &>(component).IsHigh();
    } else if (type == Core::ComponentType::POTENTIOMETER) {
        object["wiper"] = static_cast<const Core::Potentiometer &>(component).GetWiper();
    }
}

/**
 * @brief   Parts whose figures are all there is to them: the switch that follows a voltage, the relay, the transformer,
 *          the gates and flip-flops with their logic family, a clock and a logic input.
 */
void WriteFigureFields(const Core::Component &component, nlohmann::json &object) {
    const Core::ComponentType type = component.GetType();
    if (type == Core::ComponentType::V_SWITCH) {
        WritePartParameters(static_cast<const Core::VoltageControlledSwitch &>(component), SWITCH_KEYS, object);
    } else if (type == Core::ComponentType::RELAY) {
        WritePartParameters(static_cast<const Core::Relay &>(component), RELAY_KEYS, object);
    } else if (type == Core::ComponentType::TRANSFORMER) {
        WritePartParameters(static_cast<const Core::Transformer &>(component), TRANSFORMER_KEYS, object);
    } else if (Core::IsLogicPart(type)) {
        const auto &part = static_cast<const Core::LogicPart &>(component);
        WritePartParameters(part, LOGIC_KEYS, object);
        if (Core::HasVariableInputs(type)) {
            object["inputs"] = part.GetInputCount();
        }
    } else if (type == Core::ComponentType::CLOCK) {
        WritePartParameters(static_cast<const Core::Clock &>(component), CLOCK_KEYS, object);
    } else if (type == Core::ComponentType::LOGIC_INPUT) {
        WritePartParameters(static_cast<const Core::LogicInput &>(component), LOGIC_INPUT_KEYS, object);
    } else if (type == Core::ComponentType::LOGIC_OUTPUT) {
        WritePartParameters(static_cast<const Core::LogicOutput &>(component), LOGIC_OUTPUT_KEYS, object);
    }
}

nlohmann::json WriteElement(const UIElement &element) {
    const Core::Component &component = element.GetComponent();
    nlohmann::json object = {
        {"type", component.GetTypeName()},
        {"x", element.GetPosition().X},
        {"y", element.GetPosition().Y},
        {"rotation", ToDegrees(element.GetRotation())},
    };
    // Only mirrored elements carry the key, so files of unmirrored schematics stay as they were
    if (element.IsMirrored()) {
        object["mirrored"] = true;
    }
    if (!component.GetName().empty()) {
        object["name"] = component.GetName();
    }
    if (component.HasValue()) {
        if (const auto &expression = component.GetValueExpression()) {
            object["value"] = *expression;
        } else {
            object["value"] = component.GetValue();
        }
    }
    // Only a capacitor or an inductor carries one, and only when it is set
    if (const std::optional<double> initial = component.GetInitialCondition()) {
        object["ic"] = *initial;
    }
    // Every waveform is saved, so switching type after loading keeps the parameters of the others
    if (Core::IsSource(component.GetType())) {
        const auto &source = static_cast<const Core::Source &>(component);
        object["source"] = Core::GetSourceTypeName(source.GetSourceType());
        WriteParameters(AC_KEYS, source.GetAC(), object);
        WriteSourceParameters(PULSE_KEYS, source.GetPulse(), "pulse", object);
        WriteSourceParameters(EXP_KEYS, source.GetEXP(), "exp", object);
        WriteSourceParameters(SFFM_KEYS, source.GetSFFM(), "sffm", object);
        WriteSourceParameters(AM_KEYS, source.GetAM(), "am", object);
        nlohmann::json pwl = nlohmann::json::array();
        for (const Core::PWLPoint &point : source.GetPWL().Points) {
            pwl.push_back({{"time", point.Time}, {"value", point.Value}});
        }
        object["pwl"] = pwl;
    }
    WriteModelFields(component, object);
    WriteKindFields(component, object);
    WriteFigureFields(component, object);
    return object;
}

/**
 * @brief   Every waveform key is optional, so files saved before a waveform existed keep the default one of the source.
 */
std::expected<void, std::string> ReadSource(const nlohmann::json &object, Core::Source &source) {
    if (object.contains("source")) {
        const std::optional<std::string> type_name = ReadString(object, "source");
        const std::optional<Core::Source::SourceType> type =
            type_name ? Core::ParseSourceType(*type_name) : std::nullopt;
        if (!type) {
            return std::unexpected("has a source type other than DC, AC, Pulse, EXP, SFFM, AM or PWL");
        }
        source.SetSourceType(*type);
    }
    Core::ACParameters ac = source.GetAC();
    if (const auto read = ReadParameters(AC_KEYS, object, ac); !read) {
        return read;
    }
    // The AC sweep used the amplitude before the magnitude was apart, so older files keep their results
    if (!object.contains("ac_magnitude")) {
        ac.Magnitude = ac.Amplitude;
    }
    if (!source.IsValidAC(ac)) {
        return std::unexpected("has an AC frequency that is not positive");
    }
    source.SetAC(ac);

    Core::PulseParameters pulse = source.GetPulse();
    if (const auto read = ReadSourceParameters(object, "pulse", PULSE_KEYS, pulse); !read) {
        return read;
    }
    if (!source.IsValidPulse(pulse)) {
        return std::unexpected("has a pulse with a negative time or a period that is not positive");
    }
    source.SetPulse(pulse);

    Core::EXPParameters exp = source.GetEXP();
    if (const auto read = ReadSourceParameters(object, "exp", EXP_KEYS, exp); !read) {
        return read;
    }
    if (!source.IsValidEXP(exp)) {
        return std::unexpected("has an exponential with a negative delay, a fall before its rise or a time constant "
                               "that is not positive");
    }
    source.SetEXP(exp);

    Core::SFFMParameters sffm = source.GetSFFM();
    if (const auto read = ReadSourceParameters(object, "sffm", SFFM_KEYS, sffm); !read) {
        return read;
    }
    if (!source.IsValidSFFM(sffm)) {
        return std::unexpected("has a single-frequency FM with a carrier or signal frequency that is not positive");
    }
    source.SetSFFM(sffm);

    Core::AMParameters am = source.GetAM();
    if (const auto read = ReadSourceParameters(object, "am", AM_KEYS, am); !read) {
        return read;
    }
    if (!source.IsValidAM(am)) {
        return std::unexpected("has an amplitude modulation with a negative delay or a frequency that is not "
                               "positive");
    }
    source.SetAM(am);

    if (const auto points = object.find("pwl"); points != object.end()) {
        if (!points->is_array()) {
            return std::unexpected("has piecewise linear points that are not a list");
        }
        Core::PWLParameters pwl;
        pwl.Points.clear();
        for (const nlohmann::json &point : *points) {
            const std::optional<double> time = point.is_object() ? ReadNumber(point, "time") : std::nullopt;
            const std::optional<double> value = point.is_object() ? ReadNumber(point, "value") : std::nullopt;
            if (!time || !value) {
                return std::unexpected("has a piecewise linear point without a numeric time and value");
            }
            pwl.Points.push_back({.Time = *time, .Value = *value});
        }
        if (!source.IsValidPWL(pwl)) {
            return std::unexpected("has piecewise linear points that are fewer than two, negative or not "
                                   "increasing");
        }
        source.SetPWL(pwl);
    }
    return {};
}

/**
 * @brief   Missing custom parameters keep those of the default model of the part.
 */
template <typename Part, typename Model, typename Parameters, std::size_t Count>
std::expected<void, std::string> ReadModelChoice(const nlohmann::json &object, Part &part,
                                                 const Model *(*find_model)(Core::ComponentType, std::string_view),
                                                 const char *custom_name,
                                                 const std::array<ParameterKey<Parameters>, Count> &keys) {
    // The model it names may be missing from the document; the checks of a run say so
    if (object.contains("library_model")) {
        const std::optional<std::string> library_model = ReadString(object, "library_model");
        if (!library_model || library_model->empty() || !part.TakesLibraryModel()) {
            return std::unexpected("has no valid library model");
        }
        part.SetLibraryModel(*library_model);
        return {};
    }
    const std::optional<std::string> model_name = ReadString(object, "model");
    if (!model_name) {
        return std::unexpected("has no valid model");
    }
    if (*model_name != custom_name) {
        const Model *model = find_model(part.GetType(), *model_name);
        if (model == nullptr) {
            return std::unexpected("has no valid model");
        }
        part.SetModel(*model);
        return {};
    }

    Parameters parameters = part.GetParameters();
    if (const auto parameters_object = object.find("parameters"); parameters_object != object.end()) {
        if (!parameters_object->is_object()) {
            return std::unexpected("has model parameters that are not an object");
        }
        if (const auto read = ReadParameters(keys, *parameters_object, parameters); !read) {
            return read;
        }
    }
    if (!part.IsValidParameters(parameters)) {
        return std::unexpected("has model parameters out of range");
    }
    part.SetCustomParameters(parameters);
    return {};
}

std::expected<void, std::string> ReadModel(const nlohmann::json &object, Core::Component &component) {
    const Core::ComponentType type = component.GetType();
    if (Core::IsDiode(type)) {
        return ReadModelChoice(object, static_cast<Core::Diode &>(component), &Core::FindDiodeModel,
                               Core::CUSTOM_DIODE_MODEL_NAME, DIODE_KEYS);
    }
    if (Core::IsBJT(type)) {
        return ReadModelChoice(object, static_cast<Core::BJT &>(component), &Core::FindBJTModel,
                               Core::CUSTOM_BJT_MODEL_NAME, BJT_KEYS);
    }
    if (Core::IsMOSFET(type)) {
        return ReadModelChoice(object, static_cast<Core::MOSFET &>(component), &Core::FindMOSFETModel,
                               Core::CUSTOM_MOSFET_MODEL_NAME, MOSFET_KEYS);
    }
    // Files saved before op-amps had models hold ideal ones
    if (type == Core::ComponentType::OP_AMP) {
        auto &op_amp = static_cast<Core::OpAmp &>(component);
        if (object.contains("model")) {
            if (const auto read =
                    ReadModelChoice(object, op_amp, &Core::FindOpAmpModel, Core::CUSTOM_OP_AMP_MODEL_NAME, OP_AMP_KEYS);
                !read) {
                return read;
            }
        }
        if (object.contains("gbw")) {
            const std::optional<double> bandwidth = ReadNumber(object, "gbw");
            if (!bandwidth || !Core::IsValidIdealBandwidth(*bandwidth)) {
                return std::unexpected("has a gain-bandwidth product that is not positive");
            }
            op_amp.SetIdealBandwidth(*bandwidth);
        }
    }
    return {};
}

// Each reader fills what one kind of part carries, and does nothing for the others; a missing key keeps the default

/**
 * @brief   The current a controlled source follows; a missing one loads as none, and the simulation then asks for one.
 */
std::expected<void, std::string> ReadControllingCurrent(const nlohmann::json &object, Core::Component &component) {
    if (!Core::IsCurrentControlled(component.GetType()) || !object.contains("control")) {
        return {};
    }
    const std::optional<std::string> current = ReadString(object, "control");
    if (!current) {
        return std::unexpected("has a controlling current that is not text");
    }
    static_cast<Core::ControlledSource &>(component).SetControllingCurrent(*current);
    return {};
}

std::expected<void, std::string> ReadNetName(const nlohmann::json &object, Core::Component &component) {
    const Core::ComponentType type = component.GetType();
    if ((type != Core::ComponentType::NET_LABEL && !Core::IsSubcircuitPin(type)) || !object.contains("net")) {
        return {};
    }
    const std::optional<std::string> net = ReadString(object, "net");
    const bool is_pin = Core::IsSubcircuitPin(type);
    const bool valid =
        net && (is_pin ? Core::SubcircuitInstance::IsValidIdentifier(*net) : Core::NetLabel::IsValidNet(*net));
    if (!valid) {
        return std::unexpected(is_pin ? "has a subcircuit pin name that is not an identifier"
                                      : "has a net name that is not 1 to 32 letters, digits, _, - or .");
    }
    static_cast<Core::NetLabel &>(component).SetNet(*net);
    return {};
}

/**
 * @brief   A switch saved without a state stays open.
 */
std::expected<void, std::string> ReadSwitchState(const nlohmann::json &object, Core::Component &component) {
    if (!Core::IsSwitch(component.GetType()) || !object.contains("closed")) {
        return {};
    }
    const std::optional<bool> closed = ReadBool(object, "closed");
    if (!closed) {
        return std::unexpected("has a closed flag that is not true or false");
    }
    static_cast<Core::Switch &>(component).SetClosed(*closed);
    return {};
}

/**
 * @brief   A potentiometer saved without a wiper keeps it in the middle.
 */
std::expected<void, std::string> ReadWiper(const nlohmann::json &object, Core::Component &component) {
    if (component.GetType() != Core::ComponentType::POTENTIOMETER || !object.contains("wiper")) {
        return {};
    }
    const std::optional<double> wiper = ReadNumber(object, "wiper");
    if (!wiper || !Core::Potentiometer::IsValidWiper(*wiper)) {
        return std::unexpected("has a wiper that is not a number from 0 to 1");
    }
    static_cast<Core::Potentiometer &>(component).SetWiper(*wiper);
    return {};
}

/**
 * @brief   A logic input saved without a state stays low.
 */
std::expected<void, std::string> ReadLogicInputState(const nlohmann::json &object, Core::Component &component) {
    if (component.GetType() != Core::ComponentType::LOGIC_INPUT || !object.contains("high")) {
        return {};
    }
    const std::optional<bool> high = ReadBool(object, "high");
    if (!high) {
        return std::unexpected("has a high flag that is not true or false");
    }
    static_cast<Core::LogicInput &>(component).SetHigh(*high);
    return {};
}

using StateReader = std::expected<void, std::string> (*)(const nlohmann::json &, Core::Component &);

constexpr auto STATE_READERS = std::to_array<StateReader>(
    {&ReadControllingCurrent, &ReadNetName, &ReadSwitchState, &ReadWiper, &ReadLogicInputState});

/**
 * @brief   What only some kinds of part carry: the current a controlled source follows, a net name, a switch state, a
 *          wiper and the state of a logic input.
 */
std::expected<void, std::string> ReadStateFields(const nlohmann::json &object, Core::Component &component) {
    for (const StateReader reader : STATE_READERS) {
        if (const auto read = reader(object, component); !read) {
            return std::unexpected(read.error());
        }
    }
    return {};
}

/**
 * @brief   Each reader fills the figures of one kind of part and does nothing for the others; missing keys leave the
 *          part as it starts.
 */
std::expected<void, std::string> ReadSwitchFigures(const nlohmann::json &object, Core::Component &component) {
    if (component.GetType() != Core::ComponentType::V_SWITCH) {
        return {};
    }
    return ReadPartParameters(object, static_cast<Core::VoltageControlledSwitch &>(component), SWITCH_KEYS);
}

std::expected<void, std::string> ReadRelayFigures(const nlohmann::json &object, Core::Component &component) {
    if (component.GetType() != Core::ComponentType::RELAY) {
        return {};
    }
    return ReadPartParameters(object, static_cast<Core::Relay &>(component), RELAY_KEYS);
}

std::expected<void, std::string> ReadTransformerFigures(const nlohmann::json &object, Core::Component &component) {
    if (component.GetType() != Core::ComponentType::TRANSFORMER) {
        return {};
    }
    return ReadPartParameters(object, static_cast<Core::Transformer &>(component), TRANSFORMER_KEYS);
}

/**
 * @brief   A gate saved without a count has two inputs.
 */
std::expected<void, std::string> ReadLogicFigures(const nlohmann::json &object, Core::Component &component) {
    if (!Core::IsLogicPart(component.GetType())) {
        return {};
    }
    auto &part = static_cast<Core::LogicPart &>(component);
    if (const auto read = ReadPartParameters(object, part, LOGIC_KEYS); !read) {
        return std::unexpected(read.error());
    }
    if (!Core::HasVariableInputs(component.GetType()) || !object.contains("inputs")) {
        return {};
    }
    const std::optional<int> inputs = ReadInt(object, "inputs");
    if (!inputs || !Core::LogicPart::IsValidInputCount(*inputs)) {
        return std::unexpected(std::format("has a number of inputs that is not a whole number from {} to {}",
                                           Core::MIN_LOGIC_INPUTS, Core::MAX_LOGIC_INPUTS));
    }
    part.SetInputCount(*inputs);
    return {};
}

std::expected<void, std::string> ReadClockFigures(const nlohmann::json &object, Core::Component &component) {
    if (component.GetType() != Core::ComponentType::CLOCK) {
        return {};
    }
    return ReadPartParameters(object, static_cast<Core::Clock &>(component), CLOCK_KEYS);
}

std::expected<void, std::string> ReadLogicInputFigures(const nlohmann::json &object, Core::Component &component) {
    if (component.GetType() != Core::ComponentType::LOGIC_INPUT) {
        return {};
    }
    return ReadPartParameters(object, static_cast<Core::LogicInput &>(component), LOGIC_INPUT_KEYS);
}

std::expected<void, std::string> ReadLogicOutputFigures(const nlohmann::json &object, Core::Component &component) {
    if (component.GetType() != Core::ComponentType::LOGIC_OUTPUT) {
        return {};
    }
    return ReadPartParameters(object, static_cast<Core::LogicOutput &>(component), LOGIC_OUTPUT_KEYS);
}

constexpr auto FIGURE_READERS =
    std::to_array<StateReader>({&ReadSwitchFigures, &ReadRelayFigures, &ReadTransformerFigures, &ReadLogicFigures,
                                &ReadClockFigures, &ReadLogicInputFigures, &ReadLogicOutputFigures});

/**
 * @brief   Parts whose figures are all there is to them: the switch that follows a voltage, the relay, the transformer,
 *          the gates and flip-flops with their logic family, a clock and a logic input.
 */
std::expected<void, std::string> ReadFigureFields(const nlohmann::json &object, Core::Component &component) {
    for (const StateReader reader : FIGURE_READERS) {
        if (const auto read = reader(object, component); !read) {
            return std::unexpected(read.error());
        }
    }
    return {};
}

std::expected<void, std::string> ReadSubcircuitInstance(const nlohmann::json &object,
                                                        Core::SubcircuitInstance &instance,
                                                        const std::span<const SavedSubcircuit> definitions) {
    const std::optional<std::string> name = ReadString(object, "definition");
    if (!name || !Core::SubcircuitInstance::IsValidIdentifier(*name)) {
        return std::unexpected("has no valid subcircuit definition");
    }
    const auto definition = std::ranges::find(definitions, *name, &SavedSubcircuit::Name);
    if (definition == definitions.end()) {
        return std::unexpected(std::format("refers to unknown subcircuit \"{}\"", *name));
    }
    ApplyDefinitionPins(instance, *definition);
    if (const auto parameters = object.find("parameters"); parameters != object.end()) {
        if (!parameters->is_object()) {
            return std::unexpected("has subcircuit parameters that are not an object");
        }
        for (const auto &[parameter, value] : parameters->items()) {
            if (!value.is_number() ||
                !std::ranges::contains(definition->Parameters, parameter, &Core::SubcircuitParameter::Name)) {
                return std::unexpected(std::format("has no valid value for subcircuit parameter {}", parameter));
            }
            instance.SetParameter(parameter, value.get<double>());
        }
    }
    return {};
}

// Names are checked separately, once every element is read, so duplicates can be found
std::expected<std::unique_ptr<UIElement>, std::string>
ReadElement(const nlohmann::json &object, const std::span<const SavedSubcircuit> definitions = {}) {
    if (!object.is_object()) {
        return std::unexpected("is not an object");
    }
    const std::optional<std::string> type_name = ReadString(object, "type");
    if (!type_name) {
        return std::unexpected("has no type");
    }
    const std::optional<Core::ComponentType> type = Core::ParseComponentType(*type_name);
    if (!type) {
        return std::unexpected(std::format("has unknown type \"{}\"", *type_name));
    }
    const std::optional<int> x = ReadInt(object, "x");
    const std::optional<int> y = ReadInt(object, "y");
    if (!x || !y) {
        return std::unexpected("has no integer position");
    }
    const std::optional<int> degrees = ReadInt(object, "rotation");
    const std::optional<Rotation> rotation = degrees ? RotationFromDegrees(*degrees) : std::nullopt;
    if (!rotation) {
        return std::unexpected("has no rotation of 0, 90, 180 or 270");
    }

    std::unique_ptr<UIElement> element = CreateElement(*type, {*x, *y}, *rotation);
    if (object.contains("mirrored")) {
        const std::optional<bool> mirrored = ReadBool(object, "mirrored");
        if (!mirrored) {
            return std::unexpected("has a mirrored flag that is not true or false");
        }
        element->SetMirrored(*mirrored);
    }
    Core::Component &component = element->GetComponent();
    if (component.GetType() == Core::ComponentType::SUBCIRCUIT) {
        if (const auto read =
                ReadSubcircuitInstance(object, static_cast<Core::SubcircuitInstance &>(component), definitions);
            !read) {
            return std::unexpected(read.error());
        }
    }
    if (component.HasValue()) {
        if (const std::optional<double> value = ReadNumber(object, "value")) {
            if (!component.IsValidValue(*value)) {
                return std::unexpected("has no valid value");
            }
            component.SetValue(*value);
        } else if (const std::optional<std::string> expression = ReadString(object, "value")) {
            if (!Core::SubcircuitInstance::IsValidValueExpression(*expression)) {
                return std::unexpected("has no valid parameter expression");
            }
            if (!component.CanFollowParameter()) {
                return std::unexpected("follows a parameter, which only resistors, capacitors and inductors can");
            }
            component.SetValueExpression(*expression);
        } else {
            return std::unexpected("has no valid value");
        }
    }
    // A file saved before initial conditions had no key, so the part has none
    if (component.SupportsInitialCondition() && object.contains("ic")) {
        const std::optional<double> initial = ReadNumber(object, "ic");
        if (!initial) {
            return std::unexpected("has an initial condition that is not a number");
        }
        component.SetInitialCondition(*initial);
    }
    if (Core::IsSource(component.GetType())) {
        const std::expected<void, std::string> source = ReadSource(object, static_cast<Core::Source &>(component));
        if (!source) {
            return std::unexpected(source.error());
        }
    }
    if (const std::expected<void, std::string> model = ReadModel(object, component); !model) {
        return std::unexpected(model.error());
    }
    if (const std::expected<void, std::string> state = ReadStateFields(object, component); !state) {
        return std::unexpected(state.error());
    }
    if (const std::expected<void, std::string> figures = ReadFigureFields(object, component); !figures) {
        return std::unexpected(figures.error());
    }
    if (const std::optional<std::string> name = ReadString(object, "name")) {
        component.SetName(*name);
    }
    return element;
}

/**
 * @brief   Keeps valid, unique names and renames the rest, so the netlist never gets two components with one name.
 */
void FixNames(const std::vector<std::unique_ptr<UIElement>> &elements, std::vector<std::string> &warnings) {
    std::vector<std::string> used_names;
    std::vector<Core::Component *> to_rename;
    for (const auto &element : elements) {
        Core::Component &component = element->GetComponent();
        const std::string_view prefix = component.GetNamePrefix();
        if (prefix.empty()) {
            component.SetName("");
            continue;
        }
        const std::string &name = component.GetName();
        if (Core::IsValidName(name, prefix) && !std::ranges::contains(used_names, name)) {
            used_names.push_back(name);
        } else {
            to_rename.push_back(&component);
        }
    }
    for (Core::Component *component : to_rename) {
        std::string new_name = Core::NextComponentName(component->GetNamePrefix(), used_names);
        if (component->GetName().empty()) {
            warnings.push_back(std::format("A {} had no name and was named {}", component->GetTypeName(), new_name));
        } else {
            warnings.push_back(std::format("{} \"{}\" was renamed to {} because the name was invalid or repeated",
                                           component->GetTypeName(), component->GetName(), new_name));
        }
        used_names.push_back(new_name);
        component->SetName(std::move(new_name));
    }
}

nlohmann::json WriteSweepRange(const Core::SweepRange &range) {
    nlohmann::json object = {{"source", range.Source}};
    WriteParameters(SWEEP_RANGE_KEYS, range, object);
    return object;
}

/// File names of the analyses, in the order of the Analysis enum.
constexpr auto ANALYSIS_KEYS = std::to_array<std::string_view>(
    {"operating_point", "transient", "ac_sweep", "dc_sweep", "noise", "transfer_function", "truth_table"});

nlohmann::json WriteSettings(const SimulationSettings &settings) {
    nlohmann::json transient = nlohmann::json::object();
    WriteParameters(TRANSIENT_KEYS, settings.Transient, transient);
    transient["live_seconds"] = settings.LiveSeconds;
    nlohmann::json ac_sweep = nlohmann::json::object();
    WriteParameters(AC_SWEEP_KEYS, settings.ACSweep, ac_sweep);
    ac_sweep["points_per_decade"] = settings.ACSweep.PointsPerDecade;
    nlohmann::json noise = {{"source", settings.NoiseSource}};
    WriteParameters(AC_SWEEP_KEYS, settings.NoiseRange, noise);
    noise["points_per_decade"] = settings.NoiseRange.PointsPerDecade;
    if (settings.NoiseOutput) {
        noise["output"] = {settings.NoiseOutput->X, settings.NoiseOutput->Y};
    }
    nlohmann::json transfer_function = {{"source", settings.TransferFunctionSource}};
    if (settings.TransferFunctionOutput) {
        transfer_function["output"] = {settings.TransferFunctionOutput->X, settings.TransferFunctionOutput->Y};
    }
    const nlohmann::json truth_table = {{"inputs", settings.TruthTableInputs}};
    const ParameterStepSettings &step = settings.ParameterStep;
    nlohmann::json parameter_step = {
        {"enabled", step.Enabled}, {"part", step.Part}, {"uses_list", step.UsesList}, {"list", step.List}};
    WriteParameters(PARAMETER_STEP_KEYS, step, parameter_step);
    return {
        {"analysis", ANALYSIS_KEYS[static_cast<std::size_t>(settings.Selected)]},
        {"transient", transient},
        {"ac_sweep", ac_sweep},
        {"dc_sweep",
         {
             {"swept", WriteSweepRange(settings.SweptRange)},
             {"stepped", WriteSweepRange(settings.SteppedRange)},
             {"step_source", settings.StepSource},
         }},
        {"noise", noise},
        {"transfer_function", transfer_function},
        {"truth_table", truth_table},
        {"parameter_step", parameter_step},
    };
}

/**
 * @brief   The analysis readers below change only their own part of the settings; missing keys keep its value.
 */
std::expected<void, std::string> ReadTransientSettings(const nlohmann::json &object, SimulationSettings &settings) {
    if (const auto read = ReadParameters(TRANSIENT_KEYS, object, settings.Transient); !read) {
        return read;
    }
    if (settings.Transient.StopTime <= 0.0 || settings.Transient.TimeStep <= 0.0) {
        return std::unexpected("have a time that is not positive");
    }
    if (object.contains("live_seconds")) {
        const std::optional<double> live_seconds = ReadNumber(object, "live_seconds");
        if (!live_seconds || !IsValidLiveSeconds(*live_seconds)) {
            return std::unexpected("have no valid live_seconds");
        }
        settings.LiveSeconds = *live_seconds;
    }
    return {};
}

/**
 * @brief   The AC sweep and the noise analysis share the form of their frequency range.
 */
std::expected<void, std::string> ReadFrequencyRange(const nlohmann::json &object, Core::ACSweepSettings &range) {
    if (const auto read = ReadParameters(AC_SWEEP_KEYS, object, range); !read) {
        return read;
    }
    if (range.StartFrequency <= 0.0 || range.StopFrequency <= 0.0) {
        return std::unexpected("have a frequency that is not positive");
    }
    if (object.contains("points_per_decade")) {
        const std::optional<int> points = ReadInt(object, "points_per_decade");
        if (!points || *points < 1) {
            return std::unexpected("have no valid points_per_decade");
        }
        range.PointsPerDecade = *points;
    }
    return {};
}

std::expected<void, std::string> ReadACSweepSettings(const nlohmann::json &object, SimulationSettings &settings) {
    return ReadFrequencyRange(object, settings.ACSweep);
}

std::expected<void, std::string> ReadSweepRange(const nlohmann::json &object, Core::SweepRange &range) {
    if (!object.is_object()) {
        return std::unexpected("have a range that is not an object");
    }
    if (object.contains("source")) {
        const std::optional<std::string> source = ReadString(object, "source");
        if (!source) {
            return std::unexpected("have no valid source");
        }
        range.Source = *source;
    }
    if (const auto read = ReadParameters(SWEEP_RANGE_KEYS, object, range); !read) {
        return read;
    }
    if (range.Step == 0.0) {
        return std::unexpected("have a step of zero");
    }
    return {};
}

std::expected<void, std::string> ReadDCSweepSettings(const nlohmann::json &object, SimulationSettings &settings) {
    if (const auto swept = object.find("swept"); swept != object.end()) {
        if (const auto read = ReadSweepRange(*swept, settings.SweptRange); !read) {
            return read;
        }
    }
    if (const auto stepped = object.find("stepped"); stepped != object.end()) {
        if (const auto read = ReadSweepRange(*stepped, settings.SteppedRange); !read) {
            return read;
        }
    }
    if (object.contains("step_source")) {
        const std::optional<bool> step_source = ReadBool(object, "step_source");
        if (!step_source) {
            return std::unexpected("have a step_source flag that is not true or false");
        }
        settings.StepSource = *step_source;
    }
    return {};
}

std::expected<void, std::string> ReadNoiseSettings(const nlohmann::json &object, SimulationSettings &settings) {
    if (const auto read = ReadFrequencyRange(object, settings.NoiseRange); !read) {
        return read;
    }
    if (object.contains("source")) {
        const std::optional<std::string> source = ReadString(object, "source");
        if (!source) {
            return std::unexpected("have no valid source");
        }
        settings.NoiseSource = *source;
    }
    if (object.contains("output")) {
        const std::optional<GridPoint> output = ReadPoint(object, "output");
        if (!output) {
            return std::unexpected("have no valid output point");
        }
        settings.NoiseOutput = *output;
    }
    return {};
}

std::expected<void, std::string> ReadTransferFunctionSettings(const nlohmann::json &object,
                                                              SimulationSettings &settings) {
    if (object.contains("source")) {
        const std::optional<std::string> source = ReadString(object, "source");
        if (!source) {
            return std::unexpected("have no valid source");
        }
        settings.TransferFunctionSource = *source;
    }
    if (object.contains("output")) {
        const std::optional<GridPoint> output = ReadPoint(object, "output");
        if (!output) {
            return std::unexpected("have no valid output point");
        }
        settings.TransferFunctionOutput = *output;
    }
    return {};
}

std::expected<void, std::string> ReadTruthTableSettings(const nlohmann::json &object, SimulationSettings &settings) {
    const auto inputs = object.find("inputs");
    if (inputs == object.end()) {
        return {};
    }
    if (!inputs->is_array()) {
        return std::unexpected("have no valid input list");
    }
    std::vector<std::string> names;
    for (const nlohmann::json &name : *inputs) {
        if (!name.is_string()) {
            return std::unexpected("have an input that is not text");
        }
        names.push_back(name.get<std::string>());
    }
    settings.TruthTableInputs = std::move(names);
    return {};
}

std::expected<void, std::string> ReadParameterStepSettings(const nlohmann::json &object, SimulationSettings &settings) {
    ParameterStepSettings &step = settings.ParameterStep;
    if (const auto read = ReadParameters(PARAMETER_STEP_KEYS, object, step); !read) {
        return read;
    }
    for (const auto &[key, flag] : {std::pair{"enabled", &step.Enabled}, std::pair{"uses_list", &step.UsesList}}) {
        if (object.contains(key)) {
            const std::optional<bool> value = ReadBool(object, key);
            if (!value) {
                return std::unexpected(std::format("have a {} flag that is not true or false", key));
            }
            *flag = *value;
        }
    }
    for (const auto &[key, text] : {std::pair{"part", &step.Part}, std::pair{"list", &step.List}}) {
        if (object.contains(key)) {
            const std::optional<std::string> value = ReadString(object, key);
            if (!value) {
                return std::unexpected(std::format("have no valid {}", key));
            }
            *text = *value;
        }
    }
    return {};
}

/**
 * @struct  AnalysisReader
 * @brief   JSON key of the settings of one analysis, its name in warnings and the function that reads them.
 */
struct AnalysisReader {
    const char *Key;
    const char *Label;
    std::expected<void, std::string> (*Read)(const nlohmann::json &, SimulationSettings &);
};

constexpr auto ANALYSIS_READERS = std::to_array<AnalysisReader>({
    {"transient", "transient", &ReadTransientSettings},
    {"ac_sweep", "AC sweep", &ReadACSweepSettings},
    {"dc_sweep", "DC sweep", &ReadDCSweepSettings},
    {"noise", "noise", &ReadNoiseSettings},
    {"transfer_function", "transfer function", &ReadTransferFunctionSettings},
    {"truth_table", "truth table", &ReadTruthTableSettings},
    {"parameter_step", "parameter step", &ReadParameterStepSettings},
});

/**
 * @brief   Files saved before settings were stored have none and get the defaults; an analysis with invalid settings
 *          falls back to its defaults without affecting the others.
 */
SimulationSettings ReadSettings(const nlohmann::json &document, std::vector<std::string> &warnings) {
    SimulationSettings settings;
    const auto simulation = document.find("simulation");
    if (simulation == document.end()) {
        return settings;
    }
    if (!simulation->is_object()) {
        warnings.push_back("The simulation settings are not an object and were reset to the defaults");
        return settings;
    }
    if (const auto analysis = simulation->find("analysis"); analysis != simulation->end()) {
        const auto key = analysis->is_string() ? std::ranges::find(ANALYSIS_KEYS, analysis->get<std::string>())
                                               : ANALYSIS_KEYS.end();
        if (key != ANALYSIS_KEYS.end()) {
            settings.Selected = static_cast<Analysis>(key - ANALYSIS_KEYS.begin());
        } else {
            warnings.push_back("The selected analysis is unknown and was reset to the transient");
        }
    }
    for (const AnalysisReader &reader : ANALYSIS_READERS) {
        const auto object = simulation->find(reader.Key);
        if (object == simulation->end()) {
            continue;
        }
        SimulationSettings read_settings = settings;
        std::expected<void, std::string> read = std::unexpected("are not an object");
        if (object->is_object()) {
            read = reader.Read(*object, read_settings);
        }
        if (read) {
            settings = std::move(read_settings);
        } else {
            warnings.push_back(
                std::format("The {} settings {} and were reset to the defaults", reader.Label, read.error()));
        }
    }
    return settings;
}

/**
 * @brief   A channel keeps both its operation and its expression, so switching between them after opening loses
 *          nothing.
 */
nlohmann::json WriteMathChannel(const MathChannel &channel) {
    return {
        {"name", channel.Name},
        {"shown", channel.Shown},
        {"uses_expression", channel.UsesExpression},
        {"first", channel.First},
        {"operator", GetOperatorSymbol(channel.Operator)},
        {"second", channel.Second},
        {"expression", channel.Expression},
    };
}

nlohmann::json WriteBus(const LogicBus &bus) {
    return {
        {"name", bus.Name},
        {"shown", bus.Shown},
        {"bits", bus.Bits},
        {"format", GetBusFormatName(bus.Format)},
    };
}

nlohmann::json WriteMeasurements(const SavedMeasurements &measurements) {
    nlohmann::json voltages = nlohmann::json::array();
    for (const GridPoint point : measurements.Voltages) {
        voltages.push_back({point.X, point.Y});
    }
    nlohmann::json measurements_object = {{"voltages", voltages}, {"currents", measurements.Currents}};
    if (!measurements.MathChannels.empty()) {
        nlohmann::json channels = nlohmann::json::array();
        for (const MathChannel &channel : measurements.MathChannels) {
            channels.push_back(WriteMathChannel(channel));
        }
        measurements_object["math"] = channels;
    }
    if (!measurements.Buses.empty()) {
        nlohmann::json buses = nlohmann::json::array();
        for (const LogicBus &bus : measurements.Buses) {
            buses.push_back(WriteBus(bus));
        }
        measurements_object["buses"] = buses;
    }
    return measurements_object;
}

/**
 * @brief   Reads every entry of a list that read_entry accepts.
 * @details Returns false when the key holds no list or some entry was skipped; a missing key is an empty list.
 */
template <typename Value, typename ReadEntry>
bool ReadList(const nlohmann::json &object, const char *key, const ReadEntry &read_entry, std::vector<Value> &values) {
    const auto list = object.find(key);
    if (list == object.end()) {
        return true;
    }
    if (!list->is_array()) {
        return false;
    }
    bool complete = true;
    for (const nlohmann::json &entry : *list) {
        if (const std::optional<Value> value = read_entry(entry)) {
            values.push_back(*value);
        } else {
            complete = false;
        }
    }
    return complete;
}

std::optional<std::string> ReadStringValue(const nlohmann::json &value) {
    if (!value.is_string()) {
        return std::nullopt;
    }
    return value.get<std::string>();
}

/**
 * @brief   A channel needs its name; the other fields fall back to those of a new channel, but an operator that is not
 *          one of the four rejects the channel, since it would compute something else.
 */
std::optional<MathChannel> ReadMathChannelValue(const nlohmann::json &value) {
    if (!value.is_object()) {
        return std::nullopt;
    }
    const std::optional<std::string> name = ReadString(value, "name");
    if (!name || name->empty()) {
        return std::nullopt;
    }
    MathChannel channel{.Name = *name};
    channel.Shown = ReadBool(value, "shown").value_or(channel.Shown);
    channel.UsesExpression = ReadBool(value, "uses_expression").value_or(channel.UsesExpression);
    channel.First = ReadString(value, "first").value_or("");
    channel.Second = ReadString(value, "second").value_or("");
    channel.Expression = ReadString(value, "expression").value_or("");
    if (const std::optional<std::string> symbol = ReadString(value, "operator")) {
        const std::optional<MathOperator> math_operator = ParseOperatorSymbol(*symbol);
        if (!math_operator) {
            return std::nullopt;
        }
        channel.Operator = *math_operator;
    }
    return channel;
}

/**
 * @brief   A bus needs its name and a list of bit names; a format it does not know rejects it, since it would show
 *          other values.
 */
std::optional<LogicBus> ReadBusValue(const nlohmann::json &value) {
    if (!value.is_object()) {
        return std::nullopt;
    }
    const std::optional<std::string> name = ReadString(value, "name");
    const auto bits = value.find("bits");
    if (!name || name->empty() || bits == value.end() || !bits->is_array()) {
        return std::nullopt;
    }
    LogicBus bus{.Name = *name};
    bus.Shown = ReadBool(value, "shown").value_or(bus.Shown);
    for (const nlohmann::json &bit : *bits) {
        if (!bit.is_string()) {
            return std::nullopt;
        }
        bus.Bits.push_back(bit.get<std::string>());
    }
    if (const std::optional<std::string> format_name = ReadString(value, "format")) {
        const std::optional<BusFormat> format = ParseBusFormat(*format_name);
        if (!format) {
            return std::nullopt;
        }
        bus.Format = *format;
    }
    return bus;
}

/**
 * @brief   Entries that cannot be read are skipped with one warning per list, since a lost trace is easy to probe
 *          again.
 */
SavedMeasurements ReadMeasurements(const nlohmann::json &document, std::vector<std::string> &warnings) {
    SavedMeasurements measurements;
    const auto object = document.find("measurements");
    if (object == document.end()) {
        return measurements;
    }
    if (!object->is_object()) {
        warnings.push_back("The measurements are not an object and were skipped");
        return measurements;
    }
    if (!ReadList(*object, "voltages", ReadPointValue, measurements.Voltages)) {
        warnings.push_back("Some measured voltages are not integer points and were skipped");
    }
    if (!ReadList(*object, "currents", ReadStringValue, measurements.Currents)) {
        warnings.push_back("Some measured currents are not names and were skipped");
    }
    if (!ReadList(*object, "math", ReadMathChannelValue, measurements.MathChannels)) {
        warnings.push_back("Some math channels could not be read and were skipped");
    }
    if (!ReadList(*object, "buses", ReadBusValue, measurements.Buses)) {
        warnings.push_back("Some logic buses could not be read and were skipped");
    }
    return measurements;
}

nlohmann::json WriteElements(const std::span<const UIElement *const> elements) {
    nlohmann::json list = nlohmann::json::array();
    for (const UIElement *element : elements) {
        list.push_back(WriteElement(*element));
    }
    return list;
}

nlohmann::json WriteWires(const std::vector<UIWire> &wires) {
    nlohmann::json list = nlohmann::json::array();
    for (const UIWire &wire : wires) {
        list.push_back({
            {"start", {wire.GetStart().X, wire.GetStart().Y}},
            {"end", {wire.GetEnd().X, wire.GetEnd().Y}},
        });
    }
    return list;
}

nlohmann::json WriteSubcircuit(const SavedSubcircuit &definition) {
    nlohmann::json pins = nlohmann::json::array();
    for (const SavedSubcircuitPin &pin : definition.Pins) {
        if (definition.Spice) {
            pins.push_back({{"name", pin.Name}, {"side", GetPinSideName(pin.Side)}});
        } else {
            pins.push_back({{"name", pin.Name}, {"point", {pin.Point.X, pin.Point.Y}}});
        }
    }
    nlohmann::json parameters = nlohmann::json::array();
    for (const Core::SubcircuitParameter &parameter : definition.Parameters) {
        parameters.push_back({{"name", parameter.Name}, {"default", parameter.DefaultValue}});
    }
    // A block kept as SPICE text is its ports and its text, which the file keeps as they came
    if (definition.Spice) {
        return {{"name", definition.Name},
                {"pins", pins},
                {"parameters", parameters},
                {"spice", {{"ports", definition.Spice->Ports}, {"text", definition.Spice->Text}}}};
    }
    std::vector<const UIElement *> elements;
    for (const auto &element : definition.Elements) {
        elements.push_back(element.get());
    }
    return {{"name", definition.Name},
            {"pins", pins},
            {"parameters", parameters},
            {"elements", WriteElements(elements)},
            {"wires", WriteWires(definition.Wires)}};
}

std::string WriteDocument(const std::span<const UIElement *const> elements, const std::vector<UIWire> &wires,
                          const SimulationSettings &settings, const SavedMeasurements &measurements,
                          const std::span<const SavedSubcircuit> subcircuits = {},
                          const std::span<const Core::LibraryModel> models = {}) {
    nlohmann::json document = {
        {"format", FORMAT_NAME},
        {"version", FORMAT_VERSION},
        {"elements", WriteElements(elements)},
        {"wires", WriteWires(wires)},
        {"simulation", WriteSettings(settings)},
    };
    if (!subcircuits.empty()) {
        document["subcircuits"] = nlohmann::json::array();
        for (const SavedSubcircuit &definition : subcircuits) {
            document["subcircuits"].push_back(WriteSubcircuit(definition));
        }
    }
    // A model is its text, from which its name and kind are read back
    if (!models.empty()) {
        document["models"] = nlohmann::json::array();
        for (const Core::LibraryModel &model : models) {
            document["models"].push_back({{"text", model.Text}});
        }
    }
    if (!measurements.Voltages.empty() || !measurements.Currents.empty() || !measurements.MathChannels.empty() ||
        !measurements.Buses.empty()) {
        document["measurements"] = WriteMeasurements(measurements);
    }
    return document.dump(2) + "\n";
}

/**
 * @brief   One pin of a block: a drawn one sits at the point of its marker, one of SPICE text keeps the edge it goes
 *          on.
 */
std::expected<SavedSubcircuitPin, std::string> ReadSubcircuitPin(const nlohmann::json &pin, const bool is_text) {
    const std::optional<std::string> name = pin.is_object() ? ReadString(pin, "name") : std::nullopt;
    if (!name || !Core::SubcircuitInstance::IsValidIdentifier(*name)) {
        return std::unexpected("has an invalid or repeated pin");
    }
    if (is_text) {
        const std::optional<std::string> side_name = ReadString(pin, "side");
        const std::optional<Core::PinSide> side = side_name ? ParsePinSide(*side_name) : std::nullopt;
        if (!side) {
            return std::unexpected(std::format("has no valid edge for pin {}", *name));
        }
        return SavedSubcircuitPin{.Name = *name, .Point = {}, .Side = *side};
    }
    const std::optional<GridPoint> point = ReadPoint(pin, "point");
    if (!point) {
        return std::unexpected("has an invalid or repeated pin");
    }
    return SavedSubcircuitPin{.Name = *name, .Point = *point};
}

/**
 * @brief   The pins of a block, in order, each with a name of its own.
 */
std::expected<void, std::string> ReadSubcircuitPins(const nlohmann::json &object, const bool is_text,
                                                    std::vector<SavedSubcircuitPin> &pins) {
    const auto list = object.find("pins");
    if (list == object.end()) {
        return {};
    }
    if (!list->is_array()) {
        return std::unexpected("has pins that are not a list");
    }
    for (const nlohmann::json &entry : *list) {
        std::expected<SavedSubcircuitPin, std::string> pin = ReadSubcircuitPin(entry, is_text);
        if (!pin) {
            return std::unexpected(pin.error());
        }
        if (std::ranges::contains(pins, pin->Name, &SavedSubcircuitPin::Name)) {
            return std::unexpected("has an invalid or repeated pin");
        }
        pins.push_back(std::move(*pin));
    }
    return {};
}

/**
 * @brief   The ports and text of a block kept as SPICE text; each pin is the port of the same place.
 */
std::expected<SavedSubcircuit, std::string> ReadSubcircuitText(const nlohmann::json &object,
                                                               SavedSubcircuit definition) {
    if (!object.is_object()) {
        return std::unexpected("has SPICE text that is not an object");
    }
    const auto ports = object.find("ports");
    const std::optional<std::string> text = ReadString(object, "text");
    if (ports == object.end() || !ports->is_array() || !text) {
        return std::unexpected("has no SPICE ports or text");
    }
    Core::SubcircuitText spice{.Ports = {}, .Text = *text};
    for (const nlohmann::json &port : *ports) {
        if (!port.is_string() || port.get<std::string>().empty() ||
            port.get<std::string>().find_first_of(" \t\n") != std::string::npos) {
            return std::unexpected("has a SPICE port that is not one word");
        }
        spice.Ports.push_back(port.get<std::string>());
    }
    if (spice.Ports.size() != definition.Pins.size()) {
        return std::unexpected("has a different number of pins and SPICE ports");
    }
    definition.Spice = std::move(spice);
    return definition;
}

/**
 * @brief   Reads one block definition; the blocks its parts place must be read already, since their instances take
 *          their pins from them.
 */
std::expected<SavedSubcircuit, std::string> ReadSubcircuit(const nlohmann::json &object,
                                                           std::vector<std::string> &warnings,
                                                           const std::span<const SavedSubcircuit> definitions) {
    if (!object.is_object()) {
        return std::unexpected("is not an object");
    }
    SavedSubcircuit definition;
    const std::optional<std::string> name = ReadString(object, "name");
    if (!name || !Core::SubcircuitInstance::IsValidIdentifier(*name)) {
        return std::unexpected("has no valid name");
    }
    definition.Name = *name;
    const bool is_text = object.contains("spice");
    if (auto read = ReadSubcircuitPins(object, is_text, definition.Pins); !read) {
        return std::unexpected(read.error());
    }
    const auto parameters = object.find("parameters");
    if (parameters != object.end() && !parameters->is_array()) {
        return std::unexpected("has parameters that are not a list");
    }
    if (parameters != object.end()) {
        for (const nlohmann::json &parameter : *parameters) {
            const std::optional<std::string> parameter_name =
                parameter.is_object() ? ReadString(parameter, "name") : std::nullopt;
            const std::optional<double> value = parameter.is_object() ? ReadNumber(parameter, "default") : std::nullopt;
            if (!parameter_name || !Core::SubcircuitInstance::IsValidIdentifier(*parameter_name) || !value ||
                std::ranges::contains(definition.Parameters, *parameter_name, &Core::SubcircuitParameter::Name)) {
                return std::unexpected("has an invalid or repeated parameter");
            }
            definition.Parameters.push_back({*parameter_name, *value});
        }
    }
    if (is_text) {
        return ReadSubcircuitText(object.at("spice"), std::move(definition));
    }

    std::vector<std::string> local_warnings;
    const auto elements = object.find("elements");
    if (elements != object.end() && elements->is_array()) {
        for (std::size_t index = 0; index < elements->size(); ++index) {
            auto element = ReadElement((*elements)[index], definitions);
            if (element) {
                definition.Elements.push_back(std::move(*element));
            } else {
                local_warnings.push_back(std::format("Element {} {} and was skipped", index + 1, element.error()));
            }
        }
    }
    FixNames(definition.Elements, local_warnings);
    const auto wires = object.find("wires");
    if (wires != object.end() && wires->is_array()) {
        for (std::size_t index = 0; index < wires->size(); ++index) {
            const nlohmann::json &wire = (*wires)[index];
            const std::optional<GridPoint> start = wire.is_object() ? ReadPoint(wire, "start") : std::nullopt;
            const std::optional<GridPoint> end = wire.is_object() ? ReadPoint(wire, "end") : std::nullopt;
            if (!start || !end || *start == *end || (start->X != end->X && start->Y != end->Y)) {
                local_warnings.push_back(std::format("Wire {} is invalid and was skipped", index + 1));
            } else {
                definition.Wires.emplace_back(*start, *end);
            }
        }
    }
    if (const std::optional<std::string> problem =
            CheckPinNodes(Connectivity(definition.Elements, definition.Wires), definition.Pins)) {
        return std::unexpected(std::format("has pins it cannot connect: {}", *problem));
    }
    for (std::string &warning : local_warnings) {
        warnings.push_back(std::format("Subcircuit {}: {}", definition.Name, warning));
    }
    return definition;
}

std::vector<Core::LibraryModel> ReadLibraryModels(const nlohmann::json &document, std::vector<std::string> &warnings) {
    std::vector<Core::LibraryModel> models;
    const auto list = document.find("models");
    if (list == document.end()) {
        return models;
    }
    if (!list->is_array()) {
        warnings.emplace_back("The library models are not a list and were skipped");
        return models;
    }
    for (std::size_t index = 0; index < list->size(); ++index) {
        const nlohmann::json &entry = (*list)[index];
        const std::optional<std::string> text = entry.is_object() ? ReadString(entry, "text") : std::nullopt;
        std::optional<Core::LibraryModel> model = text ? Core::ReadLibraryModel(*text) : std::nullopt;
        if (!model) {
            warnings.push_back(
                std::format("Library model {} is not a diode or transistor .model and was skipped", index + 1));
        } else if (std::ranges::contains(models, model->Name, &Core::LibraryModel::Name)) {
            warnings.push_back(std::format("Library model {} is repeated and was skipped", model->Name));
        } else {
            models.push_back(std::move(*model));
        }
    }
    return models;
}

/**
 * @brief   The names of the blocks the parts of a block definition place, as the file lists them.
 */
std::set<std::string> ListPlacedDefinitions(const nlohmann::json &object) {
    std::set<std::string> names;
    const auto elements = object.is_object() ? object.find("elements") : object.end();
    if (elements == object.end() || !elements->is_array()) {
        return names;
    }
    for (const nlohmann::json &element : *elements) {
        if (const std::optional<std::string> name =
                element.is_object() ? ReadString(element, "definition") : std::nullopt) {
            names.insert(*name);
        }
    }
    return names;
}

/**
 * @brief   The order to read the definitions in, so each block is read after the blocks it places, whatever order the
 *          file lists them in; a block in a cycle, which only a damaged file can hold, comes after the others it can.
 */
std::vector<std::size_t> OrderDefinitionsForReading(const nlohmann::json &list) {
    std::map<std::string, std::size_t, std::less<>> first_of_name;
    for (std::size_t index = 0; index < list.size(); ++index) {
        if (const std::optional<std::string> name =
                list[index].is_object() ? ReadString(list[index], "name") : std::nullopt) {
            first_of_name.try_emplace(*name, index);
        }
    }
    std::vector<std::size_t> order;
    std::vector<bool> is_placed(list.size(), false);
    std::vector<bool> is_open(list.size(), false);
    const std::function<void(std::size_t)> place = [&](const std::size_t index) {
        if (is_placed[index] || is_open[index]) {
            return;
        }
        is_open[index] = true;
        for (const std::string &used : ListPlacedDefinitions(list[index])) {
            if (const auto inner = first_of_name.find(used); inner != first_of_name.end()) {
                place(inner->second);
            }
        }
        is_open[index] = false;
        is_placed[index] = true;
        order.push_back(index);
    };
    for (std::size_t index = 0; index < list.size(); ++index) {
        place(index);
    }
    return order;
}

/**
 * @brief   Reads every block definition, each after the blocks it places, and keeps them in the order of the file.
 */
std::vector<SavedSubcircuit> ReadSubcircuits(const nlohmann::json &document, std::vector<std::string> &warnings) {
    std::vector<SavedSubcircuit> definitions;
    const auto list = document.find("subcircuits");
    if (list == document.end()) {
        return definitions;
    }
    if (!list->is_array()) {
        warnings.push_back("The subcircuit definitions are not a list and were skipped");
        return definitions;
    }
    std::vector<std::size_t> file_positions;
    for (const std::size_t index : OrderDefinitionsForReading(*list)) {
        auto definition = ReadSubcircuit((*list)[index], warnings, definitions);
        if (!definition) {
            warnings.push_back(std::format("Subcircuit {} {} and was skipped", index + 1, definition.error()));
        } else if (const auto existing = std::ranges::find(definitions, definition->Name, &SavedSubcircuit::Name);
                   existing != definitions.end()) {
            // The first in the file wins, whichever was read first
            warnings.push_back(std::format("Subcircuit {} is repeated and was skipped", definition->Name));
            const auto position = static_cast<std::size_t>(existing - definitions.begin());
            if (index < file_positions[position]) {
                *existing = std::move(*definition);
                file_positions[position] = index;
            }
        } else {
            definitions.push_back(std::move(*definition));
            file_positions.push_back(index);
        }
    }
    // Back to the order of the file, so saving writes them where they were
    std::vector<std::size_t> by_file(definitions.size());
    for (std::size_t position = 0; position < by_file.size(); ++position) {
        by_file[position] = position;
    }
    std::ranges::sort(by_file, {}, [&](const std::size_t position) { return file_positions[position]; });
    std::vector<SavedSubcircuit> ordered;
    for (const std::size_t position : by_file) {
        ordered.push_back(std::move(definitions[position]));
    }
    return ordered;
}

} // namespace

/**
 * @brief   Writes a schematic as JSON.
 * @param[in] elements      Components placed on the grid.
 * @param[in] wires         Wire segments.
 * @param[in] settings      Settings of the analyses.
 * @param[in] measurements  Traces the plots show; the key is left out when there are none.
 * @param[in] subcircuits   Project-local definitions, including their internal elements and wires.
 * @param[in] models        Device models imported from libraries, as their text.
 * @return  The file contents, indented for reading and diffing. Values are plain numbers, never suffixed text, except
 *          for subcircuit parameter references saved as strings such as "{RESISTANCE}".
 */
std::string SaveSchematic(const std::vector<std::unique_ptr<UIElement>> &elements, const std::vector<UIWire> &wires,
                          const SimulationSettings &settings, const SavedMeasurements &measurements,
                          const std::span<const SavedSubcircuit> subcircuits,
                          const std::span<const Core::LibraryModel> models) {
    std::vector<const UIElement *> pointers;
    for (const auto &element : elements) {
        pointers.push_back(element.get());
    }
    return WriteDocument(pointers, wires, settings, measurements, subcircuits, models);
}

/**
 * @brief   Writes some parts and wires as a schematic of their own, for the clipboard.
 * @param[in] elements  Parts to write, which stay where they are.
 * @param[in] wires        Wire segments to write.
 * @param[in] subcircuits  Definitions referenced by the copied instances.
 * @param[in] models       Library models the copied parts use.
 * @return  The same JSON a file holds, with the default settings and no measurements, so it loads back with
 *          LoadSchematic().
 */
std::string SaveParts(const std::span<const UIElement *const> elements, const std::vector<UIWire> &wires,
                      const std::span<const SavedSubcircuit> subcircuits,
                      const std::span<const Core::LibraryModel> models) {
    return WriteDocument(elements, wires, {}, {}, subcircuits, models);
}

/**
 * @brief   Reads a schematic from JSON.
 * @param[in] text  File contents.
 * @return  The schematic, or an error when the text is not a schematic this version can read. Elements and
 *          wires that cannot be read, such as diagonal wires, are skipped with a warning instead of failing
 *          the whole file, and so are repeated or missing names, which are replaced. Invalid analysis settings
 *          fall back to their defaults, also with a warning.
 */
std::expected<LoadedSchematic, std::string> LoadSchematic(const std::string_view text) {
    const nlohmann::json document = nlohmann::json::parse(text, nullptr, false);
    if (document.is_discarded() || !document.is_object()) {
        return std::unexpected("The file is not valid JSON");
    }
    if (ReadString(document, "format") != FORMAT_NAME) {
        return std::unexpected("The file is not an imcsim schematic");
    }
    const std::optional<int> version = ReadInt(document, "version");
    if (!version || *version < 1) {
        return std::unexpected("The file has no valid format version");
    }
    if (*version > FORMAT_VERSION) {
        return std::unexpected(std::format("The file uses format version {}, but this build only reads up to {}",
                                           *version, FORMAT_VERSION));
    }

    LoadedSchematic schematic;
    schematic.Models = ReadLibraryModels(document, schematic.Warnings);
    schematic.Subcircuits = ReadSubcircuits(document, schematic.Warnings);
    const auto elements = document.find("elements");
    if (elements != document.end() && elements->is_array()) {
        for (std::size_t index = 0; index < elements->size(); ++index) {
            auto element = ReadElement((*elements)[index], schematic.Subcircuits);
            if (element) {
                schematic.Elements.push_back(std::move(*element));
            } else {
                schematic.Warnings.push_back(std::format("Element {} {} and was skipped", index + 1, element.error()));
            }
        }
    }
    FixNames(schematic.Elements, schematic.Warnings);

    const auto wires = document.find("wires");
    if (wires != document.end() && wires->is_array()) {
        for (std::size_t index = 0; index < wires->size(); ++index) {
            const nlohmann::json &wire = (*wires)[index];
            const std::optional<GridPoint> start = wire.is_object() ? ReadPoint(wire, "start") : std::nullopt;
            const std::optional<GridPoint> end = wire.is_object() ? ReadPoint(wire, "end") : std::nullopt;
            if (!start || !end) {
                schematic.Warnings.push_back(
                    std::format("Wire {} has no integer start and end and was skipped", index + 1));
            } else if (*start == *end) {
                schematic.Warnings.push_back(std::format("Wire {} has zero length and was skipped", index + 1));
            } else if (start->X != end->X && start->Y != end->Y) {
                schematic.Warnings.push_back(
                    std::format("Wire {} from ({}, {}) to ({}, {}) is diagonal and was skipped; redraw it", index + 1,
                                start->X, start->Y, end->X, end->Y));
            } else {
                schematic.Wires.emplace_back(*start, *end);
            }
        }
    }
    schematic.Settings = ReadSettings(document, schematic.Warnings);
    schematic.Measurements = ReadMeasurements(document, schematic.Warnings);
    return schematic;
}

/**
 * @brief   Reads a whole text file.
 * @param[in] path  File to read.
 * @return  The contents, or an error message naming the file.
 */
std::expected<std::string, std::string> ReadTextFile(const std::filesystem::path &path) {
    const std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::unexpected(std::format("Could not open {}", path.string()));
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    if (file.bad()) {
        return std::unexpected(std::format("Could not read {}", path.string()));
    }
    return contents.str();
}

/**
 * @brief   Writes a whole text file, replacing it if it exists.
 * @param[in] path  File to write.
 * @param[in] text  Contents.
 * @return  Nothing on success, or an error message naming the file.
 */
std::expected<void, std::string> WriteTextFile(const std::filesystem::path &path, const std::string_view text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return std::unexpected(std::format("Could not create {}", path.string()));
    }
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!file) {
        return std::unexpected(std::format("Could not write {}", path.string()));
    }
    return {};
}

} // namespace GUI
