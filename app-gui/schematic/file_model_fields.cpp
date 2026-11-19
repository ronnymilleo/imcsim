/**
 * @file    file_model_fields.cpp
 * @brief   Writes and reads the model of a diode, transistor or op-amp in the schematic file.
 */

#include "schematic/file_model_fields.h"

#include "circuit/components/bjt.h"
#include "circuit/components/diode.h"
#include "circuit/components/mosfet.h"
#include "circuit/components/op_amp.h"
#include "nlohmann/json.hpp"
#include "schematic/file_json.h"
#include <algorithm>
#include <array>
#include <optional>
#include <string_view>
#include <type_traits>

namespace GUI {

namespace {

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

/**
 * @struct  ModelCodec
 * @brief   What the file format needs to know about the models of one kind of part: the keys of its custom
 *          parameters, the name that stands for them and where the ready models are looked up.
 */
template <typename Part, typename Model, typename Parameters, std::size_t Count>
struct ModelCodec {
    using PartType = Part;

    const std::array<ParameterKey<Parameters>, Count> &Keys;
    const char *CustomName;
    const Model *(*Find)(Core::ComponentType, std::string_view);
};

/**
 * @brief   Builds the codec of a part; only the part has to be spelled out, since the rest follows from the arguments.
 */
template <typename Part, typename Model, typename Parameters, std::size_t Count>
constexpr ModelCodec<Part, Model, Parameters, Count>
MakeModelCodec(const std::array<ParameterKey<Parameters>, Count> &keys,
               const char *custom_name,
               const Model *(*find)(Core::ComponentType, std::string_view)) {
    return {keys, custom_name, find};
}

constexpr auto DIODE_MODELS =
    MakeModelCodec<Core::Diode>(DIODE_KEYS, Core::CUSTOM_DIODE_MODEL_NAME, &Core::FindDiodeModel);
constexpr auto BJT_MODELS = MakeModelCodec<Core::BJT>(BJT_KEYS, Core::CUSTOM_BJT_MODEL_NAME, &Core::FindBJTModel);
constexpr auto MOSFET_MODELS =
    MakeModelCodec<Core::MOSFET>(MOSFET_KEYS, Core::CUSTOM_MOSFET_MODEL_NAME, &Core::FindMOSFETModel);
constexpr auto OP_AMP_MODELS =
    MakeModelCodec<Core::OpAmp>(OP_AMP_KEYS, Core::CUSTOM_OP_AMP_MODEL_NAME, &Core::FindOpAmpModel);

/**
 * @brief   Parts with a model save its name, plus their parameters when custom.
 */
template <const auto &Codec>
void WriteModel(const Core::Component &component, nlohmann::json &object) {
    const auto &part = static_cast<const typename std::remove_cvref_t<decltype(Codec)>::PartType &>(component);
    // A library model lives in the document, which the part names
    if (part.HasLibraryModel()) {
        object["library_model"] = part.GetLibraryModel();
        return;
    }
    object["model"] = part.GetModelName();
    if (part.IsCustom()) {
        WriteParametersObject(Codec.Keys, part.GetParameters(), "parameters", object);
    }
}

/**
 * @brief   The model it names may be missing from the document; the checks of a run say so.
 */
std::expected<void, std::string> ReadLibraryModel(const nlohmann::json &object, Core::Component &part) {
    const std::optional<std::string> library_model = ReadString(object, "library_model");
    if (!library_model || library_model->empty() || !part.TakesLibraryModel()) {
        return std::unexpected("has no valid library model");
    }
    part.SetLibraryModel(*library_model);
    return {};
}

template <const auto &Codec>
std::expected<void, std::string> ReadNamedModel(const std::string_view name, Core::Component &component) {
    auto &part = static_cast<typename std::remove_cvref_t<decltype(Codec)>::PartType &>(component);
    const auto *model = Codec.Find(part.GetType(), name);
    if (model == nullptr) {
        return std::unexpected("has no valid model");
    }
    part.SetModel(*model);
    return {};
}

/**
 * @brief   Missing custom parameters keep those of the default model of the part.
 */
template <const auto &Codec>
std::expected<void, std::string> ReadCustomModel(const nlohmann::json &object, Core::Component &component) {
    auto &part = static_cast<typename std::remove_cvref_t<decltype(Codec)>::PartType &>(component);
    auto parameters = part.GetParameters();
    if (const auto read = ReadParametersObject(Codec.Keys,
                                               object,
                                               "parameters",
                                               "has model parameters that are not an object",
                                               parameters);
        !read) {
        return read;
    }
    if (!part.IsValidParameters(parameters)) {
        return std::unexpected("has model parameters out of range");
    }
    part.SetCustomParameters(parameters);
    return {};
}

template <const auto &Codec>
std::expected<void, std::string> ReadModel(const nlohmann::json &object, Core::Component &component) {
    if (object.contains("library_model")) {
        return ReadLibraryModel(object, component);
    }
    const std::optional<std::string> model_name = ReadString(object, "model");
    if (!model_name) {
        return std::unexpected("has no valid model");
    }
    if (*model_name != Codec.CustomName) {
        return ReadNamedModel<Codec>(*model_name, component);
    }
    return ReadCustomModel<Codec>(object, component);
}

// Op-amp, which also has an input stage and an ideal bandwidth

/**
 * @brief   The ideal op-amp also keeps its bandwidth, and a custom one the name of its input stage, apart from the
 *          numbers.
 */
void WriteOpAmpModel(const Core::Component &component, nlohmann::json &object) {
    WriteModel<OP_AMP_MODELS>(component, object);
    const auto &op_amp = static_cast<const Core::OpAmp &>(component);
    if (op_amp.IsCustom()) {
        object["parameters"]["input"] = Core::GetOpAmpInputStageName(op_amp.GetParameters().InputStage);
    }
    if (op_amp.IsIdeal()) {
        object["gbw"] = op_amp.GetIdealBandwidth();
    }
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

/**
 * @brief   Files saved before op-amps had models hold ideal ones, which keep their defaults.
 */
std::expected<void, std::string> ReadOpAmpModel(const nlohmann::json &object, Core::Component &component) {
    auto &op_amp = static_cast<Core::OpAmp &>(component);
    if (object.contains("model")) {
        if (const auto read = ReadModel<OP_AMP_MODELS>(object, op_amp); !read) {
            return read;
        }
        if (const auto read = ReadOpAmpInputStage(object, op_amp); !read) {
            return read;
        }
    }
    double bandwidth = 0.0;
    const std::expected<bool, std::string> has_bandwidth =
        ReadOptionalNumber(object,
                           "gbw",
                           bandwidth,
                           "has a gain-bandwidth product that is not positive",
                           &Core::IsValidIdealBandwidth);
    if (!has_bandwidth) {
        return std::unexpected(has_bandwidth.error());
    }
    if (*has_bandwidth) {
        op_amp.SetIdealBandwidth(bandwidth);
    }
    return {};
}

bool IsOpAmp(const Core::ComponentType type) {
    return type == Core::ComponentType::OP_AMP;
}

/**
 * @struct  ModelFieldCodec
 * @brief   The kinds of part that have a model, and the functions that write and read it.
 */
struct ModelFieldCodec {
    bool (*Matches)(Core::ComponentType);
    void (*Write)(const Core::Component &, nlohmann::json &);
    std::expected<void, std::string> (*Read)(const nlohmann::json &, Core::Component &);
};

constexpr auto MODEL_CODECS = std::to_array<ModelFieldCodec>({
    {&Core::IsDiode, &WriteModel<DIODE_MODELS>, &ReadModel<DIODE_MODELS>},
    {&Core::IsBJT, &WriteModel<BJT_MODELS>, &ReadModel<BJT_MODELS>},
    {&Core::IsMOSFET, &WriteModel<MOSFET_MODELS>, &ReadModel<MOSFET_MODELS>},
    {&IsOpAmp, &WriteOpAmpModel, &ReadOpAmpModel},
});

/**
 * @brief   Finds how the model of a kind of part is saved.
 * @return  The codec of the part, or null for a part with no model.
 */
const ModelFieldCodec *FindModelCodec(const Core::ComponentType type) {
    const auto found =
        std::ranges::find_if(MODEL_CODECS, [type](const ModelFieldCodec &codec) { return codec.Matches(type); });
    return found == MODEL_CODECS.end() ? nullptr : &*found;
}

} // namespace

/**
 * @brief   Writes the model of a diode, transistor or op-amp: its name, plus its parameters when custom.
 * @details The ideal op-amp also keeps its bandwidth. Nothing is written for the other parts.
 * @param[in]     component  Part to write.
 * @param[in,out] object     Element object that receives the keys.
 */
void WriteModelFields(const Core::Component &component, nlohmann::json &object) {
    if (const ModelFieldCodec *codec = FindModelCodec(component.GetType())) {
        codec->Write(component, object);
    }
}

/**
 * @brief   Reads the model of a diode, transistor or op-amp.
 * @param[in]     object     Element object to read.
 * @param[in,out] component  Part that receives the model.
 * @return  Nothing on success, or what is wrong with the model.
 */
std::expected<void, std::string> ReadModelFields(const nlohmann::json &object, Core::Component &component) {
    if (const ModelFieldCodec *codec = FindModelCodec(component.GetType())) {
        return codec->Read(object, component);
    }
    return {};
}

} // namespace GUI
