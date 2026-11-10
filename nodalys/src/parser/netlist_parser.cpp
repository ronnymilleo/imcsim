/**
 * @file    netlist_parser.cpp
 * @brief   Parses SPICE netlists into compiled circuits and simulation requests.
 */

#include "nodalys/netlist_parser.h"
#include "nodalys/circuit_builder.h"
#include "parser/spice_number.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <format>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace nodalys {

namespace {

std::string ToUpper(std::string_view sv) {
    std::string s;
    s.reserve(sv.size());
    for (const char c : sv) {
        s.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    return s;
}

std::string ToLower(std::string_view sv) {
    std::string s;
    s.reserve(sv.size());
    for (const char c : sv) {
        s.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return s;
}

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
        while (i < n && !std::isspace(static_cast<unsigned char>(line[i])) && line[i] != ',' && line[i] != ';' &&
               line[i] != '$') {
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

struct ParsedModel {
    enum class ModelKind {
        Diode,
        BjtNpn,
        BjtPnp,
        MosfetNmos,
        MosfetPmos,
        Switch
    };
    ModelKind Kind = ModelKind::Diode;
    std::map<std::string, double> Params;
};

DiodeParameters BuildDiodeParameters(const ParsedModel &m) {
    DiodeParameters p;
    auto get = [&](std::string_view k, double def) {
        auto it = m.Params.find(std::string(k));
        return it != m.Params.end() ? it->second : def;
    };
    p.SaturationCurrent = get("IS", p.SaturationCurrent);
    p.SeriesResistance = get("RS", p.SeriesResistance);
    p.EmissionCoefficient = get("N", p.EmissionCoefficient);
    p.BreakdownVoltage = get("BV", p.BreakdownVoltage);
    p.BreakdownCurrent = get("IBV", p.BreakdownCurrent);
    p.JunctionCapacitance = get("CJO", p.JunctionCapacitance);
    p.JunctionPotential = get("VJ", p.JunctionPotential);
    p.GradingCoefficient = get("M", p.GradingCoefficient);
    p.TransitTime = get("TT", p.TransitTime);
    return p;
}

BjtParameters BuildBjtParameters(const ParsedModel &m) {
    BjtParameters p;
    auto get = [&](std::string_view k, double def) {
        auto it = m.Params.find(std::string(k));
        return it != m.Params.end() ? it->second : def;
    };
    p.SaturationCurrent = get("IS", p.SaturationCurrent);
    p.ForwardBeta = get("BF", p.ForwardBeta);
    p.ReverseBeta = get("BR", p.ReverseBeta);
    p.EarlyVoltage = get("VAF", p.EarlyVoltage);
    p.ForwardKneeCurrent = get("IKF", p.ForwardKneeCurrent);
    p.LeakageSaturationCurrent = get("ISE", p.LeakageSaturationCurrent);
    p.LeakageEmissionCoefficient = get("NE", p.LeakageEmissionCoefficient);
    p.BaseResistance = get("RB", p.BaseResistance);
    p.CollectorResistance = get("RC", p.CollectorResistance);
    p.EmitterResistance = get("RE", p.EmitterResistance);
    p.EmitterCapacitance = get("CJE", p.EmitterCapacitance);
    p.CollectorCapacitance = get("CJC", p.CollectorCapacitance);
    p.TransitTime = get("TF", p.TransitTime);
    return p;
}

MosfetParameters BuildMosfetParameters(const ParsedModel &m) {
    MosfetParameters p;
    auto get = [&](std::string_view k, double def) {
        auto it = m.Params.find(std::string(k));
        return it != m.Params.end() ? it->second : def;
    };
    p.ThresholdVoltage = get("VTO", p.ThresholdVoltage);
    p.Transconductance = get("KP", p.Transconductance);
    p.ChannelModulation = get("LAMBDA", p.ChannelModulation);
    p.DrainResistance = get("RD", p.DrainResistance);
    p.SourceResistance = get("RS", p.SourceResistance);
    p.GateSourceOverlap = get("CGSO", p.GateSourceOverlap);
    p.GateDrainOverlap = get("CGDO", p.GateDrainOverlap);
    p.Length = get("L", p.Length);
    p.Width = get("W", p.Width);
    return p;
}

SwitchParameters BuildSwitchParameters(const ParsedModel &m) {
    SwitchParameters p;
    auto get = [&](std::string_view k, double def) {
        auto it = m.Params.find(std::string(k));
        return it != m.Params.end() ? it->second : def;
    };
    if (m.Params.contains("VT")) {
        p.Threshold = m.Params.at("VT");
    } else if (m.Params.contains("VTHRESHOLD")) {
        p.Threshold = m.Params.at("VTHRESHOLD");
    }
    if (m.Params.contains("VH")) {
        p.Hysteresis = m.Params.at("VH");
    } else if (m.Params.contains("VHYSTERESIS")) {
        p.Hysteresis = m.Params.at("VHYSTERESIS");
    }
    if (m.Params.contains("RON")) {
        p.OnResistance = m.Params.at("RON");
    }
    if (m.Params.contains("ROFF")) {
        p.OffResistance = m.Params.at("ROFF");
    }
    return p;
}

struct SubcircuitDef {
    std::string Name;
    std::vector<std::string> Pins;
    std::vector<std::string> Lines;
};

// Map node string (e.g. "0", "GND", "IN", "1") to NodeId
class NodeManager {
public:
    void Register(std::string_view name) {
        const std::string upper = ToUpper(Trim(name));
        if (upper.empty() || upper == "0" || upper == "GND" || upper == "GROUND") {
            return;
        }
        if (m_Registered.contains(upper)) {
            return;
        }
        m_Registered.insert(upper);

        int num = 0;
        const auto [end, err] = std::from_chars(upper.data(), upper.data() + upper.size(), num);
        if (err == std::errc() && end == upper.data() + upper.size() && num > 0 && num <= 10000) {
            m_NumericNodes.push_back({upper, num});
            m_MaxNumeric = std::max(m_MaxNumeric, num);
        } else {
            m_SymbolicNodes.push_back(upper);
        }
    }

    void Finalize(CircuitBuilder &builder) {
        for (const auto &[name, num] : m_NumericNodes) {
            m_NodeMap[name] = num;
        }

        int next_id = m_MaxNumeric + 1;
        for (const auto &name : m_SymbolicNodes) {
            if (!m_NodeMap.contains(name)) {
                m_NodeMap[name] = next_id++;
            }
        }

        const int total_nodes = std::max(next_id, 1);
        builder.SetCircuitNodeCount(total_nodes);
    }

    NodeId Resolve(std::string_view name) const {
        const std::string upper = ToUpper(Trim(name));
        if (upper.empty() || upper == "0" || upper == "GND" || upper == "GROUND") {
            return GroundNode;
        }
        const auto it = m_NodeMap.find(upper);
        if (it != m_NodeMap.end()) {
            return it->second;
        }
        int num = 0;
        const auto [end, err] = std::from_chars(upper.data(), upper.data() + upper.size(), num);
        if (err == std::errc() && end == upper.data() + upper.size() && num > 0) {
            return num;
        }
        return GroundNode;
    }

private:
    std::set<std::string> m_Registered;
    std::vector<std::pair<std::string, int>> m_NumericNodes;
    std::vector<std::string> m_SymbolicNodes;
    int m_MaxNumeric = 0;
    std::map<std::string, NodeId> m_NodeMap;
};

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
    // SPICE standard: first line is circuit title
    result.Title = logical_lines.front();

    // 2. First pass: Collect .model and .subckt definitions
    std::map<std::string, ParsedModel> models;
    std::map<std::string, SubcircuitDef> subcircuits;
    std::vector<std::string> circuit_lines;

    bool in_subcircuit = false;
    SubcircuitDef current_subckt;

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
            in_subcircuit = true;
            current_subckt.Name = ToUpper(tokens[1]);
            current_subckt.Pins.clear();
            for (std::size_t p = 2; p < tokens.size(); ++p) {
                current_subckt.Pins.push_back(ToUpper(tokens[p]));
            }
            current_subckt.Lines.clear();
            continue;
        }

        if (directive == ".ENDS") {
            in_subcircuit = false;
            subcircuits[current_subckt.Name] = std::move(current_subckt);
            current_subckt = SubcircuitDef{};
            continue;
        }

        if (in_subcircuit) {
            current_subckt.Lines.push_back(line);
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
                model.Kind = ParsedModel::ModelKind::Diode;
            } else if (type_str == "NPN") {
                model.Kind = ParsedModel::ModelKind::BjtNpn;
            } else if (type_str == "PNP") {
                model.Kind = ParsedModel::ModelKind::BjtPnp;
            } else if (type_str == "NMOS") {
                model.Kind = ParsedModel::ModelKind::MosfetNmos;
            } else if (type_str == "PMOS") {
                model.Kind = ParsedModel::ModelKind::MosfetPmos;
            } else if (type_str == "SW" || type_str == "VSWITCH") {
                model.Kind = ParsedModel::ModelKind::Switch;
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
                while (pos < plen && (std::isspace(static_cast<unsigned char>(all_params[pos])) ||
                                      all_params[pos] == '(' || all_params[pos] == ')' || all_params[pos] == ',')) {
                    ++pos;
                }
                if (pos >= plen) {
                    break;
                }
                const std::size_t kstart = pos;
                while (pos < plen &&
                       (std::isalnum(static_cast<unsigned char>(all_params[pos])) || all_params[pos] == '_')) {
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
                    while (pos < plen && !std::isspace(static_cast<unsigned char>(all_params[pos])) &&
                           all_params[pos] != ')' && all_params[pos] != ',') {
                        ++pos;
                    }
                    const std::string val_str = all_params.substr(vstart, pos - vstart);
                    if (const auto num = ParseSpiceNumber(val_str)) {
                        model.Params[key] = *num;
                    }
                }
            }

            models[model_name] = std::move(model);
            continue;
        }

        if (directive == ".END") {
            break;
        }

        circuit_lines.push_back(line);
    }

    // 3. Expand Subcircuits ('X' devices) into flat circuit lines
    std::vector<std::string> flat_lines;
    std::function<std::expected<void, std::string>(const std::string &line, const std::string &prefix,
                                                   const std::map<std::string, std::string> &pin_map)>
        expand_line;

    expand_line = [&](const std::string &line, const std::string &prefix,
                      const std::map<std::string, std::string> &pin_map) -> std::expected<void, std::string> {
        const auto tokens = TokenizeLine(line);
        if (tokens.empty()) {
            return {};
        }

        if (ToUpper(tokens[0]).starts_with('X')) {
            // Subcircuit instantiation: X<name> <pins...> <subckt_name>
            if (tokens.size() < 3) {
                return std::unexpected(std::format("Malformed subcircuit call: '{}'", line));
            }
            const std::string inst_name = prefix.empty() ? tokens[0] : prefix + "." + tokens[0];
            const std::string subckt_name = ToUpper(tokens.back());

            auto it = subcircuits.find(subckt_name);
            if (it == subcircuits.end()) {
                return std::unexpected(std::format("Subcircuit '{}' not found", subckt_name));
            }

            const auto &sub = it->second;
            const std::size_t formal_pin_count = sub.Pins.size();
            const std::size_t actual_pin_count = tokens.size() - 2;
            if (actual_pin_count != formal_pin_count) {
                return std::unexpected(std::format("Subcircuit '{}' expects {} pins, got {}", subckt_name,
                                                   formal_pin_count, actual_pin_count));
            }

            std::map<std::string, std::string> child_pin_map;
            for (std::size_t p = 0; p < formal_pin_count; ++p) {
                std::string actual_pin = ToUpper(tokens[1 + p]);
                auto p_it = pin_map.find(actual_pin);
                if (p_it != pin_map.end()) {
                    actual_pin = p_it->second;
                } else if (!prefix.empty() && actual_pin != "0" && actual_pin != "GND") {
                    actual_pin = prefix + "." + actual_pin;
                }
                child_pin_map[sub.Pins[p]] = actual_pin;
            }

            for (const auto &sub_line : sub.Lines) {
                auto exp_res = expand_line(sub_line, inst_name, child_pin_map);
                if (!exp_res) {
                    return exp_res;
                }
            }
            return {};
        }

        // Normal element inside subcircuit: map its nodes and prepend instance prefix
        if (!prefix.empty()) {
            std::string rewritten = prefix + "." + tokens[0];
            for (std::size_t i = 1; i < tokens.size(); ++i) {
                rewritten.push_back(' ');
                const std::string upper_tok = ToUpper(tokens[i]);
                auto it = pin_map.find(upper_tok);
                if (it != pin_map.end()) {
                    rewritten.append(it->second);
                } else if (i <= 4 && !upper_tok.starts_with('.') && !ParseSpiceNumber(tokens[i]) && upper_tok != "0" &&
                           upper_tok != "GND") {
                    rewritten.append(prefix + "." + tokens[i]);
                } else {
                    rewritten.append(tokens[i]);
                }
            }
            flat_lines.push_back(std::move(rewritten));
        } else {
            flat_lines.push_back(line);
        }
        return {};
    };

    for (const auto &line : circuit_lines) {
        auto res = expand_line(line, "", {});
        if (!res) {
            return std::unexpected(res.error());
        }
    }

    // 4. Parse components and analyses using CircuitBuilder
    CircuitBuilder builder;
    NodeManager nodes;

    // Pre-scan all flat_lines to discover and register all circuit nodes
    for (const auto &line : flat_lines) {
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

        case 'U': {
            for (std::size_t i = 1; i < tokens.size(); ++i) {
                if (tokens[i].find('=') != std::string::npos) {
                    break;
                }
                const std::string upper = ToUpper(tokens[i]);
                if (upper == "AND" || upper == "NAND" || upper == "OR" || upper == "NOR" || upper == "XOR" ||
                    upper == "XNOR" || upper == "NOT" || upper == "INV" || upper == "BUF" || upper == "BUFFER" ||
                    upper == "SCHMITT") {
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

    nodes.Finalize(builder);

    for (const auto &line : flat_lines) {
        const auto tokens = TokenizeLine(line);
        if (tokens.empty()) {
            continue;
        }

        const std::string name = tokens[0];

        // Directives
        if (name.front() == '.') {
            const std::string dir = ToUpper(name);
            if (dir == ".OP") {
                result.Analyses.push_back({AnalysisKind::OperatingPoint, std::monostate{}});
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
                result.Analyses.push_back({AnalysisKind::Transient, cfg});
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
                result.Analyses.push_back({AnalysisKind::DcSweep, cfg});
            } else if (dir == ".AC") {
                // .AC [DEC|OCT|LIN] <np> <fstart> <fstop>
                if (tokens.size() < 4) {
                    return std::unexpected(std::format("Malformed .ac directive: '{}'", line));
                }
                AcSweepConfig cfg;
                int start_idx = 1;
                const std::string scale_type = ToUpper(tokens[1]);
                if (scale_type == "DEC" || scale_type == "OCT" || scale_type == "LIN") {
                    start_idx = 2;
                }
                cfg.PointsPerDecade = static_cast<int>(ParseSpiceNumber(tokens[start_idx]).value_or(10));
                cfg.StartFrequency = ParseSpiceNumber(tokens[start_idx + 1]).value_or(1.0);
                cfg.StopFrequency = ParseSpiceNumber(tokens[start_idx + 2]).value_or(1e6);
                result.Analyses.push_back({AnalysisKind::AcSweep, cfg});
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
                result.Analyses.push_back({AnalysisKind::TransferFunction, cfg});
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
                int sidx = 3;
                const std::string stype = ToUpper(tokens[3]);
                if (stype == "DEC" || stype == "OCT" || stype == "LIN") {
                    sidx = 4;
                }
                cfg.PointsPerDecade = static_cast<int>(ParseSpiceNumber(tokens[sidx]).value_or(10));
                cfg.StartFrequency = ParseSpiceNumber(tokens[sidx + 1]).value_or(1.0);
                cfg.StopFrequency = ParseSpiceNumber(tokens[sidx + 2]).value_or(1e6);
                result.Analyses.push_back({AnalysisKind::Noise, cfg});
            } else if (dir == ".SP") {
                // .SP [DEC|OCT|LIN] <np> <fstart> <fstop>
                if (tokens.size() < 4) {
                    return std::unexpected(std::format("Malformed .sp directive: '{}'", line));
                }
                SParameterConfig cfg;
                int start_idx = 1;
                const std::string scale_type = ToUpper(tokens[1]);
                if (scale_type == "DEC") {
                    cfg.SweepType = FrequencySweepType::Decade;
                    start_idx = 2;
                } else if (scale_type == "OCT") {
                    cfg.SweepType = FrequencySweepType::Octave;
                    start_idx = 2;
                } else if (scale_type == "LIN") {
                    cfg.SweepType = FrequencySweepType::Linear;
                    start_idx = 2;
                }
                cfg.Points = static_cast<int>(ParseSpiceNumber(tokens[start_idx]).value_or(10));
                cfg.StartFrequency = ParseSpiceNumber(tokens[start_idx + 1]).value_or(1e6);
                cfg.StopFrequency = ParseSpiceNumber(tokens[start_idx + 2]).value_or(1e9);
                result.Analyses.push_back({AnalysisKind::SParameters, cfg});
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
                    // Search flat_lines for a source connected between in_pos and in_neg
                    for (const auto &cand_line : flat_lines) {
                        const auto cand_toks = TokenizeLine(cand_line);
                        if (cand_toks.size() >= 3) {
                            const char fc =
                                static_cast<char>(std::toupper(static_cast<unsigned char>(cand_toks[0].front())));
                            if (fc == 'V' || fc == 'I') {
                                if ((cand_toks[1] == in_pos && cand_toks[2] == in_neg) ||
                                    (cand_toks[1] == in_neg && cand_toks[2] == in_pos)) {
                                    cfg.InputSource = cand_toks[0];
                                    break;
                                }
                            }
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
                result.Analyses.push_back({AnalysisKind::PoleZero, cfg});
            } else if (dir == ".OPTIONS") {
                for (std::size_t i = 1; i < tokens.size(); ++i) {
                    const auto eq = tokens[i].find('=');
                    if (eq != std::string::npos) {
                        const std::string opt_name = ToUpper(tokens[i].substr(0, eq));
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
                            }
                        }
                    }
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

            enum class WaveformType {
                None,
                Pulse,
                Sine,
                Exp,
                Pwl,
                Sffm,
                Am
            };
            WaveformType wtype = WaveformType::None;
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
                } else if (u.starts_with("PULSE(")) {
                    wtype = WaveformType::Pulse;
                    waveform_token = tokens[idx];
                } else if (u.starts_with("SIN(") || u.starts_with("SINE(")) {
                    wtype = WaveformType::Sine;
                    waveform_token = tokens[idx];
                } else if (u.starts_with("EXP(")) {
                    wtype = WaveformType::Exp;
                    waveform_token = tokens[idx];
                } else if (u.starts_with("PWL(")) {
                    wtype = WaveformType::Pwl;
                    waveform_token = tokens[idx];
                } else if (u.starts_with("SFFM(")) {
                    wtype = WaveformType::Sffm;
                    waveform_token = tokens[idx];
                } else if (u.starts_with("AM(")) {
                    wtype = WaveformType::Am;
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

            switch (wtype) {
            case WaveformType::Pulse: {
                const auto vals = ExtractParenthesizedValues(waveform_token);
                PulseParameters p;
                if (vals.size() >= 1) {
                    p.Low = vals[0];
                }
                if (vals.size() >= 2) {
                    p.High = vals[1];
                }
                if (vals.size() >= 3) {
                    p.Delay = vals[2];
                }
                if (vals.size() >= 4) {
                    p.RiseTime = vals[3];
                }
                if (vals.size() >= 5) {
                    p.FallTime = vals[4];
                }
                if (vals.size() >= 6) {
                    p.Width = vals[5];
                }
                if (vals.size() >= 7) {
                    p.Period = vals[6];
                }
                if (is_voltage) {
                    builder.AddVpulse(name, n_pos, n_neg, p, ac_mag, ac_phase);
                } else {
                    builder.AddIpulse(name, n_pos, n_neg, p, ac_mag, ac_phase);
                }
                break;
            }
            case WaveformType::Sine: {
                const auto vals = ExtractParenthesizedValues(waveform_token);
                SineParameters p;
                if (vals.size() >= 1) {
                    p.Offset = vals[0];
                }
                if (vals.size() >= 2) {
                    p.Amplitude = vals[1];
                }
                if (vals.size() >= 3) {
                    p.Frequency = vals[2];
                }
                if (vals.size() >= 4) {
                    p.Phase = vals[3];
                }
                p.Magnitude = ac_mag > 0.0 ? ac_mag : p.Amplitude;
                if (is_voltage) {
                    builder.AddVsine(name, n_pos, n_neg, p, ac_mag, ac_phase);
                } else {
                    builder.AddIsine(name, n_pos, n_neg, p, ac_mag, ac_phase);
                }
                break;
            }
            case WaveformType::Exp: {
                const auto vals = ExtractParenthesizedValues(waveform_token);
                ExpParameters p;
                if (vals.size() >= 1) {
                    p.InitialValue = vals[0];
                }
                if (vals.size() >= 2) {
                    p.PeakValue = vals[1];
                }
                if (vals.size() >= 3) {
                    p.RiseDelay = vals[2];
                }
                if (vals.size() >= 4) {
                    p.RiseTimeConstant = vals[3];
                }
                if (vals.size() >= 5) {
                    p.FallDelay = vals[4];
                }
                if (vals.size() >= 6) {
                    p.FallTimeConstant = vals[5];
                }
                if (is_voltage) {
                    builder.AddVexp(name, n_pos, n_neg, p, ac_mag, ac_phase);
                } else {
                    builder.AddIexp(name, n_pos, n_neg, p, ac_mag, ac_phase);
                }
                break;
            }
            case WaveformType::Pwl: {
                const auto vals = ExtractParenthesizedValues(waveform_token);
                PwlParameters p;
                p.Points.clear();
                for (std::size_t i = 0; i + 1 < vals.size(); i += 2) {
                    p.Points.push_back({vals[i], vals[i + 1]});
                }
                if (is_voltage) {
                    builder.AddVpwl(name, n_pos, n_neg, p, ac_mag, ac_phase);
                } else {
                    builder.AddIpwl(name, n_pos, n_neg, p, ac_mag, ac_phase);
                }
                break;
            }
            case WaveformType::Sffm: {
                const auto vals = ExtractParenthesizedValues(waveform_token);
                SffmParameters p;
                if (vals.size() >= 1) {
                    p.Offset = vals[0];
                }
                if (vals.size() >= 2) {
                    p.Amplitude = vals[1];
                }
                if (vals.size() >= 3) {
                    p.CarrierFrequency = vals[2];
                }
                if (vals.size() >= 4) {
                    p.ModulationIndex = vals[3];
                }
                if (vals.size() >= 5) {
                    p.SignalFrequency = vals[4];
                }
                if (is_voltage) {
                    builder.AddVsffm(name, n_pos, n_neg, p, ac_mag, ac_phase);
                } else {
                    builder.AddIsffm(name, n_pos, n_neg, p, ac_mag, ac_phase);
                }
                break;
            }
            case WaveformType::Am: {
                const auto vals = ExtractParenthesizedValues(waveform_token);
                AmParameters p;
                if (vals.size() >= 1) {
                    p.Amplitude = vals[0];
                }
                if (vals.size() >= 2) {
                    p.Offset = vals[1];
                }
                if (vals.size() >= 3) {
                    p.ModulatingFrequency = vals[2];
                }
                if (vals.size() >= 4) {
                    p.CarrierFrequency = vals[3];
                }
                if (vals.size() >= 5) {
                    p.Delay = vals[4];
                }
                if (is_voltage) {
                    builder.AddVam(name, n_pos, n_neg, p, ac_mag, ac_phase);
                } else {
                    builder.AddIam(name, n_pos, n_neg, p, ac_mag, ac_phase);
                }
                break;
            }
            case WaveformType::None:
            default:
                if (is_voltage) {
                    builder.AddVdc(name, n_pos, n_neg, dc_val, ac_mag, ac_phase);
                } else {
                    builder.AddIdc(name, n_pos, n_neg, dc_val, ac_mag, ac_phase);
                }
                break;
            }
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
                is_npn = (it->second.Kind != ParsedModel::ModelKind::BjtPnp);
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
                is_nmos = (it->second.Kind != ParsedModel::ModelKind::MosfetPmos);
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

        default:
            result.Diagnostics.push_back({std::format("Ignored unrecognized device line: '{}'", line)});
            break;
        }
    }

    auto circuit_res = builder.Build();
    if (!circuit_res) {
        return std::unexpected(circuit_res.error());
    }
    result.CircuitInstance = std::move(*circuit_res);

    // If no analysis was specified, default to .OP
    if (result.Analyses.empty()) {
        result.Analyses.push_back({AnalysisKind::OperatingPoint, std::monostate{}});
    }

    return result;
}

} // namespace nodalys
