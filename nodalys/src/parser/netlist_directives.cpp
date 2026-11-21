/**
 * @file    netlist_directives.cpp
 * @brief   Reads the directives of a netlist, such as .tran and .options, into the analyses and settings it asks for.
 */

#include "parser/netlist_directives.h"

#include "parser/spice_number.h"
#include "parser/spice_text.h"
#include "text/text_case.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <format>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>

namespace Nodalys {

namespace {

/**
 * @struct  OutputNodes
 * @brief   The two nodes of an output written V(node) or V(positive, negative), by name.
 */
struct OutputNodes {
    std::string Positive;
    std::string Negative;
};

/**
 * @brief   Reads the nodes of an output written V(node) or V(positive, negative); a bare name is the node itself.
 * @return  The nodes; the negative one is ground when the output names one node.
 */
OutputNodes ReadOutputNodes(std::string_view text) {
    text = TrimSpaces(text);
    const std::string upper = ToUpper(text);
    if (!upper.starts_with("V(") || !upper.ends_with(")")) {
        return {.Positive = std::string(text), .Negative = "0"};
    }
    const std::string_view inner = text.substr(2, text.size() - 3);
    const std::size_t comma = inner.find(',');
    if (comma == std::string_view::npos) {
        return {.Positive = std::string(TrimSpaces(inner)), .Negative = "0"};
    }
    return {.Positive = std::string(TrimSpaces(inner.substr(0, comma))),
            .Negative = std::string(TrimSpaces(inner.substr(comma + 1)))};
}

// Node voltages

/**
 * @struct  Assignment
 * @brief   The two sides of a NAME=VALUE word, as written.
 */
struct Assignment {
    std::string_view Name;
    std::string_view Value;
};

/**
 * @brief   Splits a word such as "v(out)=5" at its equals sign; the tokens already join "v(out) = 5" into one word.
 * @return  The two sides, or the reason the word is not an assignment.
 */
std::expected<Assignment, std::string> SplitAssignment(const std::string_view word, const std::string_view line) {
    const std::size_t equals = word.find('=');
    if (equals == std::string_view::npos) {
        return std::unexpected(std::format("Malformed entry '{}' in .ic: '{}'", word, line));
    }
    if (equals + 1 == word.size()) {
        return std::unexpected(std::format("Missing voltage after '=' in .ic: '{}'", line));
    }
    return Assignment{.Name = word.substr(0, equals), .Value = word.substr(equals + 1)};
}

/**
 * @brief   Reads a .ic or .nodeset directive such as ".ic v(1)=5 v(out) = 0" and records each node voltage, as the
 *          initial voltage of the node or as its nodeset.
 */
std::expected<void, std::string> ReadNodeVoltages(DeckContext &context, const DeckLine &line, const bool is_nodeset) {
    for (std::size_t index = 1; index < line.Tokens.size(); ++index) {
        const auto assignment = SplitAssignment(line.Tokens[index], line.Text);
        if (!assignment) {
            return std::unexpected(assignment.error());
        }
        const std::string_view name = assignment->Name;
        if (name.size() <= 3 || std::toupper(static_cast<unsigned char>(name[0])) != 'V' || name[1] != '('
            || name.back() != ')') {
            return std::unexpected(std::format("Expected v(node)=value in .ic, got '{}': '{}'", name, line.Text));
        }
        const NodeId node = context.Nodes.Resolve(name.substr(2, name.size() - 3));
        const std::optional<double> voltage = ParseSpiceNumber(assignment->Value);
        if (!voltage) {
            return std::unexpected(std::format("Invalid voltage '{}' in .ic: '{}'", assignment->Value, line.Text));
        }
        if (is_nodeset) {
            context.Builder.SetNodeset(node, *voltage);
        } else {
            context.Builder.SetInitialVoltage(node, *voltage);
        }
    }
    return {};
}

// Frequency sweeps

/**
 * @struct  FrequencySweepWords
 * @brief   The sweep of an .ac, .noise or .sens ... ac directive as it is written.
 */
struct FrequencySweepWords {
    FrequencySweepType Type = FrequencySweepType::DECADE;
    int Points = 0;
    double StartFrequency = 0.0;
    double StopFrequency = 0.0;
};

/// The words that follow the sweep type, which are the points, the start and the stop.
constexpr std::size_t SWEEP_NUMBER_COUNT = 3;

std::optional<FrequencySweepType> FindSweepType(const std::string_view word) {
    const std::string upper = ToUpper(word);
    if (upper == "DEC") {
        return FrequencySweepType::DECADE;
    }
    if (upper == "OCT") {
        return FrequencySweepType::OCTAVE;
    }
    if (upper == "LIN") {
        return FrequencySweepType::LINEAR;
    }
    return std::nullopt;
}

/**
 * @brief   Reads "[DEC|OCT|LIN] <np> <fstart> <fstop>" from the tokens of a directive.
 * @details The point count is a whole number of at least 1 and the range needs 0 < fstart <= fstop, for a linear
 *          sweep too: the engine solves at each frequency as it is asked, and a zero start has no meaning for the
 *          noise of a part that falls with frequency. ngspice accepts a start of 0 in a linear sweep; this reader
 *          does not.
 * @param[in] tokens            Tokens of the directive.
 * @param[in] first_token       Index of the sweep type, or of the point count when the type is optional and absent.
 * @param[in] is_type_required  True when the type word must be there, as in .sens ac; otherwise a missing one is a
 *                              sweep by decade.
 * @param[in] directive         Name used in the messages, such as ".ac".
 * @param[in] line              Text of the directive, quoted in the messages.
 * @return  The sweep, or the message that tells what is wrong with it.
 */
std::expected<FrequencySweepWords, std::string> ReadFrequencySweep(const std::vector<std::string> &tokens,
                                                                   const std::size_t first_token,
                                                                   const bool is_type_required,
                                                                   const std::string_view directive,
                                                                   const std::string_view line) {
    FrequencySweepWords sweep;
    std::size_t number_token = first_token;
    if (first_token >= tokens.size()) {
        return std::unexpected(std::format("Malformed {} directive: '{}'", directive, line));
    }
    if (const std::optional<FrequencySweepType> type = FindSweepType(tokens[first_token])) {
        sweep.Type = *type;
        ++number_token;
    } else if (is_type_required) {
        return std::unexpected(std::format("Unknown sweep type '{}' in {}, use dec, oct or lin: '{}'",
                                           tokens[first_token],
                                           directive,
                                           line));
    }
    if (tokens.size() < number_token + SWEEP_NUMBER_COUNT) {
        return std::unexpected(std::format("Malformed {} directive: '{}'", directive, line));
    }
    const std::optional<double> points = ParseSpiceNumber(tokens[number_token]);
    if (!points || *points < 1.0 || std::floor(*points) != *points
        || *points > static_cast<double>(std::numeric_limits<int>::max())) {
        return std::unexpected(
            std::format("Invalid number of points in {}, it must be a whole number of at least 1: '{}'",
                        directive,
                        line));
    }
    sweep.Points = static_cast<int>(*points);
    const std::optional<double> start = ParseSpiceNumber(tokens[number_token + 1]);
    const std::optional<double> stop = ParseSpiceNumber(tokens[number_token + 2]);
    if (!start || !stop || *start <= 0.0 || *stop < *start) {
        return std::unexpected(
            std::format("Invalid frequency range in {}, it needs 0 < fstart <= fstop: '{}'", directive, line));
    }
    sweep.StartFrequency = *start;
    sweep.StopFrequency = *stop;
    return sweep;
}

// Sources the directives name

/**
 * @brief   The names of the independent sources of the circuit lines that have an AC magnitude, which is what drives
 *          an AC sensitivity.
 * @details Sources inside a block are not seen: a block's own sources have no AC to name from outside.
 */
std::vector<std::string> FindAcSources(const std::vector<DeckLine> &circuit_lines) {
    constexpr std::size_t FIRST_AFTER_NODES = 3;
    std::vector<std::string> names;
    for (const DeckLine &line : circuit_lines) {
        const std::vector<std::string> &tokens = line.Tokens;
        const char kind = GetElementLetter(tokens.front());
        if (tokens.size() < FIRST_AFTER_NODES || (kind != 'V' && kind != 'I')) {
            continue;
        }
        for (std::size_t index = FIRST_AFTER_NODES; index + 1 < tokens.size(); ++index) {
            if (ToUpper(tokens[index]) == "AC") {
                if (ParseSpiceNumber(tokens[index + 1]).value_or(1.0) != 0.0) {
                    names.push_back(tokens.front());
                }
                break;
            }
        }
    }
    return names;
}

/**
 * @brief   The input of an AC sensitivity: the ngspice syntax names none, so it is the one source with an AC
 *          magnitude.
 */
std::expected<std::string, std::string> FindAcSensitivityInput(const std::vector<DeckLine> &circuit_lines) {
    const std::vector<std::string> sources = FindAcSources(circuit_lines);
    if (sources.empty()) {
        return std::unexpected("No source has an AC magnitude, so .sens ac has no input; give one source 'AC 1'");
    }
    if (sources.size() > 1) {
        std::string names;
        for (const std::string &name : sources) {
            names += (names.empty() ? "" : ", ") + name;
        }
        return std::unexpected(std::format("More than one source has an AC magnitude ({}), so .sens ac cannot tell "
                                           "its input; leave AC on one source only",
                                           names));
    }
    return sources.front();
}

/**
 * @brief   Finds the independent source between two nodes, in either direction, for the input of a .pz directive.
 * @return  Its name: a source of the netlist lines first, then one inside a subcircuit instance; none when no
 *          source joins the two nodes.
 */
std::optional<std::string>
FindSourceBetween(const DeckContext &context, const std::string &positive, const std::string &negative) {
    for (const DeckLine &line : context.Lines.CircuitLines) {
        const std::vector<std::string> &tokens = line.Tokens;
        const char kind = static_cast<char>(std::toupper(static_cast<unsigned char>(tokens.front().front())));
        const bool is_source = tokens.size() >= 3 && (kind == 'V' || kind == 'I');
        if (is_source
            && ((tokens[1] == positive && tokens[2] == negative) || (tokens[1] == negative && tokens[2] == positive))) {
            return tokens.front();
        }
    }
    const NodeId positive_node = context.Nodes.Resolve(positive);
    const NodeId negative_node = context.Nodes.Resolve(negative);
    for (const BlockSource &source : context.BlockSources) {
        if ((source.Positive == positive_node && source.Negative == negative_node)
            || (source.Positive == negative_node && source.Negative == positive_node)) {
            return source.Name;
        }
    }
    return std::nullopt;
}

/**
 * @brief   Reads what a .pz directive looks for: POL for the poles alone, ZER for the zeros alone, anything else both.
 */
void ReadPoleZeroKind(const std::string_view word, PoleZeroConfig &config) {
    const std::string upper = ToUpper(word);
    if (upper == "POL") {
        config.FindZeros = false;
    } else if (upper == "ZER") {
        config.FindPoles = false;
    }
}

// Options

/**
 * @struct  NumericOption
 * @brief   An option of .options whose number goes into the convergence options.
 */
struct NumericOption {
    std::string_view Name;
    double ConvergenceOptions::*Target;
};

constexpr auto NUMERIC_OPTIONS = std::to_array<NumericOption>({
    {"RELTOL", &ConvergenceOptions::RelTol},
    {"VNTOL", &ConvergenceOptions::VoltageAbsTol},
    {"ABSTOL", &ConvergenceOptions::CurrentAbsTol},
    {"GMIN", &ConvergenceOptions::Gmin},
});

std::optional<IntegrationMethod> FindIntegrationMethod(const std::string_view name) {
    if (name == "GEAR") {
        return IntegrationMethod::GEAR2;
    }
    if (name == "TRAP" || name == "TRAPEZOIDAL") {
        return IntegrationMethod::TRAPEZOIDAL;
    }
    return std::nullopt;
}

/**
 * @brief   Applies one NAME=VALUE option to the convergence options or to the settings applied at the end.
 * @details An option the engine does not know, or whose value is not a number, is left alone, as ngspice leaves the
 *          options it does not have.
 */
void ApplyOption(DeckContext &context, const std::string &name, const double value) {
    if (const auto *found = std::ranges::find(NUMERIC_OPTIONS, name, &NumericOption::Name);
        found != NUMERIC_OPTIONS.end()) {
        context.Result.Options.*(found->Target) = value;
    } else if (name == "MAXITER" || name == "ITL1") {
        context.Result.Options.MaxIterations = static_cast<int>(value);
    } else if (name == "TEMP") {
        context.Settings.TemperatureCelsius = value;
    } else if (name == "TNOM") {
        context.Settings.NominalCelsius = value;
    } else if (name == "SEED") {
        context.Settings.Seed = static_cast<unsigned int>(std::max(0.0, value));
    }
}

// One function per directive

std::expected<void, std::string> ReadOperatingPoint(DeckContext &context, const DeckLine & /*line*/) {
    context.Result.Analyses.push_back({AnalysisKind::OPERATING_POINT, std::monostate{}});
    return {};
}

/**
 * @brief   .tran <tstep> <tstop> [<tstart> [<tmax>]] [uic]; the start time is not used.
 */
std::expected<void, std::string> ReadTransient(DeckContext &context, const DeckLine &line) {
    const std::vector<std::string> &tokens = line.Tokens;
    if (tokens.size() < 3) {
        return std::unexpected(std::format("Malformed .tran directive: '{}'", line.Text));
    }
    TransientConfig config;
    config.TimeStep = ParseSpiceNumber(tokens[1]).value_or(0.0);
    config.StopTime = ParseSpiceNumber(tokens[2]).value_or(0.0);
    if (tokens.size() >= 5 && !ToUpper(tokens[3]).contains("UIC")) {
        config.MaxStep = ParseSpiceNumber(tokens[4]).value_or(config.TimeStep);
    }
    config.UseInitialConditions =
        std::ranges::any_of(tokens, [](const std::string &token) { return ToUpper(token) == "UIC"; });
    if (config.MaxStep <= 0.0) {
        config.MaxStep = config.TimeStep;
    }
    context.Result.Analyses.push_back({AnalysisKind::TRANSIENT, config});
    return {};
}

/**
 * @brief   .dc <source> <start> <stop> <step> [<source2> <start2> <stop2> <step2>].
 */
std::expected<void, std::string> ReadDcSweep(DeckContext &context, const DeckLine &line) {
    constexpr std::size_t TOKENS_WITH_STEPPED_SOURCE = 9;
    const std::vector<std::string> &tokens = line.Tokens;
    if (tokens.size() < 5) {
        return std::unexpected(std::format("Malformed .dc directive: '{}'", line.Text));
    }
    DcSweepConfig config;
    config.SweptSource = tokens[1];
    config.SweptStart = ParseSpiceNumber(tokens[2]).value_or(0.0);
    config.SweptStop = ParseSpiceNumber(tokens[3]).value_or(0.0);
    config.SweptStep = ParseSpiceNumber(tokens[4]).value_or(0.0);
    if (tokens.size() >= TOKENS_WITH_STEPPED_SOURCE) {
        config.SteppedSource = tokens[5];
        config.SteppedStart = ParseSpiceNumber(tokens[6]).value_or(0.0);
        config.SteppedStop = ParseSpiceNumber(tokens[7]).value_or(0.0);
        config.SteppedStep = ParseSpiceNumber(tokens[8]).value_or(0.0);
    }
    context.Result.Analyses.push_back({AnalysisKind::DC_SWEEP, config});
    return {};
}

/**
 * @brief   .ac [DEC|OCT|LIN] <np> <fstart> <fstop>.
 */
std::expected<void, std::string> ReadAcSweep(DeckContext &context, const DeckLine &line) {
    if (line.Tokens.size() < 4) {
        return std::unexpected(std::format("Malformed .ac directive: '{}'", line.Text));
    }
    const auto sweep = ReadFrequencySweep(line.Tokens, 1, false, ".ac", line.Text);
    if (!sweep) {
        return std::unexpected(sweep.error());
    }
    AcSweepConfig config;
    config.SweepType = sweep->Type;
    config.PointsPerDecade = sweep->Points;
    config.StartFrequency = sweep->StartFrequency;
    config.StopFrequency = sweep->StopFrequency;
    context.Result.Analyses.push_back({AnalysisKind::AC_SWEEP, config});
    return {};
}

/**
 * @brief   .tf <out_var> <in_src>.
 */
std::expected<void, std::string> ReadTransferFunction(DeckContext &context, const DeckLine &line) {
    if (line.Tokens.size() < 3) {
        return std::unexpected(std::format("Malformed .tf directive: '{}'", line.Text));
    }
    const OutputNodes output = ReadOutputNodes(line.Tokens[1]);
    TransferFunctionConfig config;
    config.OutputPositiveNode = context.Nodes.Resolve(output.Positive);
    config.OutputNegativeNode = context.Nodes.Resolve(output.Negative);
    config.InputSource = line.Tokens[2];
    context.Result.Analyses.push_back({AnalysisKind::TRANSFER_FUNCTION, config});
    return {};
}

/**
 * @brief   .noise <out_var> <in_src> [DEC|OCT|LIN] <np> <fstart> <fstop>.
 */
std::expected<void, std::string> ReadNoise(DeckContext &context, const DeckLine &line) {
    if (line.Tokens.size() < 6) {
        return std::unexpected(std::format("Malformed .noise directive: '{}'", line.Text));
    }
    const OutputNodes output = ReadOutputNodes(line.Tokens[1]);
    NoiseConfig config;
    config.OutputPositiveNode = context.Nodes.Resolve(output.Positive);
    config.OutputNegativeNode = context.Nodes.Resolve(output.Negative);
    config.InputSource = line.Tokens[2];
    const auto sweep = ReadFrequencySweep(line.Tokens, 3, false, ".noise", line.Text);
    if (!sweep) {
        return std::unexpected(sweep.error());
    }
    config.SweepType = sweep->Type;
    config.PointsPerDecade = sweep->Points;
    config.StartFrequency = sweep->StartFrequency;
    config.StopFrequency = sweep->StopFrequency;
    context.Result.Analyses.push_back({AnalysisKind::NOISE, config});
    return {};
}

/**
 * @brief   .sp [DEC|OCT|LIN] <np> <fstart> <fstop>; a number that cannot be read takes the default of the analysis.
 */
std::expected<void, std::string> ReadSParameters(DeckContext &context, const DeckLine &line) {
    constexpr double DEFAULT_POINTS = 10.0;
    constexpr double DEFAULT_START = 1e6;
    constexpr double DEFAULT_STOP = 1e9;
    const std::vector<std::string> &tokens = line.Tokens;
    if (tokens.size() < 4) {
        return std::unexpected(std::format("Malformed .sp directive: '{}'", line.Text));
    }
    SParameterConfig config;
    std::size_t first_number = 1;
    if (const std::optional<FrequencySweepType> type = FindSweepType(tokens[1])) {
        config.SweepType = *type;
        first_number = 2;
    }
    config.Points = static_cast<int>(ParseSpiceNumber(tokens[first_number]).value_or(DEFAULT_POINTS));
    config.StartFrequency = ParseSpiceNumber(tokens[first_number + 1]).value_or(DEFAULT_START);
    config.StopFrequency = ParseSpiceNumber(tokens[first_number + 2]).value_or(DEFAULT_STOP);
    context.Result.Analyses.push_back({AnalysisKind::S_PARAMETERS, config});
    return {};
}

/**
 * @brief   .pz <out_var> <in_src> [POL|ZER|PZ], or .pz <in_pos> <in_neg> <out_pos> <out_neg> [VOL|CUR] [POL|ZER|PZ],
 *          whose input is the source between the two input nodes.
 */
std::expected<void, std::string> ReadPoleZero(DeckContext &context, const DeckLine &line) {
    constexpr std::size_t TOKENS_WITH_NODES = 6;
    constexpr std::size_t KIND_AFTER_NODES = 6;
    constexpr std::size_t KIND_AFTER_SOURCE = 3;
    const std::vector<std::string> &tokens = line.Tokens;
    if (tokens.size() < 3) {
        return std::unexpected(std::format("Malformed .pz directive: '{}'", line.Text));
    }
    PoleZeroConfig config;
    std::size_t kind_token = KIND_AFTER_SOURCE;
    if (tokens.size() >= TOKENS_WITH_NODES) {
        config.OutputNode = context.Nodes.Resolve(tokens[3]);
        const std::optional<std::string> source = FindSourceBetween(context, tokens[1], tokens[2]);
        if (!source) {
            return std::unexpected(
                std::format("Could not find source between nodes '{}' and '{}' for .pz", tokens[1], tokens[2]));
        }
        config.InputSource = *source;
        kind_token = KIND_AFTER_NODES;
    } else {
        config.OutputNode = context.Nodes.Resolve(ReadOutputNodes(tokens[1]).Positive);
        config.InputSource = tokens[2];
    }
    if (tokens.size() > kind_token) {
        ReadPoleZeroKind(tokens[kind_token], config);
    }
    context.Result.Analyses.push_back({AnalysisKind::POLE_ZERO, config});
    return {};
}

/**
 * @brief   Reads the rest of ".sens <out_var> AC [DEC|OCT|LIN] <np> <fstart> <fstop>" after the output nodes, and
 *          picks the input source.
 */
std::expected<AcSensitivityConfig, std::string>
ReadAcSensitivity(const DeckContext &context, const DeckLine &line, const NodeId positive, const NodeId negative) {
    constexpr std::size_t TOKENS_WITH_SWEEP_TYPE = 7;
    constexpr std::size_t FIRST_SWEEP_TOKEN = 3;
    if (line.Tokens.size() != TOKENS_WITH_SWEEP_TYPE) {
        return std::unexpected("Malformed .sens ac directive, expected '.sens <out_var> ac dec|oct|lin <np> <fstart> "
                               "<fstop>'");
    }
    const auto sweep = ReadFrequencySweep(line.Tokens, FIRST_SWEEP_TOKEN, true, ".sens ac", line.Text);
    if (!sweep) {
        return std::unexpected(sweep.error());
    }
    if (positive == GROUND_NODE) {
        return std::unexpected("The output of .sens ac must have a node other than ground as its positive one");
    }
    if (positive == negative) {
        return std::unexpected("The two output nodes of .sens ac are the same node");
    }
    const std::expected<std::string, std::string> input = FindAcSensitivityInput(context.Lines.CircuitLines);
    if (!input) {
        return std::unexpected(input.error());
    }
    AcSensitivityConfig config;
    config.SweepType = sweep->Type;
    config.PointsPerDecade = sweep->Points;
    config.StartFrequency = sweep->StartFrequency;
    config.StopFrequency = sweep->StopFrequency;
    config.OutputPositiveNode = positive;
    config.OutputNegativeNode = negative;
    config.InputSource = *input;
    return config;
}

/**
 * @brief   .sens <out_var>, for the DC sensitivity, or .sens <out_var> AC ..., for the AC one.
 */
std::expected<void, std::string> ReadSensitivity(DeckContext &context, const DeckLine &line) {
    const std::vector<std::string> &tokens = line.Tokens;
    if (tokens.size() < 2) {
        return std::unexpected(std::format("Malformed .sens directive: '{}'", line.Text));
    }
    const OutputNodes output = ReadOutputNodes(tokens[1]);
    const NodeId positive = context.Nodes.Resolve(output.Positive);
    const NodeId negative = context.Nodes.Resolve(output.Negative);
    if (tokens.size() < 3 || ToUpper(tokens[2]) != "AC") {
        SensitivityConfig config;
        config.OutputPositiveNode = positive;
        config.OutputNegativeNode = negative;
        context.Result.Analyses.push_back({AnalysisKind::SENSITIVITY, config});
        return {};
    }
    const auto config = ReadAcSensitivity(context, line, positive, negative);
    if (!config) {
        return std::unexpected(std::format("{}: '{}'", config.error(), line.Text));
    }
    context.Result.Analyses.push_back({AnalysisKind::AC_SENSITIVITY, *config});
    return {};
}

/**
 * @brief   .hb <f0> <harmonics> [samples=<M>].
 */
std::expected<void, std::string> ReadHarmonicBalance(DeckContext &context, const DeckLine &line) {
    constexpr std::string_view SAMPLES_KEY = "SAMPLES=";
    constexpr std::size_t FIRST_OPTION = 3;
    const std::vector<std::string> &tokens = line.Tokens;
    if (tokens.size() < 3) {
        return std::unexpected(std::format("Malformed .hb directive: '{}'", line.Text));
    }
    const std::optional<double> fundamental = ParseSpiceNumber(tokens[1]);
    if (!fundamental || *fundamental <= 0.0) {
        return std::unexpected(std::format("Invalid fundamental frequency in .hb directive: '{}'", line.Text));
    }
    const std::optional<double> harmonics = ParseSpiceNumber(tokens[2]);
    if (!harmonics || *harmonics < 0.0 || std::floor(*harmonics) != *harmonics) {
        return std::unexpected(std::format("Invalid harmonic count in .hb directive: '{}'", line.Text));
    }
    HarmonicBalanceConfig config;
    config.FundamentalFrequency = *fundamental;
    config.Harmonics = static_cast<int>(*harmonics);
    for (std::size_t index = FIRST_OPTION; index < tokens.size(); ++index) {
        if (!ToUpper(tokens[index]).starts_with(SAMPLES_KEY)) {
            return std::unexpected(
                std::format("Unexpected token '{}' in .hb directive: '{}'", tokens[index], line.Text));
        }
        const std::optional<double> samples =
            ParseSpiceNumber(std::string_view(tokens[index]).substr(SAMPLES_KEY.size()));
        if (!samples || *samples <= 0.0 || std::floor(*samples) != *samples) {
            return std::unexpected(std::format("Invalid samples parameter in .hb directive: '{}'", line.Text));
        }
        config.Samples = static_cast<int>(*samples);
    }
    context.Result.Analyses.push_back({AnalysisKind::HARMONIC_BALANCE, config});
    return {};
}

/**
 * @brief   .options with NAME=VALUE words: METHOD picks the integration method, the others are numbers.
 */
std::expected<void, std::string> ReadOptions(DeckContext &context, const DeckLine &line) {
    for (std::size_t index = 1; index < line.Tokens.size(); ++index) {
        const std::string_view word = line.Tokens[index];
        const std::size_t equals = word.find('=');
        if (equals == std::string_view::npos) {
            continue;
        }
        const std::string name = ToUpper(word.substr(0, equals));
        const std::string_view value = word.substr(equals + 1);
        if (name == "METHOD") {
            const std::string method_name = ToUpper(value);
            const std::optional<IntegrationMethod> method = FindIntegrationMethod(method_name);
            if (!method) {
                return std::unexpected(
                    std::format("Unknown integration method '{}' in .options: '{}'", method_name, line.Text));
            }
            context.Settings.Method = method;
        } else if (const std::optional<double> number = ParseSpiceNumber(value)) {
            ApplyOption(context, name, *number);
        }
    }
    return {};
}

/**
 * @brief   .temp <celsius>.
 */
std::expected<void, std::string> ReadTemperature(DeckContext &context, const DeckLine &line) {
    if (line.Tokens.size() < 2) {
        return {};
    }
    const std::optional<double> temperature = ParseSpiceNumber(line.Tokens[1]);
    if (!temperature) {
        return std::unexpected(std::format("Invalid temperature '{}' in .temp: '{}'", line.Tokens[1], line.Text));
    }
    context.Settings.TemperatureCelsius = *temperature;
    return {};
}

std::expected<void, std::string> ReadInitialConditions(DeckContext &context, const DeckLine &line) {
    return ReadNodeVoltages(context, line, false);
}

std::expected<void, std::string> ReadNodesets(DeckContext &context, const DeckLine &line) {
    return ReadNodeVoltages(context, line, true);
}

/**
 * @struct  DirectiveReader
 * @brief   The function that reads one directive, by its upper-case name.
 */
struct DirectiveReader {
    std::string_view Name;
    std::expected<void, std::string> (*Read)(DeckContext &, const DeckLine &);
};

constexpr auto DIRECTIVE_READERS = std::to_array<DirectiveReader>({
    {".OP", ReadOperatingPoint},
    {".TRAN", ReadTransient},
    {".DC", ReadDcSweep},
    {".AC", ReadAcSweep},
    {".TF", ReadTransferFunction},
    {".NOISE", ReadNoise},
    {".SP", ReadSParameters},
    {".PZ", ReadPoleZero},
    {".SENS", ReadSensitivity},
    {".HB", ReadHarmonicBalance},
    {".OPTIONS", ReadOptions},
    {".TEMP", ReadTemperature},
    {".IC", ReadInitialConditions},
    {".NODESET", ReadNodesets},
});

} // namespace

/**
 * @brief   The nodes a directive names, which are circuit nodes even when no element touches them.
 * @param[in] line  A directive line.
 * @return  The output nodes of .tf and .noise, and the nodes of .pz; none for the other directives.
 */
std::vector<std::string> ListDirectiveNodes(const DeckLine &line) {
    constexpr std::size_t PZ_TOKENS_WITH_NODES = 5;
    const std::vector<std::string> &tokens = line.Tokens;
    const std::string directive = ToUpper(tokens.front());
    if (directive == ".PZ" && tokens.size() >= PZ_TOKENS_WITH_NODES) {
        return {tokens[1], tokens[2], tokens[3], tokens[4]};
    }
    if ((directive == ".TF" || directive == ".NOISE" || directive == ".PZ") && tokens.size() >= 2) {
        OutputNodes output = ReadOutputNodes(tokens[1]);
        return {std::move(output.Positive), std::move(output.Negative)};
    }
    return {};
}

/**
 * @brief   Reads one directive into the analyses, the options or the settings of the netlist.
 * @param[in,out] context  The netlist being parsed.
 * @param[in]     line     A line whose first token starts with a dot.
 * @return  Nothing once read, also for a directive the engine does not know, which is left alone; the reason a known
 *          directive cannot be read otherwise.
 */
std::expected<void, std::string> ReadDirective(DeckContext &context, const DeckLine &line) {
    const std::string name = ToUpper(line.Tokens.front());
    const auto *reader = std::ranges::find(DIRECTIVE_READERS, name, &DirectiveReader::Name);
    if (reader == DIRECTIVE_READERS.end()) {
        return {};
    }
    return reader->Read(context, line);
}

/**
 * @brief   Applies the settings that .options and .temp gave, once the whole netlist is read.
 * @param[in]     settings  The settings read.
 * @param[in,out] builder   Circuit being built, which takes the temperatures.
 * @param[in,out] analyses  Analyses of the netlist; the transients take the integration method and the seed.
 */
void ApplyDeckSettings(const DeckSettings &settings, CircuitBuilder &builder, std::vector<ParsedAnalysis> &analyses) {
    builder.SetTemperature(settings.TemperatureCelsius, settings.NominalCelsius);
    for (ParsedAnalysis &analysis : analyses) {
        auto *transient = std::get_if<TransientConfig>(&analysis.Config);
        if (transient == nullptr) {
            continue;
        }
        if (settings.Method) {
            transient->Method = *settings.Method;
        }
        if (settings.Seed) {
            transient->Seed = *settings.Seed;
        }
    }
}

} // namespace Nodalys
