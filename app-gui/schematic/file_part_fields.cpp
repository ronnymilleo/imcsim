/**
 * @file    file_part_fields.cpp
 * @brief   Writes and reads what each kind of part carries in the schematic file, besides its position and name.
 */

#include "schematic/file_part_fields.h"

#include "circuit/components/behavioral_source.h"
#include "circuit/components/bjt.h"
#include "circuit/components/clock.h"
#include "circuit/components/controlled_source.h"
#include "circuit/components/diode.h"
#include "circuit/components/fuse.h"
#include "circuit/components/logic_input.h"
#include "circuit/components/logic_output.h"
#include "circuit/components/logic_part.h"
#include "circuit/components/mosfet.h"
#include "circuit/components/net_label.h"
#include "circuit/components/op_amp.h"
#include "circuit/components/potentiometer.h"
#include "circuit/components/relay.h"
#include "circuit/components/seven_segment.h"
#include "circuit/components/subcircuit.h"
#include "circuit/components/subcircuit_pin.h"
#include "circuit/components/switch.h"
#include "circuit/components/transformer.h"
#include "circuit/components/voltage_controlled_switch.h"
#include "nlohmann/json.hpp"
#include "schematic/file_json.h"
#include <array>
#include <format>
#include <optional>

namespace GUI {

namespace {

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

constexpr auto FUSE_KEYS = std::to_array<ParameterKey<Core::FuseParameters>>({
    {"rated_current", &Core::FuseParameters::RatedCurrent},
    {"melting_i2t", &Core::FuseParameters::MeltingI2t},
    {"cold_resistance", &Core::FuseParameters::ColdResistance},
    {"blown_resistance", &Core::FuseParameters::BlownResistance},
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

constexpr auto SEVEN_SEGMENT_KEYS = std::to_array<ParameterKey<Core::SevenSegmentParameters>>({
    {"logic_level", &Core::SevenSegmentParameters::LogicLevel},
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
    {"ifmax", &Core::DiodeParameters::MaxForwardCurrent},
    {"pmax", &Core::DiodeParameters::MaxPower},
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
    {"vos", &Core::OpAmpParameters::InputOffsetVoltage},
});

constexpr auto NOISE_KEYS = std::to_array<ParameterKey<Core::NoiseParameters>>({
    {"offset", &Core::NoiseParameters::Offset},
    {"white", &Core::NoiseParameters::WhiteAmplitude},
    {"step", &Core::NoiseParameters::TimeStep},
    {"flicker_exponent", &Core::NoiseParameters::FlickerExponent},
    {"flicker", &Core::NoiseParameters::FlickerAmplitude},
    {"telegraph", &Core::NoiseParameters::TelegraphAmplitude},
    {"capture", &Core::NoiseParameters::CaptureTime},
    {"emission", &Core::NoiseParameters::EmissionTime},
});

/// The distribution is a name, so it is written and read apart.
constexpr auto RANDOM_KEYS = std::to_array<ParameterKey<Core::RandomParameters>>({
    {"step", &Core::RandomParameters::TimeStep},
    {"delay", &Core::RandomParameters::Delay},
    {"first", &Core::RandomParameters::First},
    {"second", &Core::RandomParameters::Second},
});

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
 * @brief   A marker that is a port saves its number and reference impedance; one that is not saves nothing.
 */
void WritePortSettings(const Core::PortSettings &port, nlohmann::json &object) {
    if (port.IsPort) {
        object["port"] = {{"number", port.Number}, {"z0", port.ReferenceOhms}};
    }
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

/**
 * @brief   The input stage of a custom op-amp; a file saved before op-amps had one keeps the NPN pair.
 */
std::expected<void, std::string> ReadOpAmpInputStage(const nlohmann::json &object, Core::OpAmp &op_amp) {
    const auto parameters_object = object.find("parameters");
    if (!op_amp.IsCustom() || parameters_object == object.end() || !parameters_object->contains("input")) {
        return {};
    }
    const std::optional<std::string> name = ReadString(*parameters_object, "input");
    const std::optional<Core::OpAmpInputStage> stage =
        name ? Core::FindOpAmpInputStage(*name) : std::optional<Core::OpAmpInputStage>{};
    if (!stage) {
        return std::unexpected("has no valid op-amp input stage");
    }
    Core::OpAmpParameters parameters = op_amp.GetParameters();
    parameters.InputStage = *stage;
    op_amp.SetCustomParameters(parameters);
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

/**
 * @brief   The expression of a behavioral source, kept as written; a missing one loads as the default.
 */
std::expected<void, std::string> ReadBehavioralExpression(const nlohmann::json &object, Core::Component &component) {
    if (!Core::IsBehavioralSource(component.GetType()) || !object.contains("expression")) {
        return {};
    }
    const std::optional<std::string> expression = ReadString(object, "expression");
    if (!expression) {
        return std::unexpected("has an expression that is not text");
    }
    static_cast<Core::BehavioralSource &>(component).SetExpression(*expression);
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
 * @brief   A pin marker saved without a port is no port.
 */
std::expected<void, std::string> ReadPortSettings(const nlohmann::json &object, Core::Component &component) {
    if (!Core::IsSubcircuitPin(component.GetType()) || !object.contains("port")) {
        return {};
    }
    const nlohmann::json &port = object["port"];
    const std::optional<int> number = port.is_object() ? ReadInt(port, "number") : std::nullopt;
    const std::optional<double> reference = port.is_object() ? ReadNumber(port, "z0") : std::nullopt;
    if (!number || !reference || *number < 1 || *reference <= 0.0) {
        return std::unexpected("has an S-parameter port without a valid number and reference impedance");
    }
    static_cast<Core::SubcircuitPinMarker &>(component).SetPort(
        {.IsPort = true, .Number = *number, .ReferenceOhms = *reference});
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

constexpr auto STATE_READERS =
    std::to_array<StateReader>({&ReadControllingCurrent, &ReadBehavioralExpression, &ReadNetName, &ReadPortSettings,
                                &ReadSwitchState, &ReadWiper, &ReadLogicInputState});

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

std::expected<void, std::string> ReadFuseFigures(const nlohmann::json &object, Core::Component &component) {
    if (component.GetType() != Core::ComponentType::FUSE) {
        return {};
    }
    return ReadPartParameters(object, static_cast<Core::Fuse &>(component), FUSE_KEYS);
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

std::expected<void, std::string> ReadSevenSegmentFigures(const nlohmann::json &object, Core::Component &component) {
    if (component.GetType() != Core::ComponentType::SEVEN_SEGMENT) {
        return {};
    }
    return ReadPartParameters(object, static_cast<Core::SevenSegmentDisplay &>(component), SEVEN_SEGMENT_KEYS);
}

constexpr auto FIGURE_READERS = std::to_array<StateReader>(
    {&ReadSwitchFigures, &ReadRelayFigures, &ReadFuseFigures, &ReadTransformerFigures, &ReadLogicFigures,
     &ReadClockFigures, &ReadLogicInputFigures, &ReadLogicOutputFigures, &ReadSevenSegmentFigures});

} // namespace

/**
 * @brief   Writes every waveform of a source, so switching the type after loading keeps the parameters of the others.
 * @param[in]     source  Source to write.
 * @param[in,out] object  Element object that receives the keys.
 */
void WriteSourceFields(const Core::Source &source, nlohmann::json &object) {
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
    WriteSourceParameters(NOISE_KEYS, source.GetNoise(), "noise", object);
    WriteSourceParameters(RANDOM_KEYS, source.GetRandom(), "random", object);
    object["random"]["distribution"] = Core::GetRandomDistributionName(source.GetRandom().Distribution);
}

/**
 * @brief   Writes the model of a diode, transistor or op-amp: its name, plus its parameters when custom.
 * @details The ideal op-amp also keeps its bandwidth. Nothing is written for the other parts.
 * @param[in]     component  Part to write.
 * @param[in,out] object     Element object that receives the keys.
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
        // The input stage is a name, so it is written apart from the numbers
        if (op_amp.IsCustom()) {
            object["parameters"]["input"] = Core::GetOpAmpInputStageName(op_amp.GetParameters().InputStage);
        }
        if (op_amp.IsIdeal()) {
            object["gbw"] = op_amp.GetIdealBandwidth();
        }
    }
}

/**
 * @brief   Writes the state of the parts that have one, such as the current a controlled source follows, the
 *          expression of a behavioral source, the definition of a block, a net name or a switch position.
 * @param[in]     component  Part to write.
 * @param[in,out] object     Element object that receives the keys.
 */
void WriteKindFields(const Core::Component &component, nlohmann::json &object) {
    const Core::ComponentType type = component.GetType();
    if (Core::IsCurrentControlled(type)) {
        object["control"] = static_cast<const Core::ControlledSource &>(component).GetControllingCurrent();
    } else if (Core::IsBehavioralSource(type)) {
        object["expression"] = static_cast<const Core::BehavioralSource &>(component).GetExpression();
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
        if (Core::IsSubcircuitPin(type)) {
            WritePortSettings(static_cast<const Core::SubcircuitPinMarker &>(component).GetPort(), object);
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
 * @brief   Writes the figures of the parts that are all figures: the switch that follows a voltage, the relay, the
 *          fuse, the transformer, the gates and flip-flops with their logic family, a clock and the logic and
 *          display parts.
 * @param[in]     component  Part to write.
 * @param[in,out] object     Element object that receives the keys.
 */
void WriteFigureFields(const Core::Component &component, nlohmann::json &object) {
    const Core::ComponentType type = component.GetType();
    if (type == Core::ComponentType::V_SWITCH) {
        WritePartParameters(static_cast<const Core::VoltageControlledSwitch &>(component), SWITCH_KEYS, object);
    } else if (type == Core::ComponentType::RELAY) {
        WritePartParameters(static_cast<const Core::Relay &>(component), RELAY_KEYS, object);
    } else if (type == Core::ComponentType::FUSE) {
        WritePartParameters(static_cast<const Core::Fuse &>(component), FUSE_KEYS, object);
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
    } else if (type == Core::ComponentType::SEVEN_SEGMENT) {
        WritePartParameters(static_cast<const Core::SevenSegmentDisplay &>(component), SEVEN_SEGMENT_KEYS, object);
    }
}

/**
 * @brief   Reads the waveforms of a source; every waveform key is optional, so files saved before a waveform existed
 *          keep the default one of the source.
 * @param[in]     object  Element object to read.
 * @param[in,out] source  Source that receives the waveforms.
 * @return  Nothing on success, or what is wrong with the source, to follow "Element 3 ...".
 */
std::expected<void, std::string> ReadSourceFields(const nlohmann::json &object, Core::Source &source) {
    if (object.contains("source")) {
        const std::optional<std::string> type_name = ReadString(object, "source");
        const std::optional<Core::Source::SourceType> type =
            type_name ? Core::ParseSourceType(*type_name) : std::nullopt;
        if (!type) {
            return std::unexpected("has a source type other than DC, AC, Pulse, EXP, SFFM, AM, PWL, Noise or Random");
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

    Core::NoiseParameters noise = source.GetNoise();
    if (const auto read = ReadSourceParameters(object, "noise", NOISE_KEYS, noise); !read) {
        return read;
    }
    if (!source.IsValidNoise(noise)) {
        return std::unexpected("has a noise with a time step that is negative or missing, a 1/f exponent outside 0 to "
                               "2 or a telegraph time that is not positive");
    }
    source.SetNoise(noise);

    Core::RandomParameters random = source.GetRandom();
    if (const auto read = ReadSourceParameters(object, "random", RANDOM_KEYS, random); !read) {
        return read;
    }
    if (const auto entry = object.find("random"); entry != object.end() && entry->contains("distribution")) {
        const std::optional<std::string> name = ReadString(*entry, "distribution");
        const std::optional<Core::RandomDistribution> distribution =
            name ? Core::ParseRandomDistribution(*name) : std::nullopt;
        if (!distribution) {
            return std::unexpected("has a random distribution other than Uniform, Gaussian, Exponential or Poisson");
        }
        random.Distribution = *distribution;
    }
    if (!source.IsValidRandom(random)) {
        return std::unexpected("has random values with a time step that is not positive, a negative delay or a first "
                               "parameter that is not valid for the distribution");
    }
    source.SetRandom(random);
    return {};
}

/**
 * @brief   Reads the model of a diode, transistor or op-amp.
 * @details Files saved before op-amps had models hold ideal ones, which keep their defaults.
 * @param[in]     object     Element object to read.
 * @param[in,out] component  Part that receives the model.
 * @return  Nothing on success, or what is wrong with the model.
 */
std::expected<void, std::string> ReadModelFields(const nlohmann::json &object, Core::Component &component) {
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
            if (const auto read = ReadOpAmpInputStage(object, op_amp); !read) {
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

/**
 * @brief   Reads what only some kinds of part carry: the current a controlled source follows, a net name, a switch
 *          state, a wiper and the state of a logic input.
 * @param[in]     object     Element object to read.
 * @param[in,out] component  Part that receives the state.
 * @return  Nothing on success, or what is wrong with the state.
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
 * @brief   Reads the figures of the parts that are all figures: the switch that follows a voltage, the relay, the
 *          fuse, the transformer, the gates and flip-flops with their logic family, a clock and the logic and
 *          display parts.
 * @param[in]     object     Element object to read.
 * @param[in,out] component  Part that receives the figures.
 * @return  Nothing on success, or what is wrong with the figures.
 */
std::expected<void, std::string> ReadFigureFields(const nlohmann::json &object, Core::Component &component) {
    for (const StateReader reader : FIGURE_READERS) {
        if (const auto read = reader(object, component); !read) {
            return std::unexpected(read.error());
        }
    }
    return {};
}

} // namespace GUI
