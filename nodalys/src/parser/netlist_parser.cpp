/**
 * @file    netlist_parser.cpp
 * @brief   Parses SPICE netlists into compiled circuits and simulation requests.
 */

#include "nodalys/netlist_parser.h"
#include "nodalys/circuit_builder.h"
#include "nodalys/expression.h"
#include "parser/model_cards.h"
#include "parser/netlist_context.h"
#include "parser/netlist_deck.h"
#include "parser/netlist_directives.h"
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

// Helper to extract numbers inside parentheses: "PULSE(0 5 1u 1u ...)" -> vector of numbers
std::vector<double> ExtractParenthesizedValues(std::string_view token) {
    const std::size_t open = token.find('(');
    const std::size_t close = token.rfind(')');
    std::vector<double> vals;
    if (open == std::string::npos || close == std::string::npos || close <= open) {
        return vals;
    }
    const std::string inner(token.substr(open + 1, close - open - 1));
    const auto toks = SplitTokens(inner);
    for (const auto &t : toks) {
        if (const auto num = ParseSpiceNumber(t)) {
            vals.push_back(*num);
        }
    }
    return vals;
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
    rest = TrimSpaces(rest);
    if (rest.size() < 2
        || (std::toupper(static_cast<unsigned char>(rest.front())) != 'V'
            && std::toupper(static_cast<unsigned char>(rest.front())) != 'I')) {
        return std::nullopt;
    }
    const bool is_voltage = std::toupper(static_cast<unsigned char>(rest.front())) == 'V';
    rest = TrimSpaces(rest.substr(1));
    if (rest.empty() || rest.front() != '=') {
        return std::nullopt;
    }
    rest = TrimSpaces(rest.substr(1));
    if (rest.empty()) {
        return std::nullopt;
    }
    return BehavioralLine{.IsVoltage = is_voltage, .Expression = std::string(rest)};
}

} // namespace

std::expected<ParsedNetlist, std::string> ParseNetlist(std::string_view netlist_text) {
    std::expected<Deck, std::string> deck = ReadDeck(netlist_text);
    if (!deck) {
        return std::unexpected(deck.error());
    }
    ParsedNetlist result;
    result.Title = deck->Title;
    result.Diagnostics = std::move(deck->Diagnostics);
    const std::vector<DeckLine> &circuit_lines = deck->CircuitLines;
    const std::vector<std::string> &definition_lines = deck->DefinitionLines;
    const std::map<std::string, ParsedModel> &models = deck->Models;

    // 4. Parse components and analyses using CircuitBuilder
    CircuitBuilder builder;
    NetlistNodes nodes;

    // Pre-scan all circuit_lines to discover and register all circuit nodes
    for (const DeckLine &deck_line : circuit_lines) {
        const std::vector<std::string> &tokens = deck_line.Tokens;
        const std::string &line = deck_line.Text;
        const std::string &name = tokens[0];
        if (name.front() == '.') {
            for (const std::string &node_name : ListDirectiveNodes(deck_line)) {
                nodes.Register(node_name);
            }
            continue;
        }

        const char first_char = GetElementLetter(name);
        if (first_char == '\0') {
            continue;
        }

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
            if (const std::optional<InstanceLine> instance = SplitInstanceLine(tokens)) {
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
    DeckContext context{.Result = result,
                        .Builder = builder,
                        .Nodes = nodes,
                        .Lines = *deck,
                        .BlockSources = {},
                        .Settings = {}};
    std::vector<BlockSource> &block_sources = context.BlockSources;
    const InstanceObservers observers{
        .OnInternalNode = [&nodes](const std::string &name, const NodeId node) { nodes.Bind(name, node); },
        .OnSource = [&block_sources](const std::string &name,
                                     const NodeId positive,
                                     const NodeId negative) { block_sources.push_back({name, positive, negative}); }};
    for (const DeckLine &deck_line : circuit_lines) {
        const std::string &line = deck_line.Text;
        if (std::toupper(static_cast<unsigned char>(deck_line.Tokens.front().front())) != 'X') {
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

    for (const DeckLine &deck_line : circuit_lines) {
        const std::vector<std::string> &tokens = deck_line.Tokens;
        const std::string &line = deck_line.Text;
        const std::string name = tokens[0];

        if (name.front() == '.') {
            if (auto read = ReadDirective(context, deck_line); !read) {
                return std::unexpected(read.error());
            }
            continue;
        }

        const char first_char = GetElementLetter(name);
        if (first_char == '\0') {
            continue;
        }

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

    ApplyDeckSettings(context.Settings, builder, result.Analyses);
    auto circuit_res = builder.Build();
    if (!circuit_res) {
        return std::unexpected(circuit_res.error());
    }
    result.CircuitInstance = std::move(*circuit_res);

    // If no analysis was specified, default to .OP
    if (result.Analyses.empty()) {
        result.Analyses.push_back({AnalysisKind::OPERATING_POINT, std::monostate{}});
    }

    return result;
}

} // namespace Nodalys
