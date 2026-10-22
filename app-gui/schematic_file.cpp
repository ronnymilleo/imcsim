/**
 * @file    schematic_file.cpp
 * @brief   Saves schematics to JSON and loads them back, reporting the parts that could not be read.
 */

#include "schematic_file.h"

#include "components/bjt.h"
#include "components/clock.h"
#include "components/controlled_source.h"
#include "components/diode.h"
#include "components/logic_input.h"
#include "components/logic_part.h"
#include "components/mosfet.h"
#include "components/net_label.h"
#include "components/op_amp.h"
#include "components/potentiometer.h"
#include "components/relay.h"
#include "components/source.h"
#include "components/switch.h"
#include "components/transformer.h"
#include "components/voltage_controlled_switch.h"
#include "element_factory.h"
#include "nlohmann/json.hpp"
#include <array>
#include <format>
#include <fstream>
#include <optional>
#include <sstream>
#include <utility>

namespace GUI {

namespace {

constexpr std::string_view FormatName = "imcsim-schematic";
constexpr int FormatVersion = 1;

/**
 * @struct  ParameterKey
 * @brief   JSON key of one member of a parameter set, such as Core::PulseParameters, and the member itself.
 */
template <typename Parameters> struct ParameterKey {
    const char *Name;
    double Parameters::*Value;
};

// The sine keys sit in the element object itself, where the first files with AC sources put them
constexpr auto ACKeys = std::to_array<ParameterKey<Core::ACParameters>>({
    {"amplitude", &Core::ACParameters::Amplitude},
    {"frequency", &Core::ACParameters::Frequency},
    {"offset", &Core::ACParameters::Offset},
    {"ac_magnitude", &Core::ACParameters::Magnitude},
});

constexpr auto SwitchKeys = std::to_array<ParameterKey<Core::SwitchParameters>>({
    {"threshold", &Core::SwitchParameters::Threshold},
    {"hysteresis", &Core::SwitchParameters::Hysteresis},
    {"on_resistance", &Core::SwitchParameters::OnResistance},
    {"off_resistance", &Core::SwitchParameters::OffResistance},
});

constexpr auto RelayKeys = std::to_array<ParameterKey<Core::RelayParameters>>({
    {"coil_resistance", &Core::RelayParameters::CoilResistance},
    {"coil_inductance", &Core::RelayParameters::CoilInductance},
    {"pull_in", &Core::RelayParameters::PullInVoltage},
    {"drop_out", &Core::RelayParameters::DropOutVoltage},
    {"contact_resistance", &Core::RelayParameters::ContactResistance},
});

constexpr auto TransformerKeys = std::to_array<ParameterKey<Core::TransformerParameters>>({
    {"turns_ratio", &Core::TransformerParameters::TurnsRatio},
    {"coupling", &Core::TransformerParameters::Coupling},
    {"winding_resistance", &Core::TransformerParameters::WindingResistance},
});

constexpr auto LogicKeys = std::to_array<ParameterKey<Core::LogicParameters>>({
    {"logic_level", &Core::LogicParameters::LogicLevel},
    {"propagation_delay", &Core::LogicParameters::PropagationDelay},
});

constexpr auto ClockKeys = std::to_array<ParameterKey<Core::ClockParameters>>({
    {"frequency", &Core::ClockParameters::Frequency},
    {"duty_cycle", &Core::ClockParameters::DutyCycle},
    {"logic_level", &Core::ClockParameters::LogicLevel},
    {"start_delay", &Core::ClockParameters::StartDelay},
});

constexpr auto LogicInputKeys = std::to_array<ParameterKey<Core::LogicInputParameters>>({
    {"logic_level", &Core::LogicInputParameters::LogicLevel},
});

constexpr auto PulseKeys = std::to_array<ParameterKey<Core::PulseParameters>>({
    {"low", &Core::PulseParameters::Low},
    {"high", &Core::PulseParameters::High},
    {"delay", &Core::PulseParameters::Delay},
    {"rise", &Core::PulseParameters::RiseTime},
    {"fall", &Core::PulseParameters::FallTime},
    {"width", &Core::PulseParameters::Width},
    {"period", &Core::PulseParameters::Period},
});

// Named after the SPICE EXP parameters
constexpr auto EXPKeys = std::to_array<ParameterKey<Core::EXPParameters>>({
    {"v1", &Core::EXPParameters::InitialValue},
    {"v2", &Core::EXPParameters::PeakValue},
    {"td1", &Core::EXPParameters::RiseDelay},
    {"tau1", &Core::EXPParameters::RiseTimeConstant},
    {"td2", &Core::EXPParameters::FallDelay},
    {"tau2", &Core::EXPParameters::FallTimeConstant},
});

constexpr auto SFFMKeys = std::to_array<ParameterKey<Core::SFFMParameters>>({
    {"offset", &Core::SFFMParameters::Offset},
    {"amplitude", &Core::SFFMParameters::Amplitude},
    {"carrier", &Core::SFFMParameters::CarrierFrequency},
    {"mdi", &Core::SFFMParameters::ModulationIndex},
    {"signal", &Core::SFFMParameters::SignalFrequency},
});

constexpr auto AMKeys = std::to_array<ParameterKey<Core::AMParameters>>({
    {"amplitude", &Core::AMParameters::Amplitude},
    {"offset", &Core::AMParameters::Offset},
    {"mod_frequency", &Core::AMParameters::ModulatingFrequency},
    {"carrier", &Core::AMParameters::CarrierFrequency},
    {"delay", &Core::AMParameters::Delay},
});

// Named after the SPICE diode parameters
constexpr auto DiodeKeys = std::to_array<ParameterKey<Core::DiodeParameters>>({
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

constexpr auto BJTKeys = std::to_array<ParameterKey<Core::BJTParameters>>({
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

constexpr auto MOSFETKeys = std::to_array<ParameterKey<Core::MOSFETParameters>>({
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

constexpr auto OpAmpKeys = std::to_array<ParameterKey<Core::OpAmpParameters>>({
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

constexpr auto TransientKeys = std::to_array<ParameterKey<Core::TransientSettings>>({
    {"stop", &Core::TransientSettings::StopTime},
    {"step", &Core::TransientSettings::TimeStep},
});

// The points per decade are an integer, so they are written and read apart
constexpr auto ACSweepKeys = std::to_array<ParameterKey<Core::ACSweepSettings>>({
    {"start", &Core::ACSweepSettings::StartFrequency},
    {"stop", &Core::ACSweepSettings::StopFrequency},
});

constexpr auto ParameterStepKeys = std::to_array<ParameterKey<ParameterStepSettings>>({
    {"start", &ParameterStepSettings::Start},
    {"stop", &ParameterStepSettings::Stop},
    {"step", &ParameterStepSettings::Step},
});

constexpr auto SweepRangeKeys = std::to_array<ParameterKey<Core::SweepRange>>({
    {"start", &Core::SweepRange::Start},
    {"stop", &Core::SweepRange::Stop},
    {"step", &Core::SweepRange::Step},
});

// Readers return no value when the key is missing or holds another type, so a damaged file never throws
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

// One waveform of a source, such as its pulse, under its own key so the sine keys in the element object stay apart
template <typename Parameters, std::size_t Count>
void WriteSourceParameters(const std::array<ParameterKey<Parameters>, Count> &keys, const Parameters &parameters,
                           const char *name, nlohmann::json &object) {
    nlohmann::json values = nlohmann::json::object();
    WriteParameters(keys, parameters, values);
    object[name] = values;
}

// Missing keys keep the value the parameters already have; a key with something other than a number is an error
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

// One waveform of a source; nothing to read when the file has no key for it
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

// Parts whose parameters are all there is to them save them as one object
template <typename Part, typename Parameters, std::size_t Count>
void WritePartParameters(const Part &part, const std::array<ParameterKey<Parameters>, Count> &keys,
                         nlohmann::json &object) {
    nlohmann::json parameters = nlohmann::json::object();
    WriteParameters(keys, part.GetParameters(), parameters);
    object["parameters"] = parameters;
}

// Missing parameters keep those the part starts with
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

// Parts with a model save its name, plus their parameters when custom
template <typename Part, typename Parameters, std::size_t Count>
void WriteModelChoice(const Part &part, const std::array<ParameterKey<Parameters>, Count> &keys,
                      nlohmann::json &object) {
    object["model"] = part.GetModelName();
    if (part.IsCustom()) {
        nlohmann::json parameters = nlohmann::json::object();
        WriteParameters(keys, part.GetParameters(), parameters);
        object["parameters"] = parameters;
    }
}

// Parts with a ready model or custom parameters; the ideal op-amp also keeps its bandwidth
void WriteModelFields(const Core::Component &component, nlohmann::json &object) {
    const Core::ComponentType type = component.GetType();
    if (Core::IsDiode(type)) {
        WriteModelChoice(static_cast<const Core::Diode &>(component), DiodeKeys, object);
    } else if (Core::IsBJT(type)) {
        WriteModelChoice(static_cast<const Core::BJT &>(component), BJTKeys, object);
    } else if (Core::IsMOSFET(type)) {
        WriteModelChoice(static_cast<const Core::MOSFET &>(component), MOSFETKeys, object);
    } else if (type == Core::ComponentType::OpAmp) {
        const auto &op_amp = static_cast<const Core::OpAmp &>(component);
        WriteModelChoice(op_amp, OpAmpKeys, object);
        if (op_amp.IsIdeal()) {
            object["gbw"] = op_amp.GetIdealBandwidth();
        }
    }
}

// The state or figures of the other parts that have any
void WriteKindFields(const Core::Component &component, nlohmann::json &object) {
    const Core::ComponentType type = component.GetType();
    if (Core::IsCurrentControlled(type)) {
        object["control"] = static_cast<const Core::ControlledSource &>(component).GetControllingCurrent();
    } else if (type == Core::ComponentType::NetLabel) {
        // A label with no name yet is saved without the key
        if (const std::string &net = static_cast<const Core::NetLabel &>(component).GetNet(); !net.empty()) {
            object["net"] = net;
        }
    } else if (Core::IsSwitch(type)) {
        object["closed"] = static_cast<const Core::Switch &>(component).IsClosed();
    } else if (type == Core::ComponentType::LogicInput) {
        object["high"] = static_cast<const Core::LogicInput &>(component).IsHigh();
    } else if (type == Core::ComponentType::Potentiometer) {
        object["wiper"] = static_cast<const Core::Potentiometer &>(component).GetWiper();
    }
}

// Parts whose figures are all there is to them: the switch that follows a voltage, the relay, the transformer, the
// gates and flip-flops with their logic family, a clock and a logic input
void WriteFigureFields(const Core::Component &component, nlohmann::json &object) {
    const Core::ComponentType type = component.GetType();
    if (type == Core::ComponentType::VSwitch) {
        WritePartParameters(static_cast<const Core::VoltageControlledSwitch &>(component), SwitchKeys, object);
    } else if (type == Core::ComponentType::Relay) {
        WritePartParameters(static_cast<const Core::Relay &>(component), RelayKeys, object);
    } else if (type == Core::ComponentType::Transformer) {
        WritePartParameters(static_cast<const Core::Transformer &>(component), TransformerKeys, object);
    } else if (Core::IsLogicPart(type)) {
        WritePartParameters(static_cast<const Core::LogicPart &>(component), LogicKeys, object);
    } else if (type == Core::ComponentType::Clock) {
        WritePartParameters(static_cast<const Core::Clock &>(component), ClockKeys, object);
    } else if (type == Core::ComponentType::LogicInput) {
        WritePartParameters(static_cast<const Core::LogicInput &>(component), LogicInputKeys, object);
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
        object["value"] = component.GetValue();
    }
    // Only a capacitor or an inductor carries one, and only when it is set
    if (const std::optional<double> initial = component.GetInitialCondition()) {
        object["ic"] = *initial;
    }
    // Every waveform is saved, so switching type after loading keeps the parameters of the others
    if (Core::IsSource(component.GetType())) {
        const auto &source = static_cast<const Core::Source &>(component);
        object["source"] = Core::GetSourceTypeName(source.GetSourceType());
        WriteParameters(ACKeys, source.GetAC(), object);
        WriteSourceParameters(PulseKeys, source.GetPulse(), "pulse", object);
        WriteSourceParameters(EXPKeys, source.GetEXP(), "exp", object);
        WriteSourceParameters(SFFMKeys, source.GetSFFM(), "sffm", object);
        WriteSourceParameters(AMKeys, source.GetAM(), "am", object);
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

// Every waveform key is optional, so files saved before a waveform existed keep the default one of the source
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
    if (const auto read = ReadParameters(ACKeys, object, ac); !read) {
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
    if (const auto read = ReadSourceParameters(object, "pulse", PulseKeys, pulse); !read) {
        return read;
    }
    if (!source.IsValidPulse(pulse)) {
        return std::unexpected("has a pulse with a negative time or a period that is not positive");
    }
    source.SetPulse(pulse);

    Core::EXPParameters exp = source.GetEXP();
    if (const auto read = ReadSourceParameters(object, "exp", EXPKeys, exp); !read) {
        return read;
    }
    if (!source.IsValidEXP(exp)) {
        return std::unexpected("has an exponential with a negative delay, a fall before its rise or a time constant "
                               "that is not positive");
    }
    source.SetEXP(exp);

    Core::SFFMParameters sffm = source.GetSFFM();
    if (const auto read = ReadSourceParameters(object, "sffm", SFFMKeys, sffm); !read) {
        return read;
    }
    if (!source.IsValidSFFM(sffm)) {
        return std::unexpected("has a single-frequency FM with a carrier or signal frequency that is not positive");
    }
    source.SetSFFM(sffm);

    Core::AMParameters am = source.GetAM();
    if (const auto read = ReadSourceParameters(object, "am", AMKeys, am); !read) {
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

// Missing custom parameters keep those of the default model of the part
template <typename Part, typename Model, typename Parameters, std::size_t Count>
std::expected<void, std::string> ReadModelChoice(const nlohmann::json &object, Part &part,
                                                 const Model *(*find_model)(Core::ComponentType, std::string_view),
                                                 const char *custom_name,
                                                 const std::array<ParameterKey<Parameters>, Count> &keys) {
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
                               Core::CustomDiodeModelName, DiodeKeys);
    }
    if (Core::IsBJT(type)) {
        return ReadModelChoice(object, static_cast<Core::BJT &>(component), &Core::FindBJTModel,
                               Core::CustomBJTModelName, BJTKeys);
    }
    if (Core::IsMOSFET(type)) {
        return ReadModelChoice(object, static_cast<Core::MOSFET &>(component), &Core::FindMOSFETModel,
                               Core::CustomMOSFETModelName, MOSFETKeys);
    }
    // Files saved before op-amps had models hold ideal ones
    if (type == Core::ComponentType::OpAmp) {
        auto &op_amp = static_cast<Core::OpAmp &>(component);
        if (object.contains("model")) {
            if (const auto read =
                    ReadModelChoice(object, op_amp, &Core::FindOpAmpModel, Core::CustomOpAmpModelName, OpAmpKeys);
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

// The current a controlled source follows; a missing one loads as none, and the simulation then asks for one
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
    if (component.GetType() != Core::ComponentType::NetLabel || !object.contains("net")) {
        return {};
    }
    const std::optional<std::string> net = ReadString(object, "net");
    if (!net || !Core::NetLabel::IsValidNet(*net)) {
        return std::unexpected("has a net name that is not 1 to 32 letters, digits, _, - or .");
    }
    static_cast<Core::NetLabel &>(component).SetNet(*net);
    return {};
}

// A switch saved without a state stays open
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

// A potentiometer saved without a wiper keeps it in the middle
std::expected<void, std::string> ReadWiper(const nlohmann::json &object, Core::Component &component) {
    if (component.GetType() != Core::ComponentType::Potentiometer || !object.contains("wiper")) {
        return {};
    }
    const std::optional<double> wiper = ReadNumber(object, "wiper");
    if (!wiper || !Core::Potentiometer::IsValidWiper(*wiper)) {
        return std::unexpected("has a wiper that is not a number from 0 to 1");
    }
    static_cast<Core::Potentiometer &>(component).SetWiper(*wiper);
    return {};
}

// A logic input saved without a state stays low
std::expected<void, std::string> ReadLogicInputState(const nlohmann::json &object, Core::Component &component) {
    if (component.GetType() != Core::ComponentType::LogicInput || !object.contains("high")) {
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

constexpr auto StateReaders = std::to_array<StateReader>(
    {&ReadControllingCurrent, &ReadNetName, &ReadSwitchState, &ReadWiper, &ReadLogicInputState});

// What only some kinds of part carry: the current a controlled source follows, a net name, a switch state, a wiper
// and the state of a logic input
std::expected<void, std::string> ReadStateFields(const nlohmann::json &object, Core::Component &component) {
    for (const StateReader reader : StateReaders) {
        if (const auto read = reader(object, component); !read) {
            return std::unexpected(read.error());
        }
    }
    return {};
}

// Parts whose figures are all there is to them, which missing keys leave as the part starts
std::expected<void, std::string> ReadFigureFields(const nlohmann::json &object, Core::Component &component) {
    if (component.GetType() == Core::ComponentType::VSwitch) {
        if (const auto read =
                ReadPartParameters(object, static_cast<Core::VoltageControlledSwitch &>(component), SwitchKeys);
            !read) {
            return std::unexpected(read.error());
        }
    }
    if (component.GetType() == Core::ComponentType::Relay) {
        if (const auto read = ReadPartParameters(object, static_cast<Core::Relay &>(component), RelayKeys); !read) {
            return std::unexpected(read.error());
        }
    }
    if (component.GetType() == Core::ComponentType::Transformer) {
        if (const auto read = ReadPartParameters(object, static_cast<Core::Transformer &>(component), TransformerKeys);
            !read) {
            return std::unexpected(read.error());
        }
    }
    if (Core::IsLogicPart(component.GetType())) {
        if (const auto read = ReadPartParameters(object, static_cast<Core::LogicPart &>(component), LogicKeys); !read) {
            return std::unexpected(read.error());
        }
    }
    if (component.GetType() == Core::ComponentType::Clock) {
        if (const auto read = ReadPartParameters(object, static_cast<Core::Clock &>(component), ClockKeys); !read) {
            return std::unexpected(read.error());
        }
    }
    if (component.GetType() == Core::ComponentType::LogicInput) {
        if (const auto read = ReadPartParameters(object, static_cast<Core::LogicInput &>(component), LogicInputKeys);
            !read) {
            return std::unexpected(read.error());
        }
    }
    return {};
}

// Names are checked separately, once every element is read, so duplicates can be found
std::expected<std::unique_ptr<UIElement>, std::string> ReadElement(const nlohmann::json &object) {
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
    if (component.HasValue()) {
        const std::optional<double> value = ReadNumber(object, "value");
        if (!value || !component.IsValidValue(*value)) {
            return std::unexpected("has no valid value");
        }
        component.SetValue(*value);
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

// Keeps valid, unique names and renames the rest, so the netlist never gets two components with one name
void FixNames(std::vector<std::unique_ptr<UIElement>> &elements, std::vector<std::string> &warnings) {
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
    WriteParameters(SweepRangeKeys, range, object);
    return object;
}

// File names of the analyses, in the order of the Analysis enum
constexpr auto AnalysisKeys = std::to_array<std::string_view>(
    {"operating_point", "transient", "ac_sweep", "dc_sweep", "noise", "transfer_function", "truth_table"});

nlohmann::json WriteSettings(const SimulationSettings &settings) {
    nlohmann::json transient = nlohmann::json::object();
    WriteParameters(TransientKeys, settings.Transient, transient);
    nlohmann::json ac_sweep = nlohmann::json::object();
    WriteParameters(ACSweepKeys, settings.ACSweep, ac_sweep);
    ac_sweep["points_per_decade"] = settings.ACSweep.PointsPerDecade;
    nlohmann::json noise = {{"source", settings.NoiseSource}};
    WriteParameters(ACSweepKeys, settings.NoiseRange, noise);
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
    WriteParameters(ParameterStepKeys, step, parameter_step);
    return {
        {"analysis", AnalysisKeys[static_cast<std::size_t>(settings.Selected)]},
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

// The analysis readers below change only their own part of the settings; missing keys keep its value
std::expected<void, std::string> ReadTransientSettings(const nlohmann::json &object, SimulationSettings &settings) {
    if (const auto read = ReadParameters(TransientKeys, object, settings.Transient); !read) {
        return read;
    }
    if (settings.Transient.StopTime <= 0.0 || settings.Transient.TimeStep <= 0.0) {
        return std::unexpected("have a time that is not positive");
    }
    return {};
}

// The AC sweep and the noise analysis share the form of their frequency range
std::expected<void, std::string> ReadFrequencyRange(const nlohmann::json &object, Core::ACSweepSettings &range) {
    if (const auto read = ReadParameters(ACSweepKeys, object, range); !read) {
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
    if (const auto read = ReadParameters(SweepRangeKeys, object, range); !read) {
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
    if (const auto read = ReadParameters(ParameterStepKeys, object, step); !read) {
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

constexpr auto AnalysisReaders = std::to_array<AnalysisReader>({
    {"transient", "transient", &ReadTransientSettings},
    {"ac_sweep", "AC sweep", &ReadACSweepSettings},
    {"dc_sweep", "DC sweep", &ReadDCSweepSettings},
    {"noise", "noise", &ReadNoiseSettings},
    {"transfer_function", "transfer function", &ReadTransferFunctionSettings},
    {"truth_table", "truth table", &ReadTruthTableSettings},
    {"parameter_step", "parameter step", &ReadParameterStepSettings},
});

// Files saved before settings were stored have none and get the defaults; an analysis with invalid settings
// falls back to its defaults without affecting the others
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
        const auto key =
            analysis->is_string() ? std::ranges::find(AnalysisKeys, analysis->get<std::string>()) : AnalysisKeys.end();
        if (key != AnalysisKeys.end()) {
            settings.Selected = static_cast<Analysis>(key - AnalysisKeys.begin());
        } else {
            warnings.push_back("The selected analysis is unknown and was reset to the transient");
        }
    }
    for (const AnalysisReader &reader : AnalysisReaders) {
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

nlohmann::json WriteMeasurements(const SavedMeasurements &measurements) {
    nlohmann::json voltages = nlohmann::json::array();
    for (const GridPoint point : measurements.Voltages) {
        voltages.push_back({point.X, point.Y});
    }
    return {{"voltages", voltages}, {"currents", measurements.Currents}};
}

// Reads every entry of a list that read_entry accepts. Returns false when the key holds no list or some entry was
// skipped; a missing key is an empty list
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

// Entries that cannot be read are skipped with one warning per list, since a lost trace is easy to probe again
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
    return measurements;
}

std::string WriteDocument(const std::span<const UIElement *const> elements, const std::vector<UIWire> &wires,
                          const SimulationSettings &settings, const SavedMeasurements &measurements) {
    nlohmann::json element_list = nlohmann::json::array();
    for (const UIElement *element : elements) {
        element_list.push_back(WriteElement(*element));
    }
    nlohmann::json wire_list = nlohmann::json::array();
    for (const UIWire &wire : wires) {
        wire_list.push_back({
            {"start", {wire.GetStart().X, wire.GetStart().Y}},
            {"end", {wire.GetEnd().X, wire.GetEnd().Y}},
        });
    }
    nlohmann::json document = {
        {"format", FormatName},
        {"version", FormatVersion},
        {"elements", element_list},
        {"wires", wire_list},
        {"simulation", WriteSettings(settings)},
    };
    if (!measurements.Voltages.empty() || !measurements.Currents.empty()) {
        document["measurements"] = WriteMeasurements(measurements);
    }
    return document.dump(2) + "\n";
}

} // namespace

/**
 * @brief   Writes a schematic as JSON.
 * @param[in] elements      Components placed on the grid.
 * @param[in] wires         Wire segments.
 * @param[in] settings      Settings of the analyses.
 * @param[in] measurements  Traces the plots show; the key is left out when there are none.
 * @return  The file contents, indented for reading and diffing. Values are plain numbers, never suffixed text.
 */
std::string SaveSchematic(const std::vector<std::unique_ptr<UIElement>> &elements, const std::vector<UIWire> &wires,
                          const SimulationSettings &settings, const SavedMeasurements &measurements) {
    std::vector<const UIElement *> pointers;
    for (const auto &element : elements) {
        pointers.push_back(element.get());
    }
    return WriteDocument(pointers, wires, settings, measurements);
}

/**
 * @brief   Writes some parts and wires as a schematic of their own, for the clipboard.
 * @param[in] elements  Parts to write, which stay where they are.
 * @param[in] wires     Wire segments to write.
 * @return  The same JSON a file holds, with the default settings and no measurements, so it loads back with
 *          LoadSchematic().
 */
std::string SaveParts(const std::span<const UIElement *const> elements, const std::vector<UIWire> &wires) {
    return WriteDocument(elements, wires, {}, {});
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
    if (ReadString(document, "format") != FormatName) {
        return std::unexpected("The file is not an imcsim schematic");
    }
    const std::optional<int> version = ReadInt(document, "version");
    if (!version || *version < 1) {
        return std::unexpected("The file has no valid format version");
    }
    if (*version > FormatVersion) {
        return std::unexpected(std::format("The file uses format version {}, but this build only reads up to {}",
                                           *version, FormatVersion));
    }

    LoadedSchematic schematic;
    const auto elements = document.find("elements");
    if (elements != document.end() && elements->is_array()) {
        for (std::size_t index = 0; index < elements->size(); ++index) {
            auto element = ReadElement((*elements)[index]);
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
    std::ifstream file(path, std::ios::binary);
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
