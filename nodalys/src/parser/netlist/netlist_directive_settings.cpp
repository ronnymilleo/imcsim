/**
 * @file    netlist_directive_settings.cpp
 * @brief   Shared directive helpers (output nodes, frequency sweeps) and settings directives (.options, .temp, .ic,
 *          .nodeset).
 */

#include "parser/netlist/netlist_directive_settings.h"

#include "parser/syntax/spice_text.h"
#include "text/spice_number.h"
#include "text/text_case.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <format>
#include <limits>

namespace Nodalys {

namespace {

/// The words that follow the sweep type, which are the points, the start and the stop.
constexpr std::size_t SWEEP_NUMBER_COUNT = 3;

/**
 * @struct  Assignment
 * @brief   The two sides of a NAME=VALUE word, as written.
 */
struct Assignment {
    std::string_view Name;
    std::string_view Value;
};

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
    if (const auto found = std::ranges::find(NUMERIC_OPTIONS, name, &NumericOption::Name);
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

} // namespace

/**
 * @brief   Reads the nodes of an output written `V(node)` or `V(positive, negative)`; a bare name is the node itself.
 * @param[in] text  Output expression text.
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

/**
 * @brief   Maps a SPICE frequency sweep keyword (`DEC`, `OCT`, `LIN`) to FrequencySweepType.
 * @param[in] word  Case-insensitive sweep keyword.
 * @return  Matching sweep type, or empty if unrecognized.
 */
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
 * @brief   Reads `[DEC|OCT|LIN] <np> <fstart> <fstop>` from the tokens of a directive.
 * @details The point count is a whole number of at least 1 and the range needs `0 < fstart <= fstop`, for a linear
 *          sweep too: the engine solves at each frequency as it is asked, and a zero start has no meaning for the
 *          noise of a part that falls with frequency. ngspice accepts a start of 0 in a linear sweep; this reader
 *          does not.
 * @param[in] tokens            Tokenized directive line.
 * @param[in] first_token       Index of the first sweep token.
 * @param[in] is_type_required  True if an explicit `DEC`/`OCT`/`LIN` keyword is mandatory.
 * @param[in] directive         Directive name used in error diagnostics.
 * @param[in] line              Original directive text used in error diagnostics.
 * @return  Parsed sweep parameters, or a parse error message.
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

/**
 * @brief   Parses `.options` with `NAME=VALUE` words: `METHOD` picks the integration method, the others are numbers.
 * @param[in,out] context  Parser deck context receiving updated convergence and solver settings.
 * @param[in]     line     Tokenized `.options` directive.
 * @return  Empty expected on success, or a parse error message.
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
 * @brief   Parses `.temp <celsius>`.
 * @param[in,out] context  Parser deck context receiving the simulation temperature.
 * @param[in]     line     Tokenized `.temp` directive.
 * @return  Empty expected on success, or a parse error message.
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

/**
 * @brief   Parses a `.ic` initial-condition node voltage directive.
 * @param[in,out] context  Parser deck context receiving initial voltages.
 * @param[in]     line     Tokenized `.ic` directive.
 * @return  Empty expected on success, or a parse error message.
 */
std::expected<void, std::string> ReadInitialConditions(DeckContext &context, const DeckLine &line) {
    return ReadNodeVoltages(context, line, false);
}

/**
 * @brief   Parses a `.nodeset` DC operating-point hint directive.
 * @param[in,out] context  Parser deck context receiving nodeset hints.
 * @param[in]     line     Tokenized `.nodeset` directive.
 * @return  Empty expected on success, or a parse error message.
 */
std::expected<void, std::string> ReadNodesets(DeckContext &context, const DeckLine &line) {
    return ReadNodeVoltages(context, line, true);
}

} // namespace Nodalys
