/**
 * @file    file_settings.cpp
 * @brief   Writes the analysis settings of a schematic to JSON and reads them back.
 */

#include "schematic/file_settings.h"

#include "nlohmann/json.hpp"
#include "schematic/file_json.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <limits>
#include <string_view>
#include <utility>

namespace GUI {

namespace {

constexpr auto TRANSIENT_KEYS = std::to_array<ParameterKey<Core::TransientSettings>>({
    {"stop", &Core::TransientSettings::StopTime},
    {"step", &Core::TransientSettings::TimeStep},
});

/// The points per decade are an integer, so they are written and read apart.
constexpr auto AC_SWEEP_KEYS = std::to_array<ParameterKey<Core::ACSweepSettings>>({
    {"start", &Core::ACSweepSettings::StartFrequency},
    {"stop", &Core::ACSweepSettings::StopFrequency},
});

/// The sweep type and the points are written and read apart.
constexpr auto S_PARAMETER_KEYS = std::to_array<ParameterKey<Core::SParameterSettings>>({
    {"start", &Core::SParameterSettings::StartFrequency},
    {"stop", &Core::SParameterSettings::StopFrequency},
});

/// The harmonic count and oversampling samples are integers, so they are written and read apart.
constexpr auto HARMONIC_BALANCE_KEYS = std::to_array<ParameterKey<Core::HarmonicBalanceSettings>>({
    {"fundamental", &Core::HarmonicBalanceSettings::FundamentalFrequency},
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

nlohmann::json WriteSweepRange(const Core::SweepRange &range) {
    nlohmann::json object = {{"source", range.Source}};
    WriteParameters(SWEEP_RANGE_KEYS, range, object);
    return object;
}

/// File names of the analyses, in the order of the Analysis enum.
constexpr auto ANALYSIS_KEYS = std::to_array<std::string_view>(
    {"operating_point", "transient", "ac_sweep", "dc_sweep", "noise", "transfer_function", "sensitivity", "pole_zero",
     "s_parameters", "truth_table", "harmonic_balance"});

const char *ToFileName(const Core::ToleranceDistribution distribution) {
    return distribution == Core::ToleranceDistribution::UNIFORM ? "uniform" : "gaussian";
}

std::optional<Core::ToleranceDistribution> ParseDistributionName(const std::string_view name) {
    if (name == "gaussian") {
        return Core::ToleranceDistribution::GAUSSIAN;
    }
    if (name == "uniform") {
        return Core::ToleranceDistribution::UNIFORM;
    }
    return std::nullopt;
}

nlohmann::json WriteHarmonicBalanceSettings(const Core::HarmonicBalanceSettings &settings) {
    nlohmann::json object = nlohmann::json::object();
    WriteParameters(HARMONIC_BALANCE_KEYS, settings, object);
    object["harmonics"] = settings.Harmonics;
    object["samples"] = settings.Samples;
    return object;
}

nlohmann::json WriteSParameterSettings(const Core::SParameterSettings &settings) {
    nlohmann::json object = {{"logarithmic", settings.IsLogarithmic},
                             {"points_per_decade", settings.PointsPerDecade},
                             {"linear_points", settings.LinearPoints}};
    WriteParameters(S_PARAMETER_KEYS, settings, object);
    return object;
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
    if (object.contains("seed")) {
        const std::optional<int> seed = ReadInt(object, "seed");
        if (!seed || *seed < 0) {
            return std::unexpected("have no valid seed");
        }
        settings.Transient.Seed = static_cast<unsigned int>(*seed);
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

std::expected<void, std::string> ReadPoleZeroSettings(const nlohmann::json &object, SimulationSettings &settings) {
    if (object.contains("source")) {
        const std::optional<std::string> source = ReadString(object, "source");
        if (!source) {
            return std::unexpected("have no valid source");
        }
        settings.PoleZeroSource = *source;
    }
    if (object.contains("output")) {
        const std::optional<GridPoint> output = ReadPoint(object, "output");
        if (!output) {
            return std::unexpected("have no valid output point");
        }
        settings.PoleZeroOutput = *output;
    }
    return {};
}

std::expected<void, std::string> ReadHarmonicBalanceSettings(const nlohmann::json &object,
                                                             SimulationSettings &settings) {
    Core::HarmonicBalanceSettings range;
    if (const auto read = ReadParameters(HARMONIC_BALANCE_KEYS, object, range); !read) {
        return std::unexpected(read.error());
    }
    if (object.contains("harmonics")) {
        const std::optional<int> harmonics = ReadInt(object, "harmonics");
        if (!harmonics) {
            return std::unexpected("has no valid harmonics");
        }
        range.Harmonics = *harmonics;
    }
    if (object.contains("samples")) {
        const std::optional<int> samples = ReadInt(object, "samples");
        if (!samples) {
            return std::unexpected("has no valid samples");
        }
        range.Samples = *samples;
    }
    if (std::optional<std::string> error = Core::CheckHarmonicBalanceGrid(range)) {
        error->front() = static_cast<char>(std::tolower(static_cast<unsigned char>(error->front())));
        return std::unexpected(std::format("are out of range ({})", *error));
    }
    settings.HarmonicBalance = range;
    return {};
}

std::expected<void, std::string> ReadSParameterSettings(const nlohmann::json &object, SimulationSettings &settings) {
    Core::SParameterSettings &range = settings.SParameters;
    if (const auto read = ReadParameters(S_PARAMETER_KEYS, object, range); !read) {
        return read;
    }
    if (range.StartFrequency <= 0.0 || range.StopFrequency <= 0.0) {
        return std::unexpected("have a frequency that is not positive");
    }
    if (object.contains("logarithmic")) {
        const std::optional<bool> is_logarithmic = ReadBool(object, "logarithmic");
        if (!is_logarithmic) {
            return std::unexpected("have a logarithmic flag that is not true or false");
        }
        range.IsLogarithmic = *is_logarithmic;
    }
    if (object.contains("points_per_decade")) {
        const std::optional<int> points = ReadInt(object, "points_per_decade");
        if (!points || *points < 1) {
            return std::unexpected("have no valid points_per_decade");
        }
        range.PointsPerDecade = *points;
    }
    if (object.contains("linear_points")) {
        const std::optional<int> points = ReadInt(object, "linear_points");
        if (!points || *points < 3) {
            return std::unexpected("have no valid linear_points");
        }
        range.LinearPoints = *points;
    }
    return {};
}

std::expected<void, std::string> ReadSensitivitySettings(const nlohmann::json &object, SimulationSettings &settings) {
    if (object.contains("output")) {
        const std::optional<GridPoint> output = ReadPoint(object, "output");
        if (!output) {
            return std::unexpected("have no valid output point");
        }
        settings.SensitivityOutput = *output;
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

std::expected<void, std::string> ReadMonteCarloSettings(const nlohmann::json &object, SimulationSettings &settings) {
    Core::MonteCarloSettings &monte_carlo = settings.MonteCarlo;
    if (object.contains("enabled")) {
        const std::optional<bool> enabled = ReadBool(object, "enabled");
        if (!enabled) {
            return std::unexpected("have an enabled flag that is not true or false");
        }
        monte_carlo.Enabled = *enabled;
    }
    if (object.contains("runs")) {
        const std::optional<int> runs = ReadInt(object, "runs");
        if (!runs || *runs < Core::MIN_MONTE_CARLO_RUNS || *runs > Core::MAX_MONTE_CARLO_RUNS) {
            return std::unexpected(
                std::format("have no valid runs, {} to {}", Core::MIN_MONTE_CARLO_RUNS, Core::MAX_MONTE_CARLO_RUNS));
        }
        monte_carlo.Runs = *runs;
    }
    if (object.contains("distribution")) {
        const std::optional<std::string> name = ReadString(object, "distribution");
        const std::optional<Core::ToleranceDistribution> distribution =
            name ? ParseDistributionName(*name) : std::nullopt;
        if (!distribution) {
            return std::unexpected("have an unknown distribution");
        }
        monte_carlo.Distribution = *distribution;
    }
    if (object.contains("seed")) {
        const std::optional<double> seed = ReadNumber(object, "seed");
        if (!seed || *seed < 0.0 || *seed > static_cast<double>(std::numeric_limits<unsigned int>::max()) ||
            *seed != std::floor(*seed)) {
            return std::unexpected("have no valid seed");
        }
        monte_carlo.Seed = static_cast<unsigned int>(*seed);
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
    {"sensitivity", "sensitivity", &ReadSensitivitySettings},
    {"pole_zero", "pole-zero", &ReadPoleZeroSettings},
    {"s_parameters", "S-parameter", &ReadSParameterSettings},
    {"truth_table", "truth table", &ReadTruthTableSettings},
    {"harmonic_balance", "harmonic balance", &ReadHarmonicBalanceSettings},
    {"parameter_step", "parameter step", &ReadParameterStepSettings},
    {"monte_carlo", "Monte Carlo", &ReadMonteCarloSettings},
});

} // namespace

/**
 * @brief   Writes the settings of every analysis.
 * @param[in] settings  Settings to write.
 * @return  An object with one entry for each analysis and the selected one.
 */
nlohmann::json WriteSettings(const SimulationSettings &settings) {
    nlohmann::json transient = nlohmann::json::object();
    WriteParameters(TRANSIENT_KEYS, settings.Transient, transient);
    transient["live_seconds"] = settings.LiveSeconds;
    transient["seed"] = settings.Transient.Seed;
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
    nlohmann::json sensitivity = nlohmann::json::object();
    if (settings.SensitivityOutput) {
        sensitivity["output"] = {settings.SensitivityOutput->X, settings.SensitivityOutput->Y};
    }
    nlohmann::json pole_zero = {{"source", settings.PoleZeroSource}};
    if (settings.PoleZeroOutput) {
        pole_zero["output"] = {settings.PoleZeroOutput->X, settings.PoleZeroOutput->Y};
    }
    const nlohmann::json truth_table = {{"inputs", settings.TruthTableInputs}};
    const ParameterStepSettings &step = settings.ParameterStep;
    nlohmann::json parameter_step = {
        {"enabled", step.Enabled}, {"part", step.Part}, {"uses_list", step.UsesList}, {"list", step.List}};
    WriteParameters(PARAMETER_STEP_KEYS, step, parameter_step);
    const Core::MonteCarloSettings &monte_carlo = settings.MonteCarlo;
    const nlohmann::json monte_carlo_object = {
        {"enabled", monte_carlo.Enabled},
        {"runs", monte_carlo.Runs},
        {"distribution", ToFileName(monte_carlo.Distribution)},
        {"seed", monte_carlo.Seed},
    };
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
        {"sensitivity", sensitivity},
        {"pole_zero", pole_zero},
        {"s_parameters", WriteSParameterSettings(settings.SParameters)},
        {"truth_table", truth_table},
        {"harmonic_balance", WriteHarmonicBalanceSettings(settings.HarmonicBalance)},
        {"parameter_step", parameter_step},
        {"monte_carlo", monte_carlo_object},
    };
}

/**
 * @brief   Reads the settings of every analysis.
 * @details Files saved before settings were stored have none and get the defaults; an analysis with invalid settings
 *          falls back to its defaults without affecting the others.
 * @param[in]     document  Document object that holds the "simulation" key.
 * @param[in,out] warnings  Receives one message for each analysis that was reset.
 * @return  The settings read.
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

} // namespace GUI
