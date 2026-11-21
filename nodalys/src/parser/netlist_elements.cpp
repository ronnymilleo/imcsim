/**
 * @file    netlist_elements.cpp
 * @brief   Reads the element lines of a netlist, such as R1 or Q2, into the circuit being built.
 */

#include "parser/netlist_elements.h"

#include "nodalys/expression.h"
#include "parser/expression_references.h"
#include "parser/source_waveforms.h"
#include "parser/spice_number.h"
#include "parser/spice_text.h"
#include "parser/subcircuit_lines.h"
#include "text/text_case.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <format>
#include <optional>
#include <string_view>
#include <utility>

namespace Nodalys {

namespace {

/// Where the nodes of an element start, after its name.
constexpr std::size_t FIRST_NODE = 1;
/// The reference impedance of a port that does not give one.
constexpr double DEFAULT_PORT_OHMS = 50.0;

using ElementResult = std::expected<void, std::string>;

/**
 * @brief   The value of a NAME=VALUE word among the tokens from first on, such as IC=2 or W=10u.
 * @param[in] tokens  Tokens of the line.
 * @param[in] first   Index of the first token that may hold the word.
 * @param[in] key     The name, in upper case.
 * @return  The value of the last such word whose value is a number, or none.
 */
std::optional<double>
FindOptionValue(const std::vector<std::string> &tokens, const std::size_t first, const std::string_view key) {
    std::optional<double> value;
    for (std::size_t index = first; index < tokens.size(); ++index) {
        const std::string upper = ToUpper(tokens[index]);
        if (upper.size() > key.size() && upper.starts_with(key) && upper[key.size()] == '=') {
            if (const std::optional<double> number = ParseSpiceNumber(std::string_view(upper).substr(key.size() + 1))) {
                value = number;
            }
        }
    }
    return value;
}

/**
 * @brief   The numbers inside the parentheses of a waveform word such as "PULSE(0 5 1u 1u)".
 * @details A word that is not a number is left out, so the waveform takes its default for it.
 */
std::vector<double> ReadParenthesizedValues(const std::string_view token) {
    const std::size_t open = token.find('(');
    const std::size_t close = token.rfind(')');
    std::vector<double> values;
    if (open == std::string_view::npos || close == std::string_view::npos || close <= open) {
        return values;
    }
    for (const std::string &word : SplitTokens(token.substr(open + 1, close - open - 1))) {
        if (const std::optional<double> number = ParseSpiceNumber(word)) {
            values.push_back(*number);
        }
    }
    return values;
}

/**
 * @brief   The model an element names, by the name it writes, in any case.
 * @return  The card, or null when the netlist has none of that name, which leaves the device at its defaults.
 */
const ParsedModel *FindModel(const DeckContext &context, const std::string &name) {
    const auto found = context.Lines.Models.find(ToUpper(name));
    return found == context.Lines.Models.end() ? nullptr : &found->second;
}

/**
 * @brief   The number in the name of a port, such as 2 for "V_PORT2", or the fallback when it has none.
 */
int GetPortNumberFromName(const std::string &name, const int fallback) {
    const std::size_t digits = name.find_first_of("0123456789");
    return digits == std::string::npos ? fallback : std::atoi(name.c_str() + digits);
}

// Logic gates

/**
 * @struct  GateWord
 * @brief   A word of a U line that names the kind of gate.
 */
struct GateWord {
    std::string_view Name;
    LogicGateType Type;
};

constexpr auto GATE_WORDS = std::to_array<GateWord>({
    {"NOT", LogicGateType::NOT},
    {"INV", LogicGateType::NOT},
    {"BUF", LogicGateType::BUFFER},
    {"BUFFER", LogicGateType::BUFFER},
    {"AND", LogicGateType::AND},
    {"NAND", LogicGateType::NAND},
    {"OR", LogicGateType::OR},
    {"NOR", LogicGateType::NOR},
    {"XOR", LogicGateType::XOR},
    {"XNOR", LogicGateType::XNOR},
    {"SCHMITT", LogicGateType::SCHMITT_TRIGGER},
});

std::optional<LogicGateType> FindLogicGateType(const std::string_view word) {
    const std::string upper = ToUpper(word);
    const auto found = std::ranges::find(GATE_WORDS, upper, &GateWord::Name);
    return found == GATE_WORDS.end() ? std::nullopt : std::optional(found->Type);
}

/**
 * @struct  GateOption
 * @brief   A NAME=VALUE word of a U line and the parameter of the gate it sets.
 */
struct GateOption {
    std::string_view Key;
    double SmoothLogicParameters::*Target;
};

constexpr auto GATE_OPTIONS = std::to_array<GateOption>({
    {"VHIGH", &SmoothLogicParameters::VHigh},
    {"VLOW", &SmoothLogicParameters::VLow},
    {"VTH", &SmoothLogicParameters::VThreshold},
    {"TPD", &SmoothLogicParameters::PropagationDelay},
    {"ROUT", &SmoothLogicParameters::OutputResistance},
    {"HYST", &SmoothLogicParameters::Hysteresis},
});

/**
 * @struct  GateKind
 * @brief   The kind of gate of a U line and where its word stands, after the output and the inputs.
 */
struct GateKind {
    std::size_t Index;
    LogicGateType Type;
};

std::optional<GateKind> FindGateKind(const std::vector<std::string> &tokens) {
    constexpr std::size_t FIRST_INPUT = 2;
    for (std::size_t index = FIRST_INPUT; index < tokens.size(); ++index) {
        if (const std::optional<LogicGateType> type = FindLogicGateType(tokens[index])) {
            return GateKind{.Index = index, .Type = *type};
        }
    }
    return std::nullopt;
}

// Behavioral sources

/**
 * @struct  BehavioralLine
 * @brief   The kind and the text of the expression of a B element line, "B1 n+ n- V=<expression>".
 * @details The text is taken from the line itself, since the tokens break an expression at its spaces and parentheses.
 */
struct BehavioralLine {
    bool IsVoltage = true;
    std::string Expression;
};

std::optional<BehavioralLine> SplitBehavioralLine(const std::string_view line) {
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
    std::string_view rest = TrimSpaces(line.substr(position));
    const char kind = rest.empty() ? '\0' : static_cast<char>(std::toupper(static_cast<unsigned char>(rest.front())));
    if (rest.size() < 2 || (kind != 'V' && kind != 'I')) {
        return std::nullopt;
    }
    rest = TrimSpaces(rest.substr(1));
    if (rest.empty() || rest.front() != '=') {
        return std::nullopt;
    }
    rest = TrimSpaces(rest.substr(1));
    if (rest.empty()) {
        return std::nullopt;
    }
    return BehavioralLine{.IsVoltage = kind == 'V', .Expression = std::string(rest)};
}

// Passive parts

/**
 * @struct  TwoTerminalValue
 * @brief   The nodes and the value of a resistor, capacitor or inductor line, "<name> <n1> <n2> <value> ...".
 */
struct TwoTerminalValue {
    NodeId First;
    NodeId Second;
    double Value;
};

std::expected<TwoTerminalValue, std::string>
ReadTwoTerminalValue(const DeckContext &context, const DeckLine &line, const std::string_view kind) {
    constexpr std::size_t VALUE_INDEX = 3;
    const std::vector<std::string> &tokens = line.Tokens;
    if (tokens.size() <= VALUE_INDEX) {
        return std::unexpected(std::format("Malformed {}: '{}'", kind, line.Text));
    }
    const std::optional<double> value = ParseSpiceNumber(tokens[VALUE_INDEX]);
    if (!value) {
        return std::unexpected(std::format("Invalid {} value '{}'", kind, tokens[VALUE_INDEX]));
    }
    return TwoTerminalValue{.First = context.Nodes.Resolve(tokens[1]),
                            .Second = context.Nodes.Resolve(tokens[2]),
                            .Value = *value};
}

ElementResult ReadResistor(DeckContext &context, const DeckLine &line) {
    const auto part = ReadTwoTerminalValue(context, line, "resistor");
    if (!part) {
        return std::unexpected(part.error());
    }
    context.Builder.AddResistor(line.Tokens.front(), part->First, part->Second, part->Value);
    return {};
}

/**
 * @brief   C<name> <n1> <n2> <value> [IC=<volts>].
 */
ElementResult ReadCapacitor(DeckContext &context, const DeckLine &line) {
    const auto part = ReadTwoTerminalValue(context, line, "capacitor");
    if (!part) {
        return std::unexpected(part.error());
    }
    context.Builder.AddCapacitor(line.Tokens.front(),
                                 part->First,
                                 part->Second,
                                 part->Value,
                                 FindOptionValue(line.Tokens, 4, "IC").value_or(0.0));
    return {};
}

/**
 * @brief   L<name> <n1> <n2> <value> [IC=<amps>].
 */
ElementResult ReadInductor(DeckContext &context, const DeckLine &line) {
    const auto part = ReadTwoTerminalValue(context, line, "inductor");
    if (!part) {
        return std::unexpected(part.error());
    }
    context.Builder.AddInductor(line.Tokens.front(),
                                part->First,
                                part->Second,
                                part->Value,
                                FindOptionValue(line.Tokens, 4, "IC").value_or(0.0));
    return {};
}

// Independent sources and ports

/**
 * @struct  PortWords
 * @brief   The port options of a V, I or P line: PORTNUM and Z0, written NAME=VALUE or NAME VALUE.
 */
struct PortWords {
    std::optional<int> Number;
    std::optional<double> Ohms;
};

/**
 * @brief   Reads a port option at a token, which a V or I line also takes to become an S-parameter port.
 * @return  How many tokens the option takes, 1 or 2, or 0 when the token is no port option.
 */
std::size_t ReadPortWord(const std::vector<std::string> &tokens, const std::size_t index, PortWords &port) {
    const std::string upper = ToUpper(tokens[index]);
    const bool has_next = index + 1 < tokens.size();
    for (const std::string_view key : {std::string_view("PORTNUM"), std::string_view("Z0")}) {
        std::optional<double> value;
        std::size_t used = 0;
        if (upper.size() > key.size() && upper.starts_with(key) && upper[key.size()] == '=') {
            value = ParseSpiceNumber(std::string_view(upper).substr(key.size() + 1));
            used = 1;
        } else if (upper == key && has_next) {
            value = ParseSpiceNumber(tokens[index + 1]);
            used = 2;
        }
        if (used == 0) {
            continue;
        }
        if (key == "Z0") {
            port.Ohms = value.value_or(DEFAULT_PORT_OHMS);
        } else {
            port.Number = static_cast<int>(value.value_or(1.0));
        }
        return used;
    }
    return 0;
}

/**
 * @struct  SourceWords
 * @brief   What a V or I line gives after its nodes.
 */
struct SourceWords {
    SourceLevels Levels;
    SourceWaveform Waveform = SourceWaveform::NONE;
    std::string WaveformToken;
    PortWords Port;
    /// Whether a port option made the line an S-parameter port.
    bool IsPort = false;
};

/**
 * @brief   Reads "[DC <value>] [AC <magnitude> [<phase>]] [<waveform>(...)] [<value>]" and the port options.
 */
SourceWords ReadSourceWords(const std::vector<std::string> &tokens) {
    constexpr std::size_t FIRST_AFTER_NODES = 3;
    SourceWords words;
    std::size_t index = FIRST_AFTER_NODES;
    while (index < tokens.size()) {
        if (const std::size_t used = ReadPortWord(tokens, index, words.Port); used > 0) {
            words.IsPort = true;
            index += used;
            continue;
        }
        const std::string upper = ToUpper(tokens[index]);
        const bool has_next = index + 1 < tokens.size();
        if (upper == "DC" && has_next) {
            words.Levels.Dc = ParseSpiceNumber(tokens[index + 1]).value_or(0.0);
            index += 2;
            continue;
        }
        if (upper == "AC" && has_next) {
            words.Levels.AcMagnitude = ParseSpiceNumber(tokens[index + 1]).value_or(1.0);
            index += 2;
            if (index < tokens.size()) {
                if (const std::optional<double> phase = ParseSpiceNumber(tokens[index])) {
                    words.Levels.AcPhaseDegrees = *phase;
                    ++index;
                }
            }
            continue;
        }
        if (const SourceWaveform waveform = FindSourceWaveform(tokens[index]); waveform != SourceWaveform::NONE) {
            words.Waveform = waveform;
            words.WaveformToken = tokens[index];
        } else if (const std::optional<double> value = ParseSpiceNumber(tokens[index])) {
            words.Levels.Dc = *value;
        }
        ++index;
    }
    return words;
}

/**
 * @brief   V or I lines: a source with its DC value, its AC magnitude and phase and its waveform, or an S-parameter
 *          port when it gives PORTNUM or Z0.
 */
ElementResult ReadSource(DeckContext &context, const DeckLine &line) {
    const std::vector<std::string> &tokens = line.Tokens;
    if (tokens.size() < 3) {
        return std::unexpected(std::format("Malformed source: '{}'", line.Text));
    }
    const std::string &name = tokens.front();
    const NodeId positive = context.Nodes.Resolve(tokens[1]);
    const SourceWords words = ReadSourceWords(tokens);
    if (words.IsPort) {
        const int number = words.Port.Number.value_or(0);
        context.Builder.AddPort(number > 0 ? number : GetPortNumberFromName(name, 1),
                                positive,
                                words.Port.Ohms.value_or(DEFAULT_PORT_OHMS));
        return {};
    }
    AddWaveformSource(context.Builder,
                      name,
                      GetElementLetter(name) == 'V',
                      positive,
                      context.Nodes.Resolve(tokens[2]),
                      words.Waveform,
                      ReadParenthesizedValues(words.WaveformToken),
                      words.Levels);
    return {};
}

/**
 * @brief   P<name> <pos> <neg> [PORTNUM=<n>] [Z0=<ohms>], or with the impedance alone; the number defaults to the one
 *          in the name.
 */
ElementResult ReadPort(DeckContext &context, const DeckLine &line) {
    const std::vector<std::string> &tokens = line.Tokens;
    if (tokens.size() < 3) {
        return std::unexpected(std::format("Malformed port: '{}'", line.Text));
    }
    PortWords port;
    for (std::size_t index = 3; index < tokens.size();) {
        if (const std::size_t used = ReadPortWord(tokens, index, port); used > 0) {
            index += used;
            continue;
        }
        if (const std::optional<double> ohms = ParseSpiceNumber(tokens[index])) {
            port.Ohms = ohms;
        }
        ++index;
    }
    context.Builder.AddPort(port.Number.value_or(GetPortNumberFromName(tokens.front(), 1)),
                            context.Nodes.Resolve(tokens[1]),
                            port.Ohms.value_or(DEFAULT_PORT_OHMS));
    return {};
}

// Controlled sources

using VoltageControlledAdder = void (CircuitBuilder::*)(std::string, NodeId, NodeId, NodeId, NodeId, double);
using CurrentControlledAdder = void (CircuitBuilder::*)(std::string, NodeId, NodeId, const std::string &, double);

/**
 * @brief   E or G lines: <name> <pos> <neg> <ctrl_pos> <ctrl_neg> <value>.
 */
ElementResult ReadVoltageControlled(DeckContext &context,
                                    const DeckLine &line,
                                    const std::string_view kind,
                                    const std::string_view quantity,
                                    const VoltageControlledAdder add) {
    constexpr std::size_t VALUE_INDEX = 5;
    const std::vector<std::string> &tokens = line.Tokens;
    if (tokens.size() <= VALUE_INDEX) {
        return std::unexpected(std::format("Malformed {}: '{}'", kind, line.Text));
    }
    const std::optional<double> value = ParseSpiceNumber(tokens[VALUE_INDEX]);
    if (!value) {
        return std::unexpected(std::format("Invalid {} {} '{}'", kind, quantity, tokens[VALUE_INDEX]));
    }
    const NetlistNodes &nodes = context.Nodes;
    (context.Builder.*add)(tokens.front(),
                           nodes.Resolve(tokens[1]),
                           nodes.Resolve(tokens[2]),
                           nodes.Resolve(tokens[3]),
                           nodes.Resolve(tokens[4]),
                           *value);
    return {};
}

/**
 * @brief   H or F lines: <name> <pos> <neg> <control source> <value>.
 */
ElementResult ReadCurrentControlled(DeckContext &context,
                                    const DeckLine &line,
                                    const std::string_view kind,
                                    const std::string_view quantity,
                                    const CurrentControlledAdder add) {
    constexpr std::size_t VALUE_INDEX = 4;
    const std::vector<std::string> &tokens = line.Tokens;
    if (tokens.size() <= VALUE_INDEX) {
        return std::unexpected(std::format("Malformed {}: '{}'", kind, line.Text));
    }
    const std::optional<double> value = ParseSpiceNumber(tokens[VALUE_INDEX]);
    if (!value) {
        return std::unexpected(std::format("Invalid {} {} '{}'", kind, quantity, tokens[VALUE_INDEX]));
    }
    (context.Builder
     .*add)(tokens.front(), context.Nodes.Resolve(tokens[1]), context.Nodes.Resolve(tokens[2]), tokens[3], *value);
    return {};
}

ElementResult ReadVcvs(DeckContext &context, const DeckLine &line) {
    return ReadVoltageControlled(context, line, "VCVS", "gain", &CircuitBuilder::AddVcvs);
}

ElementResult ReadVccs(DeckContext &context, const DeckLine &line) {
    return ReadVoltageControlled(context, line, "VCCS", "transconductance", &CircuitBuilder::AddVccs);
}

ElementResult ReadCcvs(DeckContext &context, const DeckLine &line) {
    return ReadCurrentControlled(context, line, "CCVS", "transresistance", &CircuitBuilder::AddCcvs);
}

ElementResult ReadCccs(DeckContext &context, const DeckLine &line) {
    return ReadCurrentControlled(context, line, "CCCS", "gain", &CircuitBuilder::AddCccs);
}

/**
 * @brief   B<name> <pos> <neg> V=<expression> or I=<expression>.
 * @details The builder reads nodes by number, so every v(...) is written with the numbers this netlist gave them;
 *          a current keeps the source it names.
 */
ElementResult ReadBehavioral(DeckContext &context, const DeckLine &line) {
    const std::string &name = line.Tokens.front();
    const std::optional<BehavioralLine> behavioral = SplitBehavioralLine(line.Text);
    if (line.Tokens.size() < 4 || !behavioral) {
        return std::unexpected(std::format("Malformed behavioral source: '{}'", line.Text));
    }
    const auto expression = Expression::Parse(behavioral->Expression);
    if (!expression) {
        return std::unexpected(std::format("{}: {}", name, expression.error()));
    }
    const auto resolved = RewriteExpressionReferences(
        behavioral->Expression,
        *expression,
        [&context](const std::string_view node) { return context.Nodes.Resolve(node); },
        [](const std::string_view source) -> std::expected<std::string, std::string> { return std::string(source); });
    if (!resolved) {
        return std::unexpected(resolved.error());
    }
    const NodeId positive = context.Nodes.Resolve(line.Tokens[1]);
    const NodeId negative = context.Nodes.Resolve(line.Tokens[2]);
    if (behavioral->IsVoltage) {
        context.Builder.AddBehavioralVoltage(name, positive, negative, *resolved);
    } else {
        context.Builder.AddBehavioralCurrent(name, positive, negative, *resolved);
    }
    return {};
}

// Semiconductors and switches

/**
 * @brief   The area of a device, written after its model, or 1 when the line gives none.
 */
double ReadArea(const std::vector<std::string> &tokens, const std::size_t index) {
    return index < tokens.size() ? ParseSpiceNumber(tokens[index]).value_or(1.0) : 1.0;
}

/**
 * @brief   D<name> <anode> <cathode> <model> [area].
 */
ElementResult ReadDiode(DeckContext &context, const DeckLine &line) {
    const std::vector<std::string> &tokens = line.Tokens;
    if (tokens.size() < 4) {
        return std::unexpected(std::format("Malformed diode: '{}'", line.Text));
    }
    const ParsedModel *model = FindModel(context, tokens[3]);
    context.Builder.AddDiode(tokens.front(),
                             context.Nodes.Resolve(tokens[1]),
                             context.Nodes.Resolve(tokens[2]),
                             model != nullptr ? BuildDiodeParameters(*model) : DiodeParameters{},
                             ReadArea(tokens, 4));
    return {};
}

/**
 * @brief   Q<name> <c> <b> <e> <model> [area]; a transistor whose model is not found is an NPN at the defaults.
 */
ElementResult ReadBjt(DeckContext &context, const DeckLine &line) {
    const std::vector<std::string> &tokens = line.Tokens;
    if (tokens.size() < 5) {
        return std::unexpected(std::format("Malformed BJT: '{}'", line.Text));
    }
    const ParsedModel *model = FindModel(context, tokens[4]);
    const NetlistNodes &nodes = context.Nodes;
    context.Builder.AddBjt(tokens.front(),
                           nodes.Resolve(tokens[1]),
                           nodes.Resolve(tokens[2]),
                           nodes.Resolve(tokens[3]),
                           model == nullptr || model->Kind != ParsedModel::ModelKind::BJT_PNP,
                           model != nullptr ? BuildBjtParameters(*model) : BjtParameters{},
                           ReadArea(tokens, 5));
    return {};
}

/**
 * @brief   M<name> <d> <g> <s> <b> <model> [L=<length>] [W=<width>]; one whose model is not found is an NMOS.
 */
ElementResult ReadMosfet(DeckContext &context, const DeckLine &line) {
    constexpr std::size_t FIRST_OPTION = 6;
    const std::vector<std::string> &tokens = line.Tokens;
    if (tokens.size() < FIRST_OPTION) {
        return std::unexpected(std::format("Malformed MOSFET: '{}'", line.Text));
    }
    const ParsedModel *model = FindModel(context, tokens[5]);
    MosfetParameters parameters = model != nullptr ? BuildMosfetParameters(*model) : MosfetParameters{};
    parameters.Length = FindOptionValue(tokens, FIRST_OPTION, "L").value_or(parameters.Length);
    parameters.Width = FindOptionValue(tokens, FIRST_OPTION, "W").value_or(parameters.Width);
    const NetlistNodes &nodes = context.Nodes;
    context.Builder.AddMosfet(tokens.front(),
                              nodes.Resolve(tokens[1]),
                              nodes.Resolve(tokens[2]),
                              nodes.Resolve(tokens[3]),
                              nodes.Resolve(tokens[4]),
                              model == nullptr || model->Kind != ParsedModel::ModelKind::MOSFET_PMOS,
                              parameters);
    return {};
}

/**
 * @brief   S<name> <a1> <a2> <c1> <c2> <model>.
 */
ElementResult ReadSwitch(DeckContext &context, const DeckLine &line) {
    const std::vector<std::string> &tokens = line.Tokens;
    if (tokens.size() < 6) {
        return std::unexpected(std::format("Malformed switch: '{}'", line.Text));
    }
    const ParsedModel *model = FindModel(context, tokens[5]);
    const NetlistNodes &nodes = context.Nodes;
    context.Builder.AddVoltageControlledSwitch(tokens.front(),
                                               nodes.Resolve(tokens[1]),
                                               nodes.Resolve(tokens[2]),
                                               nodes.Resolve(tokens[3]),
                                               nodes.Resolve(tokens[4]),
                                               model != nullptr ? BuildSwitchParameters(*model) : SwitchParameters{});
    return {};
}

/**
 * @brief   U<name> <out> <in1> [<in2> ...] <kind> [VHIGH=5] [VLOW=0] [VTH=2.5] [TPD=1n] [ROUT=50] [HYST=0.5].
 */
ElementResult ReadLogicGate(DeckContext &context, const DeckLine &line) {
    const std::vector<std::string> &tokens = line.Tokens;
    if (tokens.size() < 4) {
        return std::unexpected(std::format("Malformed logic gate: '{}'", line.Text));
    }
    const std::optional<GateKind> kind = FindGateKind(tokens);
    if (!kind) {
        return std::unexpected(std::format("Missing or unrecognized logic gate type in: '{}'", line.Text));
    }
    std::vector<NodeId> inputs;
    for (std::size_t index = 2; index < kind->Index; ++index) {
        inputs.push_back(context.Nodes.Resolve(tokens[index]));
    }
    if (inputs.empty()) {
        return std::unexpected(std::format("Logic gate '{}' has no inputs", tokens.front()));
    }
    SmoothLogicParameters parameters;
    for (const GateOption &option : GATE_OPTIONS) {
        parameters.*(option.Target) =
            FindOptionValue(tokens, kind->Index + 1, option.Key).value_or(parameters.*(option.Target));
    }
    context.Builder.AddLogicGate(tokens.front(),
                                 kind->Type,
                                 context.Nodes.Resolve(tokens[1]),
                                 std::move(inputs),
                                 parameters);
    return {};
}

/**
 * @brief   X lines are placed before the other lines are read, since the directives may name their nodes.
 */
ElementResult SkipInstance(DeckContext & /*context*/, const DeckLine & /*line*/) {
    return {};
}

// Nodes of each kind of element

/**
 * @brief   The first count node tokens of a line, or none when the line is too short to have them.
 */
std::vector<std::string> ListFirstNodes(const std::vector<std::string> &tokens, const std::size_t count) {
    if (tokens.size() <= count) {
        return {};
    }
    return {tokens.begin() + FIRST_NODE, tokens.begin() + static_cast<std::ptrdiff_t>(FIRST_NODE + count)};
}

/**
 * @brief   The nodes of a B line, and those its expression reads, which are circuit nodes even when no other element
 *          touches them.
 */
std::vector<std::string> ListBehavioralNodes(const DeckLine &line) {
    std::vector<std::string> nodes = ListFirstNodes(line.Tokens, 2);
    const std::optional<BehavioralLine> behavioral = SplitBehavioralLine(line.Text);
    if (!behavioral) {
        return nodes;
    }
    if (const auto expression = Expression::Parse(behavioral->Expression)) {
        for (const ExpressionInput &input : expression->GetInputs()) {
            if (input.Type == ExpressionInput::Kind::VOLTAGE) {
                nodes.push_back(input.First);
                nodes.push_back(input.Second);
            }
        }
    }
    return nodes;
}

/**
 * @brief   The nodes of a U line: every token before the kind of gate and the first NAME=VALUE.
 */
std::vector<std::string> ListGateNodes(const DeckLine &line) {
    const std::vector<std::string> &tokens = line.Tokens;
    std::vector<std::string> nodes;
    for (std::size_t index = FIRST_NODE; index < tokens.size(); ++index) {
        if (tokens[index].find('=') != std::string::npos || FindLogicGateType(tokens[index])) {
            break;
        }
        nodes.push_back(tokens[index]);
    }
    return nodes;
}

std::vector<std::string> ListTwoNodes(const DeckLine &line) {
    return ListFirstNodes(line.Tokens, 2);
}

std::vector<std::string> ListThreeNodes(const DeckLine &line) {
    return ListFirstNodes(line.Tokens, 3);
}

std::vector<std::string> ListFourNodes(const DeckLine &line) {
    return ListFirstNodes(line.Tokens, 4);
}

std::vector<std::string> ListInstanceNodes(const DeckLine &line) {
    const std::optional<InstanceLine> instance = SplitInstanceLine(line.Tokens);
    return instance ? instance->NodeNames : std::vector<std::string>{};
}

/**
 * @struct  ElementReader
 * @brief   What the engine knows of the elements of one letter: how to list the nodes of a line before they are
 *          numbered, and how to read it.
 */
struct ElementReader {
    char Letter;
    std::vector<std::string> (*ListNodes)(const DeckLine &);
    ElementResult (*Read)(DeckContext &, const DeckLine &);
};

constexpr auto ELEMENT_READERS = std::to_array<ElementReader>({
    {'R', ListTwoNodes, ReadResistor},
    {'C', ListTwoNodes, ReadCapacitor},
    {'L', ListTwoNodes, ReadInductor},
    {'V', ListTwoNodes, ReadSource},
    {'I', ListTwoNodes, ReadSource},
    {'P', ListTwoNodes, ReadPort},
    {'E', ListFourNodes, ReadVcvs},
    {'G', ListFourNodes, ReadVccs},
    {'H', ListTwoNodes, ReadCcvs},
    {'F', ListTwoNodes, ReadCccs},
    {'B', ListBehavioralNodes, ReadBehavioral},
    {'D', ListTwoNodes, ReadDiode},
    {'Q', ListThreeNodes, ReadBjt},
    {'M', ListFourNodes, ReadMosfet},
    {'S', ListFourNodes, ReadSwitch},
    {'U', ListGateNodes, ReadLogicGate},
    {'X', ListInstanceNodes, SkipInstance},
});

/**
 * @brief   The reader of the elements of a line, by the letter of its name.
 * @return  The reader, or null for a letter the engine has no element for.
 */
const ElementReader *FindElementReader(const DeckLine &line) {
    const auto reader =
        std::ranges::find(ELEMENT_READERS, GetElementLetter(line.Tokens.front()), &ElementReader::Letter);
    return reader == ELEMENT_READERS.end() ? nullptr : &*reader;
}

} // namespace

/**
 * @brief   The nodes an element line names, before the nodes are numbered.
 * @param[in] line  An element line.
 * @return  Its nodes as written; none for a kind of element the engine does not read.
 */
std::vector<std::string> ListElementNodes(const DeckLine &line) {
    const ElementReader *reader = FindElementReader(line);
    return reader == nullptr ? std::vector<std::string>{} : reader->ListNodes(line);
}

/**
 * @brief   Reads one element line into the circuit being built.
 * @param[in,out] context  The netlist being parsed.
 * @param[in]     line     A line that is not a directive.
 * @return  Nothing once read, also for a letter the engine has no element for, which is noted in the diagnostics;
 *          the reason the line cannot be read otherwise.
 */
std::expected<void, std::string> ReadElement(DeckContext &context, const DeckLine &line) {
    if (GetElementLetter(line.Tokens.front()) == '\0') {
        return {};
    }
    const ElementReader *reader = FindElementReader(line);
    if (reader == nullptr) {
        context.Result.Diagnostics.push_back({std::format("Ignored unrecognized device line: '{}'", line.Text)});
        return {};
    }
    return reader->Read(context, line);
}

} // namespace Nodalys
