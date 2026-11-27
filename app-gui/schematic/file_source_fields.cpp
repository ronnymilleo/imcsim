/**
 * @file    file_source_fields.cpp
 * @brief   Writes and reads the waveforms of a source in the schematic file.
 */

#include "schematic/file_source_fields.h"

#include "circuit/components/source.h"
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
 * @struct  WaveformCodec
 * @brief   What the file format needs to know about a waveform that is a set of numbers: its key, the numbers, how
 *          the source holds them and what is wrong when the source rejects them.
 */
template <typename Parameters, std::size_t Count>
struct WaveformCodec {
    const char *Key; ///< Key of the object that holds the waveform, so the sine keys in the element object stay apart.
    const std::array<ParameterKey<Parameters>, Count> &Keys;
    const Parameters &(Core::Source::*Get)() const;
    void (Core::Source::*Set)(const Parameters &);
    bool (Core::Source::*IsValid)(const Parameters &) const;
    const char *Message; ///< Continues "Element 3 ..." when the source rejects the numbers.
};

constexpr WaveformCodec PULSE_WAVEFORM{"pulse",
                                       PULSE_KEYS,
                                       &Core::Source::GetPulse,
                                       &Core::Source::SetPulse,
                                       &Core::Source::IsValidPulse,
                                       "has a pulse with a negative time or a period that is not positive"};

constexpr WaveformCodec EXP_WAVEFORM{"exp",
                                     EXP_KEYS,
                                     &Core::Source::GetEXP,
                                     &Core::Source::SetEXP,
                                     &Core::Source::IsValidEXP,
                                     "has an exponential with a negative delay, a fall before its rise or a time "
                                     "constant that is not positive"};

constexpr WaveformCodec SFFM_WAVEFORM{
    "sffm",
    SFFM_KEYS,
    &Core::Source::GetSFFM,
    &Core::Source::SetSFFM,
    &Core::Source::IsValidSFFM,
    "has a single-frequency FM with a carrier or signal frequency that is not positive"};

constexpr WaveformCodec AM_WAVEFORM{"am",
                                    AM_KEYS,
                                    &Core::Source::GetAM,
                                    &Core::Source::SetAM,
                                    &Core::Source::IsValidAM,
                                    "has an amplitude modulation with a negative delay or a frequency that is not "
                                    "positive"};

constexpr WaveformCodec NOISE_WAVEFORM{"noise",
                                       NOISE_KEYS,
                                       &Core::Source::GetNoise,
                                       &Core::Source::SetNoise,
                                       &Core::Source::IsValidNoise,
                                       "has a noise with a time step that is negative or missing, a 1/f exponent "
                                       "outside 0 to 2 or a telegraph time that is not positive"};

constexpr WaveformCodec RANDOM_WAVEFORM{"random",
                                        RANDOM_KEYS,
                                        &Core::Source::GetRandom,
                                        &Core::Source::SetRandom,
                                        &Core::Source::IsValidRandom,
                                        "has random values with a time step that is not positive, a negative delay or "
                                        "a first parameter that is not valid for the distribution"};

/**
 * @brief   Checks the numbers read for a waveform with the source and hands them over.
 */
template <const auto &Codec, typename Parameters>
std::expected<void, std::string> ApplyWaveform(Core::Source &source, const Parameters &parameters) {
    if (!(source.*Codec.IsValid)(parameters)) {
        return std::unexpected(Codec.Message);
    }
    (source.*Codec.Set)(parameters);
    return {};
}

template <const auto &Codec>
void WriteWaveform(const Core::Source &source, nlohmann::json &object) {
    WriteParametersObject(Codec.Keys, (source.*Codec.Get)(), Codec.Key, object);
}

/**
 * @brief   Nothing to read when the file has no key for the waveform.
 */
template <const auto &Codec>
std::expected<void, std::string> ReadWaveform(const nlohmann::json &object, Core::Source &source) {
    auto parameters = (source.*Codec.Get)();
    if (const auto read = ReadParametersObject(Codec.Keys,
                                               object,
                                               Codec.Key,
                                               std::format("has a {} that is not an object", Codec.Key),
                                               parameters);
        !read) {
        return read;
    }
    return ApplyWaveform<Codec>(source, parameters);
}

void WriteSourceType(const Core::Source &source, nlohmann::json &object) {
    object["source"] = Core::GetSourceTypeName(source.GetSourceType());
}

std::expected<void, std::string> ReadSourceType(const nlohmann::json &object, Core::Source &source) {
    std::string type_name;
    const std::expected<bool, std::string> has_type =
        ReadOptionalString(object,
                           "source",
                           type_name,
                           "has a source type other than DC, AC, Pulse, EXP, SFFM, AM, PWL, Noise or Random");
    if (!has_type) {
        return std::unexpected(has_type.error());
    }
    if (!*has_type) {
        return {};
    }
    const std::optional<Core::Source::SourceType> type = Core::ParseSourceType(type_name);
    if (!type) {
        return std::unexpected("has a source type other than DC, AC, Pulse, EXP, SFFM, AM, PWL, Noise or Random");
    }
    source.SetSourceType(*type);
    return {};
}

void WriteAcWaveform(const Core::Source &source, nlohmann::json &object) {
    WriteParameters(AC_KEYS, source.GetAC(), object);
}

std::expected<void, std::string> ReadAcWaveform(const nlohmann::json &object, Core::Source &source) {
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
    return {};
}

void WritePwlWaveform(const Core::Source &source, nlohmann::json &object) {
    nlohmann::json pwl = nlohmann::json::array();
    for (const Core::PWLPoint &point : source.GetPWL().Points) {
        pwl.push_back({{"time", point.Time}, {"value", point.Value}});
    }
    object["pwl"] = pwl;
}

std::expected<void, std::string> ReadPwlWaveform(const nlohmann::json &object, Core::Source &source) {
    const auto points = object.find("pwl");
    if (points == object.end()) {
        return {};
    }
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
        return std::unexpected("has piecewise linear points that are fewer than two, negative or not increasing");
    }
    source.SetPWL(pwl);
    return {};
}

void WriteRandomWaveform(const Core::Source &source, nlohmann::json &object) {
    WriteWaveform<RANDOM_WAVEFORM>(source, object);
    object["random"]["distribution"] = Core::GetRandomDistributionName(source.GetRandom().Distribution);
}

std::expected<void, std::string> ReadRandomWaveform(const nlohmann::json &object, Core::Source &source) {
    Core::RandomParameters random = source.GetRandom();
    if (const auto read = ReadParametersObject(RANDOM_WAVEFORM.Keys,
                                               object,
                                               RANDOM_WAVEFORM.Key,
                                               "has a random that is not an object",
                                               random);
        !read) {
        return read;
    }
    if (const auto entry = object.find(RANDOM_WAVEFORM.Key); entry != object.end()) {
        std::string name;
        const std::expected<bool, std::string> has_distribution =
            ReadOptionalString(*entry,
                               "distribution",
                               name,
                               "has a random distribution other than Uniform, Gaussian, Exponential or Poisson");
        if (!has_distribution) {
            return std::unexpected(has_distribution.error());
        }
        if (*has_distribution) {
            const std::optional<Core::RandomDistribution> distribution = Core::ParseRandomDistribution(name);
            if (!distribution) {
                return std::unexpected(
                    "has a random distribution other than Uniform, Gaussian, Exponential or Poisson");
            }
            random.Distribution = *distribution;
        }
    }
    return ApplyWaveform<RANDOM_WAVEFORM>(source, random);
}

/**
 * @struct  SourceCodec
 * @brief   The functions that write and read one part of what a source carries.
 */
struct SourceCodec {
    void (*Write)(const Core::Source &, nlohmann::json &);
    std::expected<void, std::string> (*Read)(const nlohmann::json &, Core::Source &);
};

/// In the order the parts are read, which decides which problem a damaged file reports first.
constexpr auto SOURCE_CODECS = std::to_array<SourceCodec>({
    {&WriteSourceType, &ReadSourceType},
    {&WriteAcWaveform, &ReadAcWaveform},
    {&WriteWaveform<PULSE_WAVEFORM>, &ReadWaveform<PULSE_WAVEFORM>},
    {&WriteWaveform<EXP_WAVEFORM>, &ReadWaveform<EXP_WAVEFORM>},
    {&WriteWaveform<SFFM_WAVEFORM>, &ReadWaveform<SFFM_WAVEFORM>},
    {&WriteWaveform<AM_WAVEFORM>, &ReadWaveform<AM_WAVEFORM>},
    {&WritePwlWaveform, &ReadPwlWaveform},
    {&WriteWaveform<NOISE_WAVEFORM>, &ReadWaveform<NOISE_WAVEFORM>},
    {&WriteRandomWaveform, &ReadRandomWaveform},
});

} // namespace

/**
 * @brief   Writes every waveform of a source, so switching the type after loading keeps the parameters of the others.
 * @param[in]     component  Part to write; nothing is written unless it is a source.
 * @param[in,out] object     Element object that receives the keys.
 */
void WriteSourceFields(const Core::Component &component, nlohmann::json &object) {
    if (!Core::IsSource(component.GetType())) {
        return;
    }
    const auto &source = static_cast<const Core::Source &>(component);
    for (const SourceCodec &codec : SOURCE_CODECS) {
        codec.Write(source, object);
    }
}

/**
 * @brief   Reads the waveforms of a source; every waveform key is optional, so files saved before a waveform existed
 *          keep the default one of the source.
 * @param[in]     object     Element object to read.
 * @param[in,out] component  Part that receives the waveforms; nothing is read unless it is a source.
 * @return  Nothing on success, or what is wrong with the source, to follow "Element 3 ...".
 */
std::expected<void, std::string> ReadSourceFields(const nlohmann::json &object, Core::Component &component) {
    if (!Core::IsSource(component.GetType())) {
        return {};
    }
    auto &source = static_cast<Core::Source &>(component);
    for (const SourceCodec &codec : SOURCE_CODECS) {
        if (const auto read = codec.Read(object, source); !read) {
            return read;
        }
    }
    return {};
}

} // namespace GUI
