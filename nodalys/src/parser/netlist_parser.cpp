/**
 * @file    netlist_parser.cpp
 * @brief   Parses SPICE netlists into compiled circuits and simulation requests.
 */

#include "nodalys/netlist_parser.h"
#include "nodalys/circuit_builder.h"
#include "nodalys/expression.h"
#include "parser/model_cards.h"
#include "parser/netlist_nodes.h"
#include "parser/netlist_subcircuits.h"
#include "parser/source_waveforms.h"
#include "parser/spice_number.h"
#include "parser/spice_text.h"
#include "text/text_case.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <format>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace Nodalys {

namespace {

std::string_view Trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.remove_suffix(1);
    }
    return s;
}

std::vector<std::string> TokenizeLine(std::string_view line) {
    std::vector<std::string> tokens;
    std::size_t i = 0;
    const std::size_t n = line.size();

    while (i < n) {
        // Skip whitespace and commas
        while (i < n && (std::isspace(static_cast<unsigned char>(line[i])) || line[i] == ',')) {
            ++i;
        }
        if (i >= n) {
            break;
        }

        // Inline comments: ; or $
        if (line[i] == ';' || line[i] == '$') {
            break;
        }

        // Parenthesized tokens, e.g. PULSE(...) or SIN(...) or model param list (...)
        if (line[i] == '(') {
            std::size_t start = i;
            int depth = 0;
            while (i < n) {
                if (line[i] == '(') {
                    ++depth;
                } else if (line[i] == ')') {
                    --depth;
                    if (depth == 0) {
                        ++i;
                        break;
                    }
                }
                ++i;
            }
            tokens.emplace_back(line.substr(start, i - start));
            continue;
        }

        std::size_t start = i;
        while (i < n && !std::isspace(static_cast<unsigned char>(line[i])) && line[i] != ',' && line[i] != ';'
               && line[i] != '$') {
            if (line[i] == '(') {
                // e.g. SIN(...) attached to name
                int depth = 0;
                while (i < n) {
                    if (line[i] == '(') {
                        ++depth;
                    } else if (line[i] == ')') {
                        --depth;
                        if (depth == 0) {
                            ++i;
                            break;
                        }
                    }
                    ++i;
                }
                break;
            }
            ++i;
        }
        tokens.emplace_back(line.substr(start, i - start));
    }
    return tokens;
}

struct IcAssignment {
    std::string_view Lhs;
    std::string_view Rhs;
};

std::expected<IcAssignment, std::string>
NextIcAssignment(const std::vector<std::string> &tokens, std::size_t &index, std::string_view line) {
    const std::size_t eq = tokens[index].find('=');
    if (eq != std::string::npos) {
        const std::string_view lhs = std::string_view(tokens[index]).substr(0, eq);
        std::string_view rhs = std::string_view(tokens[index]).substr(eq + 1);
        if (rhs.empty()) {
            if (index + 1 >= tokens.size()) {
                return std::unexpected(std::format("Missing voltage after '=' in .ic: '{}'", line));
            }
            rhs = tokens[++index];
        }
        return IcAssignment{.Lhs = lhs, .Rhs = rhs};
    }
    if (index + 1 < tokens.size() && tokens[index + 1].starts_with('=')) {
        const std::string_view lhs = tokens[index];
        if (tokens[index + 1].size() > 1) {
            return IcAssignment{.Lhs = lhs, .Rhs = std::string_view(tokens[++index]).substr(1)};
        }
        if (index + 2 >= tokens.size()) {
            return std::unexpected(std::format("Missing voltage after '=' in .ic: '{}'", line));
        }
        const std::string_view rhs = tokens[index + 2];
        index += 2;
        return IcAssignment{.Lhs = lhs, .Rhs = rhs};
    }
    return std::unexpected(std::format("Malformed entry '{}' in .ic: '{}'", tokens[index], line));
}

/// Parses a `.ic` or `.nodeset` directive such as `.ic v(1)=5 v(out) = 0` and records each node voltage, as the
/// initial voltage of the node or as its nodeset.
std::expected<void, std::string> ParseNodeVoltageDirective(const std::vector<std::string> &tokens,
                                                           std::string_view line,
                                                           const NetlistNodes &nodes,
                                                           CircuitBuilder &builder,
                                                           const bool is_nodeset) {
    for (std::size_t i = 1; i < tokens.size(); ++i) {
        const auto assignment = NextIcAssignment(tokens, i, line);
        if (!assignment) {
            return std::unexpected(assignment.error());
        }
        const std::string_view lhs = assignment->Lhs;
        if (lhs.size() <= 3 || (lhs[0] != 'v' && lhs[0] != 'V') || lhs[1] != '(' || lhs.back() != ')') {
            return std::unexpected(std::format("Expected v(node)=value in .ic, got '{}': '{}'", lhs, line));
        }
        const std::string_view node_name = lhs.substr(2, lhs.size() - 3);
        const auto voltage = ParseSpiceNumber(assignment->Rhs);
        if (!voltage) {
            return std::unexpected(std::format("Invalid voltage '{}' in .ic: '{}'", assignment->Rhs, line));
        }
        if (is_nodeset) {
            builder.SetNodeset(nodes.Resolve(node_name), *voltage);
        } else {
            builder.SetInitialVoltage(nodes.Resolve(node_name), *voltage);
        }
    }
    return {};
}

// Helper to extract numbers inside parentheses: "PULSE(0 5 1u 1u ...)" -> vector of numbers
std::vector<double> ExtractParenthesizedValues(std::string_view token) {
    const std::size_t open = token.find('(');
    const std::size_t close = token.rfind(')');
    std::vector<double> vals;
    if (open == std::string::npos || close == std::string::npos || close <= open) {
        return vals;
    }
    const std::string inner(token.substr(open + 1, close - open - 1));
    const auto toks = TokenizeLine(inner);
    for (const auto &t : toks) {
        if (const auto num = ParseSpiceNumber(t)) {
            vals.push_back(*num);
        }
    }
    return vals;
}

// Parses V(node) or V(pos, neg)
std::pair<std::string, std::string> ParseVoltageNodes(std::string_view s) {
    s = Trim(s);
    const std::string upper = ToUpper(s);
    if (upper.starts_with("V(") && upper.ends_with(")")) {
        const std::string_view inner = s.substr(2, s.size() - 3);
        const auto comma = inner.find(',');
        if (comma != std::string_view::npos) {
            return {std::string(Trim(inner.substr(0, comma))), std::string(Trim(inner.substr(comma + 1)))};
        }
        return {std::string(Trim(inner)), "0"};
    }
    return {std::string(s), "0"};
}

/// The names of the independent sources of the circuit lines that have an AC magnitude, which is what drives an AC
/// sensitivity. Sources inside a block are not seen: a block's own sources have no AC to name from outside.
std::vector<std::string> FindAcSources(const std::vector<std::string> &circuit_lines) {
    std::vector<std::string> names;
    for (const std::string &line : circuit_lines) {
        const std::vector<std::string> tokens = TokenizeLine(line);
        if (tokens.size() < 3) {
            continue;
        }
        const char kind = static_cast<char>(std::toupper(static_cast<unsigned char>(tokens[0].front())));
        if (kind != 'V' && kind != 'I') {
            continue;
        }
        for (std::size_t index = 3; index + 1 < tokens.size(); ++index) {
            if (ToUpper(tokens[index]) == "AC") {
                if (ParseSpiceNumber(tokens[index + 1]).value_or(1.0) != 0.0) {
                    names.push_back(tokens[0]);
                }
                break;
            }
        }
    }
    return names;
}

/// The sweep of an `.ac`, `.noise` or `.sens ... ac` directive as it is written.
struct FrequencySweepWords {
    FrequencySweepType Type = FrequencySweepType::DECADE;
    int Points = 0;
    double StartFrequency = 0.0;
    double StopFrequency = 0.0;
};

/// Where the sweep of `.sens <out_var> AC <type> <np> <fstart> <fstop>` starts among the tokens.
constexpr std::size_t FIRST_AC_SENSITIVITY_SWEEP_TOKEN = 3;

/// The words that follow the sweep type, which are the points, the start and the stop.
constexpr std::size_t SWEEP_NUMBER_COUNT = 3;

std::optional<FrequencySweepType> FindSweepType(const std::string &word) {
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
 * @details The point count is a whole number of at least 1 and the range needs 0 < fstart <= fstop, for a linear
 *          sweep too: the engine solves at each frequency as it is asked, and a zero start has no meaning for the
 *          noise of a part that falls with frequency. ngspice accepts a start of 0 in a linear sweep; this reader
 *          does not.
 * @param[in] tokens             Tokens of the directive.
 * @param[in] first_token        Index of the sweep type, or of the point count when the type is optional and absent.
 * @param[in] is_type_required   True when the type word must be there, as in `.sens ac`; otherwise a missing one is a
 *                               sweep by decade.
 * @param[in] directive          Name used in the messages, such as ".ac".
 * @param[in] line               Text of the directive, quoted in the messages.
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
    const std::optional<FrequencySweepType> type = FindSweepType(tokens[first_token]);
    if (type) {
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

/// The input of an AC sensitivity: the ngspice syntax names none, so it is the one source with an AC magnitude.
std::expected<std::string, std::string> FindAcSensitivityInput(const std::vector<std::string> &circuit_lines) {
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

/// Reads the rest of `.sens <out_var> AC [DEC|OCT|LIN] <np> <fstart> <fstop>` after the output nodes, and picks the
/// input source.
std::expected<AcSensitivityConfig, std::string> ParseAcSensitivity(const std::vector<std::string> &tokens,
                                                                   const NodeId positive,
                                                                   const NodeId negative,
                                                                   const std::vector<std::string> &circuit_lines,
                                                                   const std::string_view line) {
    constexpr std::size_t TOKENS_WITH_SWEEP_TYPE = 7;
    if (tokens.size() != TOKENS_WITH_SWEEP_TYPE) {
        return std::unexpected("Malformed .sens ac directive, expected '.sens <out_var> ac dec|oct|lin <np> <fstart> "
                               "<fstop>'");
    }
    const std::expected<FrequencySweepWords, std::string> sweep =
        ReadFrequencySweep(tokens, FIRST_AC_SENSITIVITY_SWEEP_TOKEN, true, ".sens ac", line);
    if (!sweep) {
        return std::unexpected(sweep.error());
    }
    AcSensitivityConfig cfg;
    cfg.SweepType = sweep->Type;
    cfg.PointsPerDecade = sweep->Points;
    cfg.StartFrequency = sweep->StartFrequency;
    cfg.StopFrequency = sweep->StopFrequency;
    if (positive == GROUND_NODE) {
        return std::unexpected("The output of .sens ac must have a node other than ground as its positive one");
    }
    if (positive == negative) {
        return std::unexpected("The two output nodes of .sens ac are the same node");
    }
    cfg.OutputPositiveNode = positive;
    cfg.OutputNegativeNode = negative;
    const std::expected<std::string, std::string> input = FindAcSensitivityInput(circuit_lines);
    if (!input) {
        return std::unexpected(input.error());
    }
    cfg.InputSource = *input;
    return cfg;
}

/// The kind and the text of the expression of a B element line, `B1 n+ n- V=<expression>`; the text is taken from
/// the line itself, since the tokens break an expression at its spaces and parentheses.
struct BehavioralLine {
    bool IsVoltage = true;
    std::string Expression;
};

std::optional<BehavioralLine> SplitBehavioralLine(std::string_view line) {
    constexpr int WORDS_BEFORE_EXPRESSION = 3;
    std::size_t position = 0;
    for (int word = 0; word < WORDS_BEFORE_EXPRESSION; ++word) {
        while (position < line.size() && std::isspace(static_cast<unsigned char>(line[position]))) {
            ++position;
        }
        while (position < line.size() && !std::isspace(static_cast<unsigned char>(line[position]))) {
            ++position;
        }
    }
    std::string_view rest = line.substr(position);
    if (const std::size_t comment = rest.find_first_of(";$"); comment != std::string_view::npos) {
        rest = rest.substr(0, comment);
    }
    rest = Trim(rest);
    if (rest.size() < 2
        || (std::toupper(static_cast<unsigned char>(rest.front())) != 'V'
            && std::toupper(static_cast<unsigned char>(rest.front())) != 'I')) {
        return std::nullopt;
    }
    const bool is_voltage = std::toupper(static_cast<unsigned char>(rest.front())) == 'V';
    rest = Trim(rest.substr(1));
    if (rest.empty() || rest.front() != '=') {
        return std::nullopt;
    }
    rest = Trim(rest.substr(1));
    if (rest.empty()) {
        return std::nullopt;
    }
    return BehavioralLine{.IsVoltage = is_voltage, .Expression = std::string(rest)};
}

} // namespace

std::expected<ParsedNetlist, std::string> ParseNetlist(std::string_view netlist_text) {
    // 1. Split into raw lines and handle continuation lines ('+')
    std::vector<std::string> logical_lines;
    {
        std::istringstream stream{std::string(netlist_text)};
        std::string raw_line;
        while (std::getline(stream, raw_line)) {
            // Trim CR
            if (!raw_line.empty() && raw_line.back() == '\r') {
                raw_line.pop_back();
            }
            std::string_view trimmed = Trim(raw_line);
            if (trimmed.empty()) {
                continue;
            }
            if (trimmed.front() == '+') {
                trimmed.remove_prefix(1);
                if (!logical_lines.empty()) {
                    logical_lines.back().push_back(' ');
                    logical_lines.back().append(trimmed);
                } else {
                    logical_lines.emplace_back(trimmed);
                }
            } else {
                logical_lines.emplace_back(trimmed);
            }
        }
    }

    if (logical_lines.empty()) {
        return std::unexpected("The netlist is empty");
    }

    ParsedNetlist result;
    // .options may follow the .tran it applies to, so the method is applied once the whole deck is read
    std::optional<IntegrationMethod> integration_method;
    double temperature_celsius = 27.0;
    double nominal_celsius = 27.0;
    std::optional<unsigned int> random_seed;
    // SPICE standard: first line is circuit title
    result.Title = logical_lines.front();

    // 2. First pass: Collect .model definitions and the lines the subcircuits are placed from
    std::map<std::string, ParsedModel> models;
    std::vector<std::string> circuit_lines;
    // The .subckt blocks, .model cards and .param lines, which every X line is placed over
    std::vector<std::string> definition_lines;

    int subcircuit_depth = 0;
    std::string open_subcircuit_name;

    for (std::size_t line_idx = 1; line_idx < logical_lines.size(); ++line_idx) {
        const std::string &line = logical_lines[line_idx];
        std::string_view trimmed = Trim(line);
        if (trimmed.empty() || trimmed.front() == '*' || trimmed.front() == ';') {
            continue; // comment
        }

        const auto tokens = TokenizeLine(line);
        if (tokens.empty()) {
            continue;
        }

        const std::string directive = ToUpper(tokens[0]);

        if (directive == ".SUBCKT") {
            if (tokens.size() < 2) {
                return std::unexpected(std::format("Malformed .subckt directive on line {}", line_idx + 1));
            }
            if (subcircuit_depth == 0) {
                open_subcircuit_name = tokens[1];
            }
            ++subcircuit_depth;
            definition_lines.push_back(line);
            continue;
        }

        if (directive == ".ENDS") {
            if (subcircuit_depth > 0) {
                --subcircuit_depth;
                definition_lines.push_back(line);
            }
            continue;
        }

        if (subcircuit_depth > 0) {
            definition_lines.push_back(line);
            continue;
        }

        if (directive == ".PARAM") {
            definition_lines.push_back(line);
            continue;
        }

        if (directive == ".MODEL") {
            if (tokens.size() < 3) {
                return std::unexpected(std::format("Malformed .model directive on line {}", line_idx + 1));
            }
            const std::string model_name = ToUpper(tokens[1]);
            std::string type_str = ToUpper(tokens[2]);
            // Type might have parameters directly attached: "D(IS=1e-15...)"
            std::string params_str;
            const auto paren_pos = type_str.find('(');
            if (paren_pos != std::string::npos) {
                params_str = type_str.substr(paren_pos);
                type_str = type_str.substr(0, paren_pos);
            }

            ParsedModel model;
            if (type_str == "D") {
                model.Kind = ParsedModel::ModelKind::DIODE;
            } else if (type_str == "NPN") {
                model.Kind = ParsedModel::ModelKind::BJT_NPN;
            } else if (type_str == "PNP") {
                model.Kind = ParsedModel::ModelKind::BJT_PNP;
            } else if (type_str == "NMOS") {
                model.Kind = ParsedModel::ModelKind::MOSFET_NMOS;
            } else if (type_str == "PMOS") {
                model.Kind = ParsedModel::ModelKind::MOSFET_PMOS;
            } else if (type_str == "SW" || type_str == "VSWITCH") {
                model.Kind = ParsedModel::ModelKind::SWITCH;
            } else {
                result.Diagnostics.push_back(
                    {std::format("Unsupported .model type '{}' in model '{}'", type_str, model_name)});
            }

            // Collect all remaining tokens as parameters
            std::string all_params = params_str;
            for (std::size_t p = 3; p < tokens.size(); ++p) {
                all_params.push_back(' ');
                all_params.append(tokens[p]);
            }

            // Parse parameter key=value pairs
            std::size_t pos = 0;
            const std::size_t plen = all_params.size();
            while (pos < plen) {
                while (pos < plen
                       && (std::isspace(static_cast<unsigned char>(all_params[pos])) || all_params[pos] == '('
                           || all_params[pos] == ')' || all_params[pos] == ',')) {
                    ++pos;
                }
                if (pos >= plen) {
                    break;
                }
                const std::size_t kstart = pos;
                while (pos < plen
                       && (std::isalnum(static_cast<unsigned char>(all_params[pos])) || all_params[pos] == '_')) {
                    ++pos;
                }
                const std::string key = ToUpper(all_params.substr(kstart, pos - kstart));
                while (pos < plen && std::isspace(static_cast<unsigned char>(all_params[pos]))) {
                    ++pos;
                }
                if (pos < plen && all_params[pos] == '=') {
                    ++pos;
                    while (pos < plen && std::isspace(static_cast<unsigned char>(all_params[pos]))) {
                        ++pos;
                    }
                    const std::size_t vstart = pos;
                    while (pos < plen && !std::isspace(static_cast<unsigned char>(all_params[pos]))
                           && all_params[pos] != ')' && all_params[pos] != ',') {
                        ++pos;
                    }
                    const std::string val_str = all_params.substr(vstart, pos - vstart);
                    if (const auto num = ParseSpiceNumber(val_str)) {
                        model.Params[key] = *num;
                    }
                }
            }

            models[model_name] = std::move(model);
            definition_lines.push_back(line);
            continue;
        }

        if (directive == ".END") {
            break;
        }

        circuit_lines.push_back(line);
    }

    if (subcircuit_depth > 0) {
        return std::unexpected(std::format("The subcircuit '{}' has no .ends", open_subcircuit_name));
    }
    // 4. Parse components and analyses using CircuitBuilder
    CircuitBuilder builder;
    NetlistNodes nodes;

    // Pre-scan all circuit_lines to discover and register all circuit nodes
    for (const auto &line : circuit_lines) {
        const auto tokens = TokenizeLine(line);
        if (tokens.empty()) {
            continue;
        }

        const std::string &name = tokens[0];
        if (name.front() == '.') {
            const std::string dir = ToUpper(name);
            if ((dir == ".TF" || dir == ".NOISE") && tokens.size() >= 2) {
                const auto [pos_str, neg_str] = ParseVoltageNodes(tokens[1]);
                nodes.Register(pos_str);
                nodes.Register(neg_str);
            } else if (dir == ".PZ") {
                if (tokens.size() >= 5) {
                    nodes.Register(tokens[1]);
                    nodes.Register(tokens[2]);
                    nodes.Register(tokens[3]);
                    nodes.Register(tokens[4]);
                } else if (tokens.size() >= 2) {
                    const auto [pos_str, neg_str] = ParseVoltageNodes(tokens[1]);
                    nodes.Register(pos_str);
                    nodes.Register(neg_str);
                }
            }
            continue;
        }

        const auto dot_pos = name.rfind('.');
        const std::string_view base_name =
            (dot_pos != std::string::npos) ? std::string_view(name).substr(dot_pos + 1) : std::string_view(name);
        if (base_name.empty()) {
            continue;
        }
        const char first_char = static_cast<char>(std::toupper(static_cast<unsigned char>(base_name.front())));

        switch (first_char) {
        case 'R':
        case 'C':
        case 'L':
        case 'V':
        case 'I':
        case 'D':
        case 'P':
            if (tokens.size() >= 3) {
                nodes.Register(tokens[1]);
                nodes.Register(tokens[2]);
            }
            break;

        case 'Q':
            if (tokens.size() >= 4) {
                nodes.Register(tokens[1]);
                nodes.Register(tokens[2]);
                nodes.Register(tokens[3]);
            }
            break;

        case 'M':
        case 'S':
        case 'E':
        case 'G':
            if (tokens.size() >= 5) {
                nodes.Register(tokens[1]);
                nodes.Register(tokens[2]);
                nodes.Register(tokens[3]);
                nodes.Register(tokens[4]);
            }
            break;

        case 'H':
        case 'F':
            if (tokens.size() >= 3) {
                nodes.Register(tokens[1]);
                nodes.Register(tokens[2]);
            }
            break;

        case 'B': {
            if (tokens.size() >= 3) {
                nodes.Register(tokens[1]);
                nodes.Register(tokens[2]);
            }
            // The nodes the expression reads are circuit nodes too, even when no other element touches them
            if (const std::optional<BehavioralLine> behavioral = SplitBehavioralLine(line)) {
                if (const auto expression = Expression::Parse(behavioral->Expression)) {
                    for (const ExpressionInput &input : expression->GetInputs()) {
                        if (input.Type == ExpressionInput::Kind::VOLTAGE) {
                            nodes.Register(input.First);
                            nodes.Register(input.Second);
                        }
                    }
                }
            }
            break;
        }

        case 'X':
            if (const std::optional<InstanceLine> instance = SplitInstanceLine(SplitTokens(line))) {
                for (const std::string &node_name : instance->NodeNames) {
                    nodes.Register(node_name);
                }
            }
            break;

        case 'U': {
            for (std::size_t i = 1; i < tokens.size(); ++i) {
                if (tokens[i].find('=') != std::string::npos) {
                    break;
                }
                const std::string upper = ToUpper(tokens[i]);
                if (upper == "AND" || upper == "NAND" || upper == "OR" || upper == "NOR" || upper == "XOR"
                    || upper == "XNOR" || upper == "NOT" || upper == "INV" || upper == "BUF" || upper == "BUFFER"
                    || upper == "SCHMITT") {
                    break;
                }
                nodes.Register(tokens[i]);
            }
            break;
        }

        default:
            break;
        }
    }

    builder.SetCircuitNodeCount(nodes.AssignIds());

    // Subcircuits come first, since their internal nodes (X1.MID) are named by the directives that follow, wherever
    // those stand in the netlist
    struct BlockSource {
        std::string Name;
        NodeId Positive;
        NodeId Negative;
    };

    std::vector<BlockSource> block_sources;
    const InstanceObservers observers{
        .OnInternalNode = [&nodes](const std::string &name, const NodeId node) { nodes.Bind(name, node); },
        .OnSource = [&block_sources](const std::string &name,
                                     const NodeId positive,
                                     const NodeId negative) { block_sources.push_back({name, positive, negative}); }};
    for (const auto &line : circuit_lines) {
        const auto tokens = TokenizeLine(line);
        if (tokens.empty() || std::toupper(static_cast<unsigned char>(tokens[0].front())) != 'X') {
            continue;
        }
        const auto added = AddNetlistInstance(
            builder,
            definition_lines,
            line,
            [&nodes](const std::string_view name) { return nodes.Resolve(name); },
            observers);
        if (!added) {
            return std::unexpected(added.error());
        }
        for (const std::string &warning : *added) {
            const bool is_reported =
                std::ranges::any_of(result.Diagnostics,
                                    [&warning](const DiagnosticMessage &message) { return message.Text == warning; });
            if (!is_reported) {
                result.Diagnostics.push_back({warning});
            }
        }
    }

    for (const auto &line : circuit_lines) {
        const auto tokens = TokenizeLine(line);
        if (tokens.empty()) {
            continue;
        }

        const std::string name = tokens[0];

        // Directives
        if (name.front() == '.') {
            const std::string dir = ToUpper(name);
            if (dir == ".OP") {
                result.Analyses.push_back({AnalysisKind::OPERATING_POINT, std::monostate{}});
            } else if (dir == ".TRAN") {
                // .TRAN <tstep> <tstop> [<tstart> [<tmax>]] [uic]
                if (tokens.size() < 3) {
                    return std::unexpected(std::format("Malformed .tran directive: '{}'", line));
                }
                TransientConfig cfg;
                cfg.TimeStep = ParseSpiceNumber(tokens[1]).value_or(0.0);
                cfg.StopTime = ParseSpiceNumber(tokens[2]).value_or(0.0);
                if (tokens.size() >= 4 && !ToUpper(tokens[3]).contains("UIC")) {
                    // tstart ignored or tmax
                    if (tokens.size() >= 5) {
                        cfg.MaxStep = ParseSpiceNumber(tokens[4]).value_or(cfg.TimeStep);
                    }
                }
                for (const auto &tok : tokens) {
                    if (ToUpper(tok) == "UIC") {
                        cfg.UseInitialConditions = true;
                    }
                }
                if (cfg.MaxStep <= 0.0) {
                    cfg.MaxStep = cfg.TimeStep;
                }
                result.Analyses.push_back({AnalysisKind::TRANSIENT, cfg});
            } else if (dir == ".DC") {
                // .DC <src> <start> <stop> <step> [<src2> <start2> <stop2> <step2>]
                if (tokens.size() < 5) {
                    return std::unexpected(std::format("Malformed .dc directive: '{}'", line));
                }
                DcSweepConfig cfg;
                cfg.SweptSource = tokens[1];
                cfg.SweptStart = ParseSpiceNumber(tokens[2]).value_or(0.0);
                cfg.SweptStop = ParseSpiceNumber(tokens[3]).value_or(0.0);
                cfg.SweptStep = ParseSpiceNumber(tokens[4]).value_or(0.0);
                if (tokens.size() >= 9) {
                    cfg.SteppedSource = tokens[5];
                    cfg.SteppedStart = ParseSpiceNumber(tokens[6]).value_or(0.0);
                    cfg.SteppedStop = ParseSpiceNumber(tokens[7]).value_or(0.0);
                    cfg.SteppedStep = ParseSpiceNumber(tokens[8]).value_or(0.0);
                }
                result.Analyses.push_back({AnalysisKind::DC_SWEEP, cfg});
            } else if (dir == ".AC") {
                // .AC [DEC|OCT|LIN] <np> <fstart> <fstop>
                if (tokens.size() < 4) {
                    return std::unexpected(std::format("Malformed .ac directive: '{}'", line));
                }
                const std::expected<FrequencySweepWords, std::string> sweep =
                    ReadFrequencySweep(tokens, 1, false, ".ac", line);
                if (!sweep) {
                    return std::unexpected(sweep.error());
                }
                AcSweepConfig cfg;
                cfg.SweepType = sweep->Type;
                cfg.PointsPerDecade = sweep->Points;
                cfg.StartFrequency = sweep->StartFrequency;
                cfg.StopFrequency = sweep->StopFrequency;
                result.Analyses.push_back({AnalysisKind::AC_SWEEP, cfg});
            } else if (dir == ".TF") {
                // .TF <out_var> <in_src>
                if (tokens.size() < 3) {
                    return std::unexpected(std::format("Malformed .tf directive: '{}'", line));
                }
                TransferFunctionConfig cfg;
                const auto [pos_str, neg_str] = ParseVoltageNodes(tokens[1]);
                cfg.OutputPositiveNode = nodes.Resolve(pos_str);
                cfg.OutputNegativeNode = nodes.Resolve(neg_str);
                cfg.InputSource = tokens[2];
                result.Analyses.push_back({AnalysisKind::TRANSFER_FUNCTION, cfg});
            } else if (dir == ".NOISE") {
                // .NOISE <out_var> <in_src> [DEC|OCT|LIN] <np> <fstart> <fstop>
                if (tokens.size() < 6) {
                    return std::unexpected(std::format("Malformed .noise directive: '{}'", line));
                }
                NoiseConfig cfg;
                const auto [pos_str, neg_str] = ParseVoltageNodes(tokens[1]);
                cfg.OutputPositiveNode = nodes.Resolve(pos_str);
                cfg.OutputNegativeNode = nodes.Resolve(neg_str);
                cfg.InputSource = tokens[2];
                const std::expected<FrequencySweepWords, std::string> sweep =
                    ReadFrequencySweep(tokens, 3, false, ".noise", line);
                if (!sweep) {
                    return std::unexpected(sweep.error());
                }
                cfg.SweepType = sweep->Type;
                cfg.PointsPerDecade = sweep->Points;
                cfg.StartFrequency = sweep->StartFrequency;
                cfg.StopFrequency = sweep->StopFrequency;
                result.Analyses.push_back({AnalysisKind::NOISE, cfg});
            } else if (dir == ".SP") {
                // .SP [DEC|OCT|LIN] <np> <fstart> <fstop>
                if (tokens.size() < 4) {
                    return std::unexpected(std::format("Malformed .sp directive: '{}'", line));
                }
                SParameterConfig cfg;
                int start_idx = 1;
                const std::string scale_type = ToUpper(tokens[1]);
                if (scale_type == "DEC") {
                    cfg.SweepType = FrequencySweepType::DECADE;
                    start_idx = 2;
                } else if (scale_type == "OCT") {
                    cfg.SweepType = FrequencySweepType::OCTAVE;
                    start_idx = 2;
                } else if (scale_type == "LIN") {
                    cfg.SweepType = FrequencySweepType::LINEAR;
                    start_idx = 2;
                }
                cfg.Points = static_cast<int>(ParseSpiceNumber(tokens[start_idx]).value_or(10));
                cfg.StartFrequency = ParseSpiceNumber(tokens[start_idx + 1]).value_or(1e6);
                cfg.StopFrequency = ParseSpiceNumber(tokens[start_idx + 2]).value_or(1e9);
                result.Analyses.push_back({AnalysisKind::S_PARAMETERS, cfg});
            } else if (dir == ".PZ") {
                // .PZ <out_var> <in_src> [POL|ZER|PZ]
                // or .PZ <in_pos> <in_neg> <out_pos> <out_neg> [VOL|CUR] [POL|ZER|PZ]
                if (tokens.size() < 3) {
                    return std::unexpected(std::format("Malformed .pz directive: '{}'", line));
                }
                PoleZeroConfig cfg;
                if (tokens.size() >= 6) {
                    const std::string in_pos = tokens[1];
                    const std::string in_neg = tokens[2];
                    const std::string out_pos = tokens[3];
                    cfg.OutputNode = nodes.Resolve(out_pos);
                    // Search circuit_lines for a source connected between in_pos and in_neg
                    for (const auto &cand_line : circuit_lines) {
                        const auto cand_toks = TokenizeLine(cand_line);
                        if (cand_toks.size() >= 3) {
                            const char fc =
                                static_cast<char>(std::toupper(static_cast<unsigned char>(cand_toks[0].front())));
                            if (fc == 'V' || fc == 'I') {
                                if ((cand_toks[1] == in_pos && cand_toks[2] == in_neg)
                                    || (cand_toks[1] == in_neg && cand_toks[2] == in_pos)) {
                                    cfg.InputSource = cand_toks[0];
                                    break;
                                }
                            }
                        }
                    }
                    for (const BlockSource &source : block_sources) {
                        if (!cfg.InputSource.empty()) {
                            break;
                        }
                        const NodeId in_positive = nodes.Resolve(in_pos);
                        const NodeId in_negative = nodes.Resolve(in_neg);
                        if ((source.Positive == in_positive && source.Negative == in_negative)
                            || (source.Positive == in_negative && source.Negative == in_positive)) {
                            cfg.InputSource = source.Name;
                        }
                    }
                    if (cfg.InputSource.empty()) {
                        return std::unexpected(
                            std::format("Could not find source between nodes '{}' and '{}' for .pz", in_pos, in_neg));
                    }
                    if (tokens.size() >= 7) {
                        const std::string type = ToUpper(tokens[6]);
                        if (type == "POL") {
                            cfg.FindZeros = false;
                        } else if (type == "ZER") {
                            cfg.FindPoles = false;
                        }
                    }
                } else {
                    const auto [pos_str, neg_str] = ParseVoltageNodes(tokens[1]);
                    cfg.OutputNode = nodes.Resolve(pos_str);
                    cfg.InputSource = tokens[2];
                    if (tokens.size() >= 4) {
                        const std::string type = ToUpper(tokens[3]);
                        if (type == "POL") {
                            cfg.FindZeros = false;
                        } else if (type == "ZER") {
                            cfg.FindPoles = false;
                        }
                    }
                }
                result.Analyses.push_back({AnalysisKind::POLE_ZERO, cfg});
            } else if (dir == ".SENS") {
                // .SENS <out_var> (e.g. .sens v(out) or .sens v(out, ref))
                // .SENS <out_var> AC [DEC|OCT|LIN] <np> <fstart> <fstop>
                if (tokens.size() < 2) {
                    return std::unexpected(std::format("Malformed .sens directive: '{}'", line));
                }
                const auto [pos_str, neg_str] = ParseVoltageNodes(tokens[1]);
                const NodeId positive = nodes.Resolve(pos_str);
                const NodeId negative = nodes.Resolve(neg_str);
                if (tokens.size() >= 3 && ToUpper(tokens[2]) == "AC") {
                    const std::expected<AcSensitivityConfig, std::string> cfg =
                        ParseAcSensitivity(tokens, positive, negative, circuit_lines, line);
                    if (!cfg) {
                        return std::unexpected(std::format("{}: '{}'", cfg.error(), line));
                    }
                    result.Analyses.push_back({AnalysisKind::AC_SENSITIVITY, *cfg});
                } else {
                    SensitivityConfig cfg;
                    cfg.OutputPositiveNode = positive;
                    cfg.OutputNegativeNode = negative;
                    result.Analyses.push_back({AnalysisKind::SENSITIVITY, cfg});
                }
            } else if (dir == ".HB") {
                // .HB <f0> <harmonics> [samples=<M>]
                if (tokens.size() < 3) {
                    return std::unexpected(std::format("Malformed .hb directive: '{}'", line));
                }
                const auto f0_opt = ParseSpiceNumber(tokens[1]);
                if (!f0_opt || *f0_opt <= 0.0) {
                    return std::unexpected(std::format("Invalid fundamental frequency in .hb directive: '{}'", line));
                }
                const auto harm_opt = ParseSpiceNumber(tokens[2]);
                if (!harm_opt || *harm_opt < 0.0 || std::floor(*harm_opt) != *harm_opt) {
                    return std::unexpected(std::format("Invalid harmonic count in .hb directive: '{}'", line));
                }
                HarmonicBalanceConfig cfg;
                cfg.FundamentalFrequency = *f0_opt;
                cfg.Harmonics = static_cast<int>(*harm_opt);
                for (std::size_t k = 3; k < tokens.size(); ++k) {
                    const std::string tok_upper = ToUpper(tokens[k]);
                    if (tok_upper.starts_with("SAMPLES=")) {
                        const auto samp_opt = ParseSpiceNumber(std::string_view{tokens[k]}.substr(8));
                        if (!samp_opt || *samp_opt <= 0.0 || std::floor(*samp_opt) != *samp_opt) {
                            return std::unexpected(
                                std::format("Invalid samples parameter in .hb directive: '{}'", line));
                        }
                        cfg.Samples = static_cast<int>(*samp_opt);
                    } else {
                        return std::unexpected(
                            std::format("Unexpected token '{}' in .hb directive: '{}'", tokens[k], line));
                    }
                }
                result.Analyses.push_back({AnalysisKind::HARMONIC_BALANCE, cfg});
            } else if (dir == ".OPTIONS") {
                for (std::size_t i = 1; i < tokens.size(); ++i) {
                    const auto eq = tokens[i].find('=');
                    if (eq != std::string::npos) {
                        const std::string opt_name = ToUpper(tokens[i].substr(0, eq));
                        if (opt_name == "METHOD") {
                            const std::string method_name = ToUpper(tokens[i].substr(eq + 1));
                            if (method_name == "GEAR") {
                                integration_method = IntegrationMethod::GEAR2;
                            } else if (method_name == "TRAP" || method_name == "TRAPEZOIDAL") {
                                integration_method = IntegrationMethod::TRAPEZOIDAL;
                            } else {
                                return std::unexpected(std::format("Unknown integration method '{}' in .options: '{}'",
                                                                   method_name,
                                                                   line));
                            }
                            continue;
                        }
                        const auto opt_val = ParseSpiceNumber(tokens[i].substr(eq + 1));
                        if (opt_val) {
                            if (opt_name == "RELTOL") {
                                result.Options.RelTol = *opt_val;
                            } else if (opt_name == "VNTOL") {
                                result.Options.VoltageAbsTol = *opt_val;
                            } else if (opt_name == "ABSTOL") {
                                result.Options.CurrentAbsTol = *opt_val;
                            } else if (opt_name == "GMIN") {
                                result.Options.Gmin = *opt_val;
                            } else if (opt_name == "MAXITER" || opt_name == "ITL1") {
                                result.Options.MaxIterations = static_cast<int>(*opt_val);
                            } else if (opt_name == "TEMP") {
                                temperature_celsius = *opt_val;
                            } else if (opt_name == "TNOM") {
                                nominal_celsius = *opt_val;
                            } else if (opt_name == "SEED") {
                                random_seed = static_cast<unsigned int>(std::max(0.0, *opt_val));
                            }
                        }
                    }
                }
            } else if (dir == ".TEMP") {
                if (tokens.size() >= 2) {
                    if (const auto temp_val = ParseSpiceNumber(tokens[1])) {
                        temperature_celsius = *temp_val;
                    } else {
                        return std::unexpected(std::format("Invalid temperature '{}' in .temp: '{}'", tokens[1], line));
                    }
                }
            } else if (dir == ".IC" || dir == ".NODESET") {
                if (const auto ic_res = ParseNodeVoltageDirective(tokens, line, nodes, builder, dir == ".NODESET");
                    !ic_res) {
                    return std::unexpected(ic_res.error());
                }
            }
            continue;
        }

        const auto dot_pos = name.rfind('.');
        const std::string_view base_name =
            (dot_pos != std::string::npos) ? std::string_view(name).substr(dot_pos + 1) : std::string_view(name);
        if (base_name.empty()) {
            continue;
        }
        const char first_char = static_cast<char>(std::toupper(static_cast<unsigned char>(base_name.front())));

        // Circuit Components
        switch (first_char) {
        case 'R': { // Resistor: R<name> <n1> <n2> <value>
            if (tokens.size() < 4) {
                return std::unexpected(std::format("Malformed resistor: '{}'", line));
            }
            const NodeId n1 = nodes.Resolve(tokens[1]);
            const NodeId n2 = nodes.Resolve(tokens[2]);
            const auto val = ParseSpiceNumber(tokens[3]);
            if (!val) {
                return std::unexpected(std::format("Invalid resistor value '{}'", tokens[3]));
            }
            builder.AddResistor(name, n1, n2, *val);
            break;
        }

        case 'C': { // Capacitor: C<name> <n1> <n2> <value> [ic=<val>]
            if (tokens.size() < 4) {
                return std::unexpected(std::format("Malformed capacitor: '{}'", line));
            }
            const NodeId n1 = nodes.Resolve(tokens[1]);
            const NodeId n2 = nodes.Resolve(tokens[2]);
            const auto val = ParseSpiceNumber(tokens[3]);
            if (!val) {
                return std::unexpected(std::format("Invalid capacitor value '{}'", tokens[3]));
            }
            double ic = 0.0;
            for (std::size_t i = 4; i < tokens.size(); ++i) {
                const std::string u = ToUpper(tokens[i]);
                if (u.starts_with("IC=")) {
                    if (const auto ic_val = ParseSpiceNumber(u.substr(3))) {
                        ic = *ic_val;
                    }
                }
            }
            builder.AddCapacitor(name, n1, n2, *val, ic);
            break;
        }

        case 'L': { // Inductor: L<name> <n1> <n2> <value> [ic=<val>]
            if (tokens.size() < 4) {
                return std::unexpected(std::format("Malformed inductor: '{}'", line));
            }
            const NodeId n1 = nodes.Resolve(tokens[1]);
            const NodeId n2 = nodes.Resolve(tokens[2]);
            const auto val = ParseSpiceNumber(tokens[3]);
            if (!val) {
                return std::unexpected(std::format("Invalid inductor value '{}'", tokens[3]));
            }
            double ic = 0.0;
            for (std::size_t i = 4; i < tokens.size(); ++i) {
                const std::string u = ToUpper(tokens[i]);
                if (u.starts_with("IC=")) {
                    if (const auto ic_val = ParseSpiceNumber(u.substr(3))) {
                        ic = *ic_val;
                    }
                }
            }
            builder.AddInductor(name, n1, n2, *val, ic);
            break;
        }

        case 'V':
        case 'I': { // Voltage or Current Source
            if (tokens.size() < 3) {
                return std::unexpected(std::format("Malformed source: '{}'", line));
            }
            const NodeId n_pos = nodes.Resolve(tokens[1]);
            const NodeId n_neg = nodes.Resolve(tokens[2]);

            double dc_val = 0.0;
            double ac_mag = 0.0;
            double ac_phase = 0.0;
            bool is_port = false;
            int port_num = 0;
            double z0 = 50.0;

            SourceWaveform wtype = SourceWaveform::NONE;
            std::string waveform_token;

            std::size_t idx = 3;
            while (idx < tokens.size()) {
                const std::string u = ToUpper(tokens[idx]);
                if (u == "PORTNUM" && idx + 1 < tokens.size()) {
                    port_num = static_cast<int>(ParseSpiceNumber(tokens[idx + 1]).value_or(1));
                    idx += 2;
                    is_port = true;
                    continue;
                }
                if (u.starts_with("PORTNUM=")) {
                    port_num = static_cast<int>(ParseSpiceNumber(u.substr(8)).value_or(1));
                    ++idx;
                    is_port = true;
                    continue;
                }
                if (u == "Z0" && idx + 1 < tokens.size()) {
                    z0 = ParseSpiceNumber(tokens[idx + 1]).value_or(50.0);
                    idx += 2;
                    is_port = true;
                    continue;
                }
                if (u.starts_with("Z0=")) {
                    z0 = ParseSpiceNumber(u.substr(3)).value_or(50.0);
                    ++idx;
                    is_port = true;
                    continue;
                }
                if (u == "DC") {
                    if (idx + 1 < tokens.size()) {
                        dc_val = ParseSpiceNumber(tokens[idx + 1]).value_or(0.0);
                        idx += 2;
                        continue;
                    }
                } else if (u == "AC") {
                    if (idx + 1 < tokens.size()) {
                        ac_mag = ParseSpiceNumber(tokens[idx + 1]).value_or(1.0);
                        idx += 2;
                        if (idx < tokens.size() && ParseSpiceNumber(tokens[idx])) {
                            ac_phase = *ParseSpiceNumber(tokens[idx]);
                            ++idx;
                        }
                        continue;
                    }
                } else if (const SourceWaveform found = FindSourceWaveform(tokens[idx]);
                           found != SourceWaveform::NONE) {
                    wtype = found;
                    waveform_token = tokens[idx];
                } else if (const auto num = ParseSpiceNumber(tokens[idx])) {
                    dc_val = *num;
                }
                ++idx;
            }

            if (is_port) {
                if (port_num <= 0) {
                    const auto digits = name.find_first_of("0123456789");
                    if (digits != std::string::npos) {
                        port_num = std::atoi(name.c_str() + digits);
                    } else {
                        port_num = 1;
                    }
                }
                builder.AddPort(port_num, n_pos, z0);
                break;
            }

            const bool is_voltage = (first_char == 'V');

            AddWaveformSource(builder,
                              name,
                              is_voltage,
                              n_pos,
                              n_neg,
                              wtype,
                              ExtractParenthesizedValues(waveform_token),
                              {.Dc = dc_val, .AcMagnitude = ac_mag, .AcPhaseDegrees = ac_phase});
            break;
        }

        case 'P': { // Port element: P<name> <pos> <neg> [portnum=<N>] [z0=<Z>] or P1 <pos> <neg> [Z0]
            if (tokens.size() < 3) {
                return std::unexpected(std::format("Malformed port: '{}'", line));
            }
            const NodeId n_pos = nodes.Resolve(tokens[1]);
            int port_num = 1;
            const auto digits = name.find_first_of("0123456789");
            if (digits != std::string::npos) {
                port_num = std::atoi(name.c_str() + digits);
            }
            double z0 = 50.0;
            for (std::size_t i = 3; i < tokens.size(); ++i) {
                const std::string u = ToUpper(tokens[i]);
                if (u.starts_with("PORTNUM=")) {
                    port_num = static_cast<int>(ParseSpiceNumber(u.substr(8)).value_or(port_num));
                } else if (u.starts_with("Z0=")) {
                    z0 = ParseSpiceNumber(u.substr(3)).value_or(z0);
                } else if (u == "PORTNUM" && i + 1 < tokens.size()) {
                    port_num = static_cast<int>(ParseSpiceNumber(tokens[++i]).value_or(port_num));
                } else if (u == "Z0" && i + 1 < tokens.size()) {
                    z0 = ParseSpiceNumber(tokens[++i]).value_or(z0);
                } else if (const auto val = ParseSpiceNumber(tokens[i])) {
                    z0 = *val;
                }
            }
            builder.AddPort(port_num, n_pos, z0);
            break;
        }

        case 'E': { // VCVS: E<name> <pos> <neg> <ctrl_pos> <ctrl_neg> <gain>
            if (tokens.size() < 6) {
                return std::unexpected(std::format("Malformed VCVS: '{}'", line));
            }
            const NodeId p = nodes.Resolve(tokens[1]);
            const NodeId n = nodes.Resolve(tokens[2]);
            const NodeId cp = nodes.Resolve(tokens[3]);
            const NodeId cn = nodes.Resolve(tokens[4]);
            const auto gain = ParseSpiceNumber(tokens[5]);
            if (!gain) {
                return std::unexpected(std::format("Invalid VCVS gain '{}'", tokens[5]));
            }
            builder.AddVcvs(name, p, n, cp, cn, *gain);
            break;
        }

        case 'G': { // VCCS: G<name> <pos> <neg> <ctrl_pos> <ctrl_neg> <gm>
            if (tokens.size() < 6) {
                return std::unexpected(std::format("Malformed VCCS: '{}'", line));
            }
            const NodeId p = nodes.Resolve(tokens[1]);
            const NodeId n = nodes.Resolve(tokens[2]);
            const NodeId cp = nodes.Resolve(tokens[3]);
            const NodeId cn = nodes.Resolve(tokens[4]);
            const auto gm = ParseSpiceNumber(tokens[5]);
            if (!gm) {
                return std::unexpected(std::format("Invalid VCCS transconductance '{}'", tokens[5]));
            }
            builder.AddVccs(name, p, n, cp, cn, *gm);
            break;
        }

        case 'H': { // CCVS: H<name> <pos> <neg> <v_ctrl> <rm>
            if (tokens.size() < 5) {
                return std::unexpected(std::format("Malformed CCVS: '{}'", line));
            }
            const NodeId p = nodes.Resolve(tokens[1]);
            const NodeId n = nodes.Resolve(tokens[2]);
            const std::string vctrl = tokens[3];
            const auto rm = ParseSpiceNumber(tokens[4]);
            if (!rm) {
                return std::unexpected(std::format("Invalid CCVS transresistance '{}'", tokens[4]));
            }
            builder.AddCcvs(name, p, n, vctrl, *rm);
            break;
        }

        case 'F': { // CCCS: F<name> <pos> <neg> <v_ctrl> <gain>
            if (tokens.size() < 5) {
                return std::unexpected(std::format("Malformed CCCS: '{}'", line));
            }
            const NodeId p = nodes.Resolve(tokens[1]);
            const NodeId n = nodes.Resolve(tokens[2]);
            const std::string vctrl = tokens[3];
            const auto gain = ParseSpiceNumber(tokens[4]);
            if (!gain) {
                return std::unexpected(std::format("Invalid CCCS gain '{}'", tokens[4]));
            }
            builder.AddCccs(name, p, n, vctrl, *gain);
            break;
        }

        case 'B': { // Behavioral source: B<name> <pos> <neg> V=<expression> or I=<expression>
            const std::optional<BehavioralLine> behavioral = SplitBehavioralLine(line);
            if (tokens.size() < 4 || !behavioral) {
                return std::unexpected(std::format("Malformed behavioral source: '{}'", line));
            }
            const auto expression = Expression::Parse(behavioral->Expression);
            if (!expression) {
                return std::unexpected(std::format("{}: {}", name, expression.error()));
            }
            // The builder reads nodes by number, so every v(...) is written with the numbers this netlist gave them
            std::string resolved;
            std::size_t copied = 0;
            for (const ExpressionReference &reference : expression->GetReferences()) {
                resolved.append(behavioral->Expression, copied, reference.Start - copied);
                copied = reference.Start + reference.Length;
                if (reference.Input.Type == ExpressionInput::Kind::CURRENT) {
                    resolved.append(behavioral->Expression, reference.Start, reference.Length);
                } else if (reference.Input.Second.empty()) {
                    resolved.append(std::format("v({})", nodes.Resolve(reference.Input.First)));
                } else {
                    resolved.append(std::format("v({},{})",
                                                nodes.Resolve(reference.Input.First),
                                                nodes.Resolve(reference.Input.Second)));
                }
            }
            resolved.append(behavioral->Expression, copied, std::string::npos);
            const NodeId p = nodes.Resolve(tokens[1]);
            const NodeId n = nodes.Resolve(tokens[2]);
            if (behavioral->IsVoltage) {
                builder.AddBehavioralVoltage(name, p, n, resolved);
            } else {
                builder.AddBehavioralCurrent(name, p, n, resolved);
            }
            break;
        }

        case 'D': { // Diode: D<name> <anode> <cathode> <model> [area]
            if (tokens.size() < 4) {
                return std::unexpected(std::format("Malformed diode: '{}'", line));
            }
            const NodeId an = nodes.Resolve(tokens[1]);
            const NodeId cat = nodes.Resolve(tokens[2]);
            const std::string mname = ToUpper(tokens[3]);
            DiodeParameters p;
            auto it = models.find(mname);
            if (it != models.end()) {
                p = BuildDiodeParameters(it->second);
            }
            double area = 1.0;
            if (tokens.size() >= 5) {
                if (const auto a = ParseSpiceNumber(tokens[4])) {
                    area = *a;
                }
            }
            builder.AddDiode(name, an, cat, p, area);
            break;
        }

        case 'Q': { // BJT: Q<name> <c> <b> <e> <model> [area]
            if (tokens.size() < 5) {
                return std::unexpected(std::format("Malformed BJT: '{}'", line));
            }
            const NodeId c = nodes.Resolve(tokens[1]);
            const NodeId b = nodes.Resolve(tokens[2]);
            const NodeId e = nodes.Resolve(tokens[3]);
            const std::string mname = ToUpper(tokens[4]);
            BjtParameters p;
            bool is_npn = true;
            auto it = models.find(mname);
            if (it != models.end()) {
                p = BuildBjtParameters(it->second);
                is_npn = (it->second.Kind != ParsedModel::ModelKind::BJT_PNP);
            }
            double area = 1.0;
            if (tokens.size() >= 6) {
                if (const auto a = ParseSpiceNumber(tokens[5])) {
                    area = *a;
                }
            }
            builder.AddBjt(name, c, b, e, is_npn, p, area);
            break;
        }

        case 'M': { // MOSFET: M<name> <d> <g> <s> <b> <model> [L=<l>] [W=<w>]
            if (tokens.size() < 6) {
                return std::unexpected(std::format("Malformed MOSFET: '{}'", line));
            }
            const NodeId d = nodes.Resolve(tokens[1]);
            const NodeId g = nodes.Resolve(tokens[2]);
            const NodeId s = nodes.Resolve(tokens[3]);
            const NodeId b = nodes.Resolve(tokens[4]);
            const std::string mname = ToUpper(tokens[5]);
            MosfetParameters p;
            bool is_nmos = true;
            auto it = models.find(mname);
            if (it != models.end()) {
                p = BuildMosfetParameters(it->second);
                is_nmos = (it->second.Kind != ParsedModel::ModelKind::MOSFET_PMOS);
            }
            for (std::size_t i = 6; i < tokens.size(); ++i) {
                const std::string u = ToUpper(tokens[i]);
                if (u.starts_with("L=")) {
                    if (const auto lval = ParseSpiceNumber(u.substr(2))) {
                        p.Length = *lval;
                    }
                } else if (u.starts_with("W=")) {
                    if (const auto wval = ParseSpiceNumber(u.substr(2))) {
                        p.Width = *wval;
                    }
                }
            }
            builder.AddMosfet(name, d, g, s, b, is_nmos, p);
            break;
        }

        case 'S': { // Switch: S<name> <a1> <a2> <c1> <c2> <model>
            if (tokens.size() < 6) {
                return std::unexpected(std::format("Malformed switch: '{}'", line));
            }
            const NodeId a1 = nodes.Resolve(tokens[1]);
            const NodeId a2 = nodes.Resolve(tokens[2]);
            const NodeId c1 = nodes.Resolve(tokens[3]);
            const NodeId c2 = nodes.Resolve(tokens[4]);
            const std::string mname = ToUpper(tokens[5]);
            SwitchParameters p;
            auto it = models.find(mname);
            if (it != models.end()) {
                p = BuildSwitchParameters(it->second);
            }
            builder.AddVoltageControlledSwitch(name, a1, a2, c1, c2, p);
            break;
        }

        case 'U': { // Logic gate: U<name> <out> <in1> [<in2> ...] <gate_type> [VHIGH=5] [VLOW=0] [VTH=2.5] [TPD=1n]
                    // [ROUT=50] [HYST=0.5]
            if (tokens.size() < 4) {
                return std::unexpected(std::format("Malformed logic gate: '{}'", line));
            }
            int gate_type_idx = -1;
            LogicGateType gate_type = LogicGateType::NOT;
            for (std::size_t i = 2; i < tokens.size(); ++i) {
                const std::string upper = ToUpper(tokens[i]);
                if (upper == "NOT" || upper == "INV") {
                    gate_type = LogicGateType::NOT;
                    gate_type_idx = static_cast<int>(i);
                    break;
                }
                if (upper == "BUF" || upper == "BUFFER") {
                    gate_type = LogicGateType::BUFFER;
                    gate_type_idx = static_cast<int>(i);
                    break;
                }
                if (upper == "AND") {
                    gate_type = LogicGateType::AND;
                    gate_type_idx = static_cast<int>(i);
                    break;
                }
                if (upper == "NAND") {
                    gate_type = LogicGateType::NAND;
                    gate_type_idx = static_cast<int>(i);
                    break;
                }
                if (upper == "OR") {
                    gate_type = LogicGateType::OR;
                    gate_type_idx = static_cast<int>(i);
                    break;
                }
                if (upper == "NOR") {
                    gate_type = LogicGateType::NOR;
                    gate_type_idx = static_cast<int>(i);
                    break;
                }
                if (upper == "XOR") {
                    gate_type = LogicGateType::XOR;
                    gate_type_idx = static_cast<int>(i);
                    break;
                }
                if (upper == "XNOR") {
                    gate_type = LogicGateType::XNOR;
                    gate_type_idx = static_cast<int>(i);
                    break;
                }
                if (upper == "SCHMITT") {
                    gate_type = LogicGateType::SCHMITT_TRIGGER;
                    gate_type_idx = static_cast<int>(i);
                    break;
                }
            }
            if (gate_type_idx < 2) {
                return std::unexpected(std::format("Missing or unrecognized logic gate type in: '{}'", line));
            }
            const NodeId out = nodes.Resolve(tokens[1]);
            std::vector<NodeId> inputs;
            for (int i = 2; i < gate_type_idx; ++i) {
                inputs.push_back(nodes.Resolve(tokens[static_cast<std::size_t>(i)]));
            }
            if (inputs.empty()) {
                return std::unexpected(std::format("Logic gate '{}' has no inputs", name));
            }
            SmoothLogicParameters params;
            for (std::size_t i = static_cast<std::size_t>(gate_type_idx + 1); i < tokens.size(); ++i) {
                const std::string u = ToUpper(tokens[i]);
                if (u.starts_with("VHIGH=")) {
                    if (const auto v = ParseSpiceNumber(u.substr(6))) {
                        params.VHigh = *v;
                    }
                } else if (u.starts_with("VLOW=")) {
                    if (const auto v = ParseSpiceNumber(u.substr(5))) {
                        params.VLow = *v;
                    }
                } else if (u.starts_with("VTH=")) {
                    if (const auto v = ParseSpiceNumber(u.substr(4))) {
                        params.VThreshold = *v;
                    }
                } else if (u.starts_with("TPD=")) {
                    if (const auto v = ParseSpiceNumber(u.substr(4))) {
                        params.PropagationDelay = *v;
                    }
                } else if (u.starts_with("ROUT=")) {
                    if (const auto v = ParseSpiceNumber(u.substr(5))) {
                        params.OutputResistance = *v;
                    }
                } else if (u.starts_with("HYST=")) {
                    if (const auto v = ParseSpiceNumber(u.substr(5))) {
                        params.Hysteresis = *v;
                    }
                }
            }
            builder.AddLogicGate(name, gate_type, out, std::move(inputs), params);
            break;
        }

        case 'X': // placed before the loop
            break;

        default:
            result.Diagnostics.push_back({std::format("Ignored unrecognized device line: '{}'", line)});
            break;
        }
    }

    builder.SetTemperature(temperature_celsius, nominal_celsius);
    auto circuit_res = builder.Build();
    if (!circuit_res) {
        return std::unexpected(circuit_res.error());
    }
    result.CircuitInstance = std::move(*circuit_res);

    if (integration_method || random_seed) {
        for (ParsedAnalysis &analysis : result.Analyses) {
            if (auto *transient = std::get_if<TransientConfig>(&analysis.Config)) {
                if (integration_method) {
                    transient->Method = *integration_method;
                }
                if (random_seed) {
                    transient->Seed = *random_seed;
                }
            }
        }
    }

    // If no analysis was specified, default to .OP
    if (result.Analyses.empty()) {
        result.Analyses.push_back({AnalysisKind::OPERATING_POINT, std::monostate{}});
    }

    return result;
}

} // namespace Nodalys
