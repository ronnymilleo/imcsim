/**
 * @file    file_figure_fields.cpp
 * @brief   Writes and reads the figures of the parts that are all figures in the schematic file.
 */

#include "schematic/file_figure_fields.h"

#include "circuit/components/clock.h"
#include "circuit/components/diac.h"
#include "circuit/components/fuse.h"
#include "circuit/components/logic_input.h"
#include "circuit/components/logic_output.h"
#include "circuit/components/logic_part.h"
#include "circuit/components/relay.h"
#include "circuit/components/seven_segment.h"
#include "circuit/components/thyristor.h"
#include "circuit/components/transformer.h"
#include "circuit/components/transmission_line.h"
#include "circuit/components/voltage_controlled_switch.h"
#include "nlohmann/json.hpp"
#include "schematic/file_json.h"
#include <algorithm>
#include <array>
#include <format>
#include <optional>

namespace GUI {

namespace {

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

constexpr auto THYRISTOR_KEYS = std::to_array<ParameterKey<Core::ThyristorParameters>>({
    {"on_threshold", &Core::ThyristorParameters::OnThresholdVoltage},
    {"on_resistance", &Core::ThyristorParameters::OnResistance},
    {"off_resistance", &Core::ThyristorParameters::OffResistance},
    {"holding_current", &Core::ThyristorParameters::HoldingCurrent},
    {"gate_trigger_current", &Core::ThyristorParameters::GateTriggerCurrent},
    {"gate_trigger_voltage", &Core::ThyristorParameters::GateTriggerVoltage},
    {"gate_resistance", &Core::ThyristorParameters::GateResistance},
    {"breakover", &Core::ThyristorParameters::BreakoverVoltage},
    {"critical_dvdt", &Core::ThyristorParameters::CriticalDvDt},
    {"turn_off_time", &Core::ThyristorParameters::TurnOffTime},
    {"vdrm", &Core::ThyristorParameters::MaxRepetitivePeakVoltage},
    {"it_max", &Core::ThyristorParameters::MaxOnStateCurrent},
    {"itsm", &Core::ThyristorParameters::MaxSurgeCurrent},
    {"i2t", &Core::ThyristorParameters::MaxI2t},
});

constexpr auto DIAC_KEYS = std::to_array<ParameterKey<Core::DiacParameters>>({
    {"breakover", &Core::DiacParameters::BreakoverVoltage},
    {"on_threshold", &Core::DiacParameters::OnThresholdVoltage},
    {"on_resistance", &Core::DiacParameters::OnResistance},
    {"off_resistance", &Core::DiacParameters::OffResistance},
    {"holding_current", &Core::DiacParameters::HoldingCurrent},
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

constexpr auto TRANSMISSION_LINE_KEYS = std::to_array<ParameterKey<Core::TransmissionLineParameters>>({
    {"delay", &Core::TransmissionLineParameters::Delay},
    {"dc_resistance", &Core::TransmissionLineParameters::DcResistance},
    {"skin_resistance", &Core::TransmissionLineParameters::SkinResistance},
    {"dc_conductance", &Core::TransmissionLineParameters::DcConductance},
    {"loss_tangent", &Core::TransmissionLineParameters::LossTangent},
    {"reference_frequency", &Core::TransmissionLineParameters::ReferenceFrequency},
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

/**
 * @brief   Parts whose parameters are all there is to them save them as one object.
 */
template <typename Part, typename Parameters, std::size_t Count>
void WritePartParameters(const Part &part,
                         const std::array<ParameterKey<Parameters>, Count> &keys,
                         nlohmann::json &object) {
    WriteParametersObject(keys, part.GetParameters(), "parameters", object);
}

/**
 * @brief   Missing parameters keep those the part starts with.
 */
template <typename Part, typename Parameters, std::size_t Count>
std::expected<void, std::string>
ReadPartParameters(const nlohmann::json &object, Part &part, const std::array<ParameterKey<Parameters>, Count> &keys) {
    Parameters parameters = part.GetParameters();
    if (const auto read =
            ReadParametersObject(keys, object, "parameters", "has parameters that are not an object", parameters);
        !read) {
        return read;
    }
    if (!Part::IsValidParameters(parameters)) {
        return std::unexpected("has parameters out of range");
    }
    part.SetParameters(parameters);
    return {};
}

template <typename Part, const auto &Keys>
void WriteFigures(const Core::Component &component, nlohmann::json &object) {
    WritePartParameters(static_cast<const Part &>(component), Keys, object);
}

template <typename Part, const auto &Keys>
std::expected<void, std::string> ReadFigures(const nlohmann::json &object, Core::Component &component) {
    return ReadPartParameters(object, static_cast<Part &>(component), Keys);
}

// Logic parts, which also carry the number of inputs of a gate

/**
 * @brief   A gate saved without a count has two inputs.
 */
void WriteLogicFigures(const Core::Component &component, nlohmann::json &object) {
    WriteFigures<Core::LogicPart, LOGIC_KEYS>(component, object);
    if (Core::HasVariableInputs(component.GetType())) {
        object["inputs"] = static_cast<const Core::LogicPart &>(component).GetInputCount();
    }
}

std::expected<void, std::string> ReadLogicFigures(const nlohmann::json &object, Core::Component &component) {
    if (const auto read = ReadFigures<Core::LogicPart, LOGIC_KEYS>(object, component); !read) {
        return read;
    }
    if (!Core::HasVariableInputs(component.GetType())) {
        return {};
    }
    const std::string error_text = std::format("has a number of inputs that is not a whole number from {} to {}",
                                               Core::MIN_LOGIC_INPUTS,
                                               Core::MAX_LOGIC_INPUTS);
    int inputs = 0;
    const std::expected<bool, std::string> has_inputs = ReadOptionalInt(object, "inputs", inputs, error_text);
    if (!has_inputs) {
        return std::unexpected(has_inputs.error());
    }
    if (!*has_inputs) {
        return {};
    }
    if (!Core::LogicPart::IsValidInputCount(inputs)) {
        return std::unexpected(error_text);
    }
    static_cast<Core::LogicPart &>(component).SetInputCount(inputs);
    return {};
}

// Table of the figures

template <Core::ComponentType Type>
bool IsType(const Core::ComponentType type) {
    return type == Type;
}

/**
 * @struct  FigureCodec
 * @brief   The kinds of part that are all figures, with the functions that write and read them; one row for each kind
 *          holds everything the file format knows about it.
 */
struct FigureCodec {
    bool (*Matches)(Core::ComponentType);
    void (*Write)(const Core::Component &, nlohmann::json &);
    std::expected<void, std::string> (*Read)(const nlohmann::json &, Core::Component &);
};

template <Core::ComponentType Type, typename Part, const auto &Keys>
constexpr FigureCodec MakeFigureCodec() {
    return {&IsType<Type>, &WriteFigures<Part, Keys>, &ReadFigures<Part, Keys>};
}

constexpr auto FIGURE_CODECS = std::to_array<FigureCodec>({
    MakeFigureCodec<Core::ComponentType::V_SWITCH, Core::VoltageControlledSwitch, SWITCH_KEYS>(),
    MakeFigureCodec<Core::ComponentType::RELAY, Core::Relay, RELAY_KEYS>(),
    MakeFigureCodec<Core::ComponentType::FUSE, Core::Fuse, FUSE_KEYS>(),
    MakeFigureCodec<Core::ComponentType::THYRISTOR, Core::Thyristor, THYRISTOR_KEYS>(),
    MakeFigureCodec<Core::ComponentType::TRIAC, Core::Thyristor, THYRISTOR_KEYS>(),
    MakeFigureCodec<Core::ComponentType::DIAC, Core::Diac, DIAC_KEYS>(),
    MakeFigureCodec<Core::ComponentType::TRANSFORMER, Core::Transformer, TRANSFORMER_KEYS>(),
    MakeFigureCodec<Core::ComponentType::TRANSMISSION_LINE, Core::TransmissionLine, TRANSMISSION_LINE_KEYS>(),
    {&Core::IsLogicPart, &WriteLogicFigures, &ReadLogicFigures},
    MakeFigureCodec<Core::ComponentType::CLOCK, Core::Clock, CLOCK_KEYS>(),
    MakeFigureCodec<Core::ComponentType::LOGIC_INPUT, Core::LogicInput, LOGIC_INPUT_KEYS>(),
    MakeFigureCodec<Core::ComponentType::LOGIC_OUTPUT, Core::LogicOutput, LOGIC_OUTPUT_KEYS>(),
    MakeFigureCodec<Core::ComponentType::SEVEN_SEGMENT, Core::SevenSegmentDisplay, SEVEN_SEGMENT_KEYS>(),
});

/**
 * @brief   Finds how the figures of a kind of part are saved.
 * @return  The codec of the part, or null for a part with no figures of its own.
 */
const FigureCodec *FindFigureCodec(const Core::ComponentType type) {
    const auto found =
        std::ranges::find_if(FIGURE_CODECS, [type](const FigureCodec &codec) { return codec.Matches(type); });
    return found == FIGURE_CODECS.end() ? nullptr : &*found;
}

} // namespace

/**
 * @brief   Writes the figures of the parts that are all figures: the switch that follows a voltage, the relay, the
 *          fuse, the transformer, the gates and flip-flops with their logic family, a clock and the logic and
 *          display parts.
 * @param[in]     component  Part to write.
 * @param[in,out] object     Element object that receives the keys.
 */
void WriteFigureFields(const Core::Component &component, nlohmann::json &object) {
    if (const FigureCodec *codec = FindFigureCodec(component.GetType())) {
        codec->Write(component, object);
    }
}

/**
 * @brief   Reads the figures of the parts that are all figures; missing keys leave the part as it starts.
 * @param[in]     object     Element object to read.
 * @param[in,out] component  Part that receives the figures.
 * @return  Nothing on success, or what is wrong with the figures.
 */
std::expected<void, std::string> ReadFigureFields(const nlohmann::json &object, Core::Component &component) {
    if (const FigureCodec *codec = FindFigureCodec(component.GetType())) {
        return codec->Read(object, component);
    }
    return {};
}

} // namespace GUI
