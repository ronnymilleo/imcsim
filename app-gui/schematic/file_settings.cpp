/**
 * @file    file_settings.cpp
 * @brief   Writes the analysis settings of a schematic to JSON and reads them back.
 */

#include "schematic/file_settings.h"

#include "nlohmann/json.hpp"
#include "schematic/file_json.h"
#include "simulation/check_harmonic_balance.h"
#include "simulation/check_solver_options.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <format>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace GUI {

namespace {

/// File names of the analyses, in the order of the Analysis enum.
constexpr auto ANALYSIS_KEYS = std::to_array<std::string_view>({"operating_point",
                                                                "transient",
                                                                "ac_sweep",
                                                                "dc_sweep",
                                                                "noise",
                                                                "transfer_function",
                                                                "sensitivity",
                                                                "pole_zero",
                                                                "s_parameters",
                                                                "truth_table",
                                                                "harmonic_balance"});

/// Fewest points of a linear S-parameter sweep a file may hold: Nodalys gives two, and a run on ngspice, which needs
/// more, says so when it starts.
constexpr int MIN_FILE_LINEAR_S_POINTS = 2;

constexpr int MIN_POINTS_PER_DECADE = 1;

constexpr auto TRANSIENT_KEYS = std::to_array<ParameterKey<Core::TransientSettings>>({
    {"stop", &Core::TransientSettings::StopTime},
    {"step", &Core::TransientSettings::TimeStep},
});

/// The points per decade are an integer, so they are written and read apart.
constexpr auto AC_SWEEP_KEYS = std::to_array<ParameterKey<Core::ACSweepSettings>>({
    {"start", &Core::ACSweepSettings::StartFrequency},
    {"stop", &Core::ACSweepSettings::StopFrequency},
});

constexpr auto SWEEP_RANGE_KEYS = std::to_array<ParameterKey<Core::SweepRange>>({
    {"start", &Core::SweepRange::Start},
    {"stop", &Core::SweepRange::Stop},
    {"step", &Core::SweepRange::Step},
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

constexpr auto SOLVER_KEYS = std::to_array<ParameterKey<Core::SolverOptions>>({
    {"reltol", &Core::SolverOptions::RelativeTolerance},
    {"abstol", &Core::SolverOptions::CurrentToleranceAmperes},
    {"vntol", &Core::SolverOptions::VoltageToleranceVolts},
    {"tnom", &Core::SolverOptions::NominalTemperatureCelsius},
});

// Names of the enums

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

const char *ToFileName(const Core::IntegrationMethod method) {
    switch (method) {
    case Core::IntegrationMethod::TRAPEZOIDAL:
        return "trapezoidal";
    case Core::IntegrationMethod::GEAR:
        return "gear";
    case Core::IntegrationMethod::DEFAULT:
    default:
        return "default";
    }
}

std::optional<Core::IntegrationMethod> ParseMethodName(const std::string_view name) {
    for (const Core::IntegrationMethod method :
         {Core::IntegrationMethod::DEFAULT, Core::IntegrationMethod::TRAPEZOIDAL, Core::IntegrationMethod::GEAR}) {
        if (name == ToFileName(method)) {
            return method;
        }
    }
    return std::nullopt;
}

/**
 * @brief   Lower-cases the first letter of a check's message, so it reads inside "are out of range (...)".
 */
std::string DescribeOutOfRange(std::string error) {
    error.front() = static_cast<char>(std::tolower(static_cast<unsigned char>(error.front())));
    return std::format("are out of range ({})", error);
}

// Shared parts of the analyses

/**
 * @brief   The frequency range of the AC sweep, the noise analysis and the AC sensitivity: two bounds and the points
 *          per decade.
 */
void WriteFrequencyRange(const Core::ACSweepSettings &range, nlohmann::json &object) {
    WriteParameters(AC_SWEEP_KEYS, range, object);
    object["points_per_decade"] = range.PointsPerDecade;
}

/**
 * @brief   Reads the two bounds of a frequency range, which must be positive; the S-parameter sweep shares this step.
 */
template <typename Range, std::size_t Count>
std::expected<void, std::string>
ReadFrequencyBounds(const std::array<ParameterKey<Range>, Count> &keys, const nlohmann::json &object, Range &range) {
    if (const auto read = ReadParameters(keys, object, range); !read) {
        return read;
    }
    if (range.StartFrequency <= 0.0 || range.StopFrequency <= 0.0) {
        return std::unexpected("have a frequency that is not positive");
    }
    return {};
}

std::expected<bool, std::string> ReadPointsPerDecade(const nlohmann::json &object, int &points_per_decade) {
    return ReadOptionalInt(object,
                           "points_per_decade",
                           points_per_decade,
                           "have no valid points_per_decade",
                           MIN_POINTS_PER_DECADE);
}

std::expected<void, std::string> ReadFrequencyRange(const nlohmann::json &object, Core::ACSweepSettings &range) {
    if (const auto read = ReadFrequencyBounds(AC_SWEEP_KEYS, object, range); !read) {
        return read;
    }
    return FirstError({ReadPointsPerDecade(object, range.PointsPerDecade)});
}

/**
 * @brief   The part of the noise, transfer function and pole-zero analyses that names the input source and the output
 *          point.
 */
void WriteSourceAndOutput(const std::string &source, const std::optional<GridPoint> &output, nlohmann::json &object) {
    object["source"] = source;
    if (output) {
        object["output"] = {output->X, output->Y};
    }
}

std::expected<void, std::string>
ReadSourceAndOutput(const nlohmann::json &object, std::string &source, std::optional<GridPoint> &output) {
    return FirstError({ReadOptionalString(object, "source", source, "have no valid source"),
                       ReadOptionalPoint(object, "output", output, "have no valid output point")});
}

// Transient

nlohmann::json WriteTransientSettings(const SimulationSettings &settings) {
    nlohmann::json object = nlohmann::json::object();
    WriteParameters(TRANSIENT_KEYS, settings.Transient, object);
    object["live_seconds"] = settings.LiveSeconds;
    object["seed"] = settings.Transient.Seed;
    return object;
}

/**
 * @brief   The analysis readers change only their own part of the settings; missing keys keep its value.
 */
std::expected<void, std::string> ReadTransientSettings(const nlohmann::json &object, SimulationSettings &settings) {
    if (const auto read = ReadParameters(TRANSIENT_KEYS, object, settings.Transient); !read) {
        return read;
    }
    if (settings.Transient.StopTime <= 0.0 || settings.Transient.TimeStep <= 0.0) {
        return std::unexpected("have a time that is not positive");
    }
    if (const auto read = ReadOptionalNumber(object,
                                             "live_seconds",
                                             settings.LiveSeconds,
                                             "have no valid live_seconds",
                                             &IsValidLiveSeconds);
        !read) {
        return std::unexpected(read.error());
    }
    int seed = 0;
    const std::expected<bool, std::string> has_seed = ReadOptionalInt(object, "seed", seed, "have no valid seed", 0);
    if (!has_seed) {
        return std::unexpected(has_seed.error());
    }
    if (*has_seed) {
        settings.Transient.Seed = static_cast<unsigned int>(seed);
    }
    return {};
}

// AC sweep

nlohmann::json WriteACSweepSettings(const SimulationSettings &settings) {
    nlohmann::json object = nlohmann::json::object();
    WriteFrequencyRange(settings.ACSweep, object);
    return object;
}

std::expected<void, std::string> ReadACSweepSettings(const nlohmann::json &object, SimulationSettings &settings) {
    return ReadFrequencyRange(object, settings.ACSweep);
}

// DC sweep

nlohmann::json WriteSweepRange(const Core::SweepRange &range) {
    nlohmann::json object = {{"source", range.Source}};
    WriteParameters(SWEEP_RANGE_KEYS, range, object);
    return object;
}

std::expected<void, std::string> ReadSweepRange(const nlohmann::json &object, Core::SweepRange &range) {
    if (!object.is_object()) {
        return std::unexpected("have a range that is not an object");
    }
    if (const auto read = ReadOptionalString(object, "source", range.Source, "have no valid source"); !read) {
        return std::unexpected(read.error());
    }
    if (const auto read = ReadParameters(SWEEP_RANGE_KEYS, object, range); !read) {
        return read;
    }
    if (range.Step == 0.0) {
        return std::unexpected("have a step of zero");
    }
    return {};
}

nlohmann::json WriteDCSweepSettings(const SimulationSettings &settings) {
    return {
        {"swept", WriteSweepRange(settings.SweptRange)},
        {"stepped", WriteSweepRange(settings.SteppedRange)},
        {"step_source", settings.StepSource},
    };
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
    return FirstError({ReadOptionalBool(object,
                                        "step_source",
                                        settings.StepSource,
                                        "have a step_source flag that is not true or false")});
}

// Noise

nlohmann::json WriteNoiseSettings(const SimulationSettings &settings) {
    nlohmann::json object = nlohmann::json::object();
    WriteFrequencyRange(settings.NoiseRange, object);
    WriteSourceAndOutput(settings.NoiseSource, settings.NoiseOutput, object);
    return object;
}

std::expected<void, std::string> ReadNoiseSettings(const nlohmann::json &object, SimulationSettings &settings) {
    if (const auto read = ReadFrequencyRange(object, settings.NoiseRange); !read) {
        return read;
    }
    return ReadSourceAndOutput(object, settings.NoiseSource, settings.NoiseOutput);
}

// Transfer function

nlohmann::json WriteTransferFunctionSettings(const SimulationSettings &settings) {
    nlohmann::json object = nlohmann::json::object();
    WriteSourceAndOutput(settings.TransferFunctionSource, settings.TransferFunctionOutput, object);
    return object;
}

std::expected<void, std::string> ReadTransferFunctionSettings(const nlohmann::json &object,
                                                              SimulationSettings &settings) {
    return ReadSourceAndOutput(object, settings.TransferFunctionSource, settings.TransferFunctionOutput);
}

// Sensitivity

nlohmann::json WriteSensitivitySettings(const SimulationSettings &settings) {
    nlohmann::json object = {{"kind", settings.SensitivityType == SensitivityKind::AC ? "ac" : "dc"}};
    if (settings.SensitivityOutput) {
        object["output"] = {settings.SensitivityOutput->X, settings.SensitivityOutput->Y};
    }
    nlohmann::json ac = {{"source", settings.ACSensitivitySource}};
    WriteFrequencyRange(settings.ACSensitivityRange, ac);
    object["ac"] = std::move(ac);
    return object;
}

std::expected<void, std::string> ReadSensitivitySettings(const nlohmann::json &object, SimulationSettings &settings) {
    if (const auto read = ReadOptionalPoint(object, "output", settings.SensitivityOutput, "have no valid output point");
        !read) {
        return std::unexpected(read.error());
    }
    std::string kind;
    const std::expected<bool, std::string> has_kind =
        ReadOptionalString(object, "kind", kind, "have a kind that is neither dc nor ac");
    if (!has_kind) {
        return std::unexpected(has_kind.error());
    }
    if (*has_kind) {
        if (kind != "dc" && kind != "ac") {
            return std::unexpected("have a kind that is neither dc nor ac");
        }
        settings.SensitivityType = kind == "ac" ? SensitivityKind::AC : SensitivityKind::DC;
    }
    if (const auto ac = object.find("ac"); ac != object.end()) {
        if (const auto read = ReadFrequencyRange(*ac, settings.ACSensitivityRange); !read) {
            return read;
        }
        return FirstError({ReadOptionalString(*ac, "source", settings.ACSensitivitySource, "have no valid source")});
    }
    return {};
}

// Pole-zero

nlohmann::json WritePoleZeroSettings(const SimulationSettings &settings) {
    nlohmann::json object = nlohmann::json::object();
    WriteSourceAndOutput(settings.PoleZeroSource, settings.PoleZeroOutput, object);
    return object;
}

std::expected<void, std::string> ReadPoleZeroSettings(const nlohmann::json &object, SimulationSettings &settings) {
    return ReadSourceAndOutput(object, settings.PoleZeroSource, settings.PoleZeroOutput);
}

// S-parameters

nlohmann::json WriteSParameterSettings(const SimulationSettings &settings) {
    const Core::SParameterSettings &range = settings.SParameters;
    nlohmann::json object = {{"logarithmic", range.IsLogarithmic},
                             {"points_per_decade", range.PointsPerDecade},
                             {"linear_points", range.LinearPoints}};
    WriteParameters(S_PARAMETER_KEYS, range, object);
    return object;
}

std::expected<void, std::string> ReadSParameterSettings(const nlohmann::json &object, SimulationSettings &settings) {
    Core::SParameterSettings &range = settings.SParameters;
    if (const auto read = ReadFrequencyBounds(S_PARAMETER_KEYS, object, range); !read) {
        return read;
    }
    return FirstError({ReadOptionalBool(object,
                                        "logarithmic",
                                        range.IsLogarithmic,
                                        "have a logarithmic flag that is not true or false"),
                       ReadPointsPerDecade(object, range.PointsPerDecade),
                       ReadOptionalInt(object,
                                       "linear_points",
                                       range.LinearPoints,
                                       "have no valid linear_points",
                                       MIN_FILE_LINEAR_S_POINTS)});
}

// Truth table

nlohmann::json WriteTruthTableSettings(const SimulationSettings &settings) {
    return {{"inputs", settings.TruthTableInputs}};
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

// Harmonic balance

nlohmann::json WriteHarmonicBalanceSettings(const SimulationSettings &settings) {
    nlohmann::json object = nlohmann::json::object();
    WriteParameters(HARMONIC_BALANCE_KEYS, settings.HarmonicBalance, object);
    object["harmonics"] = settings.HarmonicBalance.Harmonics;
    object["samples"] = settings.HarmonicBalance.Samples;
    return object;
}

std::expected<void, std::string> ReadHarmonicBalanceSettings(const nlohmann::json &object,
                                                             SimulationSettings &settings) {
    Core::HarmonicBalanceSettings range;
    if (const auto read = ReadParameters(HARMONIC_BALANCE_KEYS, object, range); !read) {
        return read;
    }
    if (const auto read = FirstError({ReadOptionalInt(object, "harmonics", range.Harmonics, "has no valid harmonics"),
                                      ReadOptionalInt(object, "samples", range.Samples, "has no valid samples")});
        !read) {
        return read;
    }
    if (std::optional<std::string> error = Core::CheckHarmonicBalanceGrid(range)) {
        return std::unexpected(DescribeOutOfRange(std::move(*error)));
    }
    settings.HarmonicBalance = range;
    return {};
}

// Parameter step

nlohmann::json WriteParameterStepSettings(const SimulationSettings &settings) {
    const ParameterStepSettings &step = settings.ParameterStep;
    nlohmann::json object = {{"enabled", step.Enabled},
                             {"part", step.Part},
                             {"uses_list", step.UsesList},
                             {"list", step.List}};
    WriteParameters(PARAMETER_STEP_KEYS, step, object);
    return object;
}

std::expected<void, std::string> ReadParameterStepSettings(const nlohmann::json &object, SimulationSettings &settings) {
    ParameterStepSettings &step = settings.ParameterStep;
    if (const auto read = ReadParameters(PARAMETER_STEP_KEYS, object, step); !read) {
        return read;
    }
    for (const auto &[key, flag] : {std::pair{"enabled", &step.Enabled}, std::pair{"uses_list", &step.UsesList}}) {
        if (const auto read =
                ReadOptionalBool(object, key, *flag, std::format("have a {} flag that is not true or false", key));
            !read) {
            return std::unexpected(read.error());
        }
    }
    for (const auto &[key, text] : {std::pair{"part", &step.Part}, std::pair{"list", &step.List}}) {
        if (const auto read = ReadOptionalString(object, key, *text, std::format("have no valid {}", key)); !read) {
            return std::unexpected(read.error());
        }
    }
    return {};
}

// Monte Carlo

/**
 * @brief   A seed is a whole number that fits the generator, which a JSON number may not be.
 */
bool IsValidMonteCarloSeed(const double seed) {
    return seed >= 0.0 && seed <= static_cast<double>(std::numeric_limits<unsigned int>::max())
        && seed == std::floor(seed);
}

nlohmann::json WriteMonteCarloSettings(const SimulationSettings &settings) {
    const Core::MonteCarloSettings &monte_carlo = settings.MonteCarlo;
    return {
        {"enabled", monte_carlo.Enabled},
        {"runs", monte_carlo.Runs},
        {"distribution", ToFileName(monte_carlo.Distribution)},
        {"seed", monte_carlo.Seed},
    };
}

std::expected<void, std::string> ReadMonteCarloSettings(const nlohmann::json &object, SimulationSettings &settings) {
    Core::MonteCarloSettings &monte_carlo = settings.MonteCarlo;
    if (const auto read = FirstError(
            {ReadOptionalBool(object, "enabled", monte_carlo.Enabled, "have an enabled flag that is not true or false"),
             ReadOptionalInt(
                 object,
                 "runs",
                 monte_carlo.Runs,
                 std::format("have no valid runs, {} to {}", Core::MIN_MONTE_CARLO_RUNS, Core::MAX_MONTE_CARLO_RUNS),
                 Core::MIN_MONTE_CARLO_RUNS,
                 Core::MAX_MONTE_CARLO_RUNS)});
        !read) {
        return read;
    }
    std::string distribution_name;
    const std::expected<bool, std::string> has_distribution =
        ReadOptionalString(object, "distribution", distribution_name, "have an unknown distribution");
    if (!has_distribution) {
        return std::unexpected(has_distribution.error());
    }
    if (*has_distribution) {
        const std::optional<Core::ToleranceDistribution> distribution = ParseDistributionName(distribution_name);
        if (!distribution) {
            return std::unexpected("have an unknown distribution");
        }
        monte_carlo.Distribution = *distribution;
    }
    double seed = 0.0;
    const std::expected<bool, std::string> has_seed =
        ReadOptionalNumber(object, "seed", seed, "have no valid seed", &IsValidMonteCarloSeed);
    if (!has_seed) {
        return std::unexpected(has_seed.error());
    }
    if (*has_seed) {
        monte_carlo.Seed = static_cast<unsigned int>(seed);
    }
    return {};
}

// Solver

nlohmann::json WriteSolverSettings(const SimulationSettings &settings) {
    nlohmann::json object = {{"method", ToFileName(settings.Solver.Method)}};
    WriteParameters(SOLVER_KEYS, settings.Solver, object);
    return object;
}

std::expected<void, std::string> ReadSolverSettings(const nlohmann::json &object, SimulationSettings &settings) {
    Core::SolverOptions options = settings.Solver;
    if (const auto read = ReadParameters(SOLVER_KEYS, object, options); !read) {
        return read;
    }
    std::string method_name;
    const std::expected<bool, std::string> has_method =
        ReadOptionalString(object, "method", method_name, "have an unknown integration method");
    if (!has_method) {
        return std::unexpected(has_method.error());
    }
    if (*has_method) {
        const std::optional<Core::IntegrationMethod> method = ParseMethodName(method_name);
        if (!method) {
            return std::unexpected("have an unknown integration method");
        }
        options.Method = *method;
    }
    if (std::optional<std::string> error = Core::CheckSolverOptions(options)) {
        return std::unexpected(DescribeOutOfRange(std::move(*error)));
    }
    settings.Solver = options;
    return {};
}

// Table of the analyses

/**
 * @struct  AnalysisCodec
 * @brief   JSON key of the settings of one analysis, its name in warnings, and the functions that write and read
 *          them, so a field is never added to one side only.
 */
struct AnalysisCodec {
    const char *Key;
    const char *Label;
    nlohmann::json (*Write)(const SimulationSettings &);
    std::expected<void, std::string> (*Read)(const nlohmann::json &, SimulationSettings &);
};

constexpr auto ANALYSIS_CODECS = std::to_array<AnalysisCodec>({
    {"transient", "transient", &WriteTransientSettings, &ReadTransientSettings},
    {"ac_sweep", "AC sweep", &WriteACSweepSettings, &ReadACSweepSettings},
    {"dc_sweep", "DC sweep", &WriteDCSweepSettings, &ReadDCSweepSettings},
    {"noise", "noise", &WriteNoiseSettings, &ReadNoiseSettings},
    {"transfer_function", "transfer function", &WriteTransferFunctionSettings, &ReadTransferFunctionSettings},
    {"sensitivity", "sensitivity", &WriteSensitivitySettings, &ReadSensitivitySettings},
    {"pole_zero", "pole-zero", &WritePoleZeroSettings, &ReadPoleZeroSettings},
    {"s_parameters", "S-parameter", &WriteSParameterSettings, &ReadSParameterSettings},
    {"truth_table", "truth table", &WriteTruthTableSettings, &ReadTruthTableSettings},
    {"harmonic_balance", "harmonic balance", &WriteHarmonicBalanceSettings, &ReadHarmonicBalanceSettings},
    {"parameter_step", "parameter step", &WriteParameterStepSettings, &ReadParameterStepSettings},
    {"monte_carlo", "Monte Carlo", &WriteMonteCarloSettings, &ReadMonteCarloSettings},
    {"solver", "solver", &WriteSolverSettings, &ReadSolverSettings},
});

} // namespace

/**
 * @brief   Writes the settings of every analysis.
 * @param[in] settings  Settings to write.
 * @return  An object with one entry for each analysis and the selected one.
 */
nlohmann::json WriteSettings(const SimulationSettings &settings) {
    nlohmann::json object = {{"analysis", ANALYSIS_KEYS[static_cast<std::size_t>(settings.Selected)]}};
    for (const AnalysisCodec &codec : ANALYSIS_CODECS) {
        object[codec.Key] = codec.Write(settings);
    }
    return object;
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
    for (const AnalysisCodec &codec : ANALYSIS_CODECS) {
        const auto object = simulation->find(codec.Key);
        if (object == simulation->end()) {
            continue;
        }
        SimulationSettings read_settings = settings;
        std::expected<void, std::string> read = std::unexpected("are not an object");
        if (object->is_object()) {
            read = codec.Read(*object, read_settings);
        }
        if (read) {
            settings = std::move(read_settings);
        } else {
            warnings.push_back(
                std::format("The {} settings {} and were reset to the defaults", codec.Label, read.error()));
        }
    }
    return settings;
}

} // namespace GUI
