/**
 * @file    netlist_element_devices.cpp
 * @brief   Reads semiconductor, switch, and logic gate element lines of a SPICE netlist.
 */

#include "parser/netlist/netlist_element_devices.h"

#include "text/spice_number.h"
#include "text/text_case.h"

#include <algorithm>
#include <array>
#include <format>
#include <utility>

namespace Nodalys {

namespace {

/// Where the nodes of an element start, after its name.
constexpr std::size_t FIRST_NODE = 1;

/**
 * @struct  GateKind
 * @brief   The kind of gate of a U line and where its word stands, after the output and the inputs.
 */
struct GateKind {
    std::size_t Index;
    LogicGateType Type;
};

/**
 * @struct  GateOption
 * @brief   A NAME=VALUE word of a U line and the parameter of the gate it sets.
 */
struct GateOption {
    std::string_view Key;
    double SmoothLogicParameters::*Target;
};

/**
 * @struct  GateWord
 * @brief   A word of a U line that names the kind of gate.
 */
struct GateWord {
    std::string_view Name;
    LogicGateType Type;
};

constexpr auto GATE_OPTIONS = std::to_array<GateOption>({
    {"VHIGH", &SmoothLogicParameters::VHigh},
    {"VLOW", &SmoothLogicParameters::VLow},
    {"VTH", &SmoothLogicParameters::VThreshold},
    {"TPD", &SmoothLogicParameters::PropagationDelay},
    {"ROUT", &SmoothLogicParameters::OutputResistance},
    {"HYST", &SmoothLogicParameters::Hysteresis},
});

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

/**
 * @brief   The model an element names, by the name it writes, in any case.
 * @return  The card, or null when the netlist has none of that name, which leaves the device at its defaults.
 */
const ParsedModel *FindModel(const DeckContext &context, const std::string &name) {
    const auto found = context.Lines.Models.find(ToUpper(name));
    return found == context.Lines.Models.end() ? nullptr : &found->second;
}

/**
 * @brief   The device parameters a line takes from the model it names, and a diagnostic, once for each model, that
 *          names the parameters of its card that the device does not read.
 */
template <typename T>
T ReadModelParameters(DeckContext &context,
                      const std::string &name,
                      BuiltParameters<T> (*const build)(const ParsedModel &model)) {
    const ParsedModel *model = FindModel(context, name);
    if (model == nullptr) {
        return T{};
    }
    BuiltParameters<T> built = build(*model);
    if (const std::string ignored = ListIgnoredParameters(*model, built.ReadKeys); !ignored.empty()) {
        AddUniqueDiagnostic(
            context.Result.Diagnostics,
            std::format("Nodalys does not read the parameters {} of the model {}", ignored, ToUpper(name)));
    }
    return std::move(built.Parameters);
}

/**
 * @brief   The area of a device, written after its model, or 1 when the line gives none.
 */
double ReadArea(const std::vector<std::string> &tokens, const std::size_t index) {
    return index < tokens.size() ? ParseSpiceNumber(tokens[index]).value_or(1.0) : 1.0;
}

std::optional<LogicGateType> FindLogicGateType(const std::string_view word) {
    const std::string upper = ToUpper(word);
    const auto found = std::ranges::find(GATE_WORDS, upper, &GateWord::Name);
    return found == GATE_WORDS.end() ? std::nullopt : std::optional(found->Type);
}

std::optional<GateKind> FindGateKind(const std::vector<std::string> &tokens) {
    constexpr std::size_t FIRST_INPUT = 2;
    for (std::size_t index = FIRST_INPUT; index < tokens.size(); ++index) {
        if (const std::optional<LogicGateType> type = FindLogicGateType(tokens[index])) {
            return GateKind{.Index = index, .Type = *type};
        }
    }
    return std::nullopt;
}

} // namespace

/**
 * @brief   Finds the value of a `NAME=VALUE` word among `tokens` from `first` onward, such as `IC=2` or `W=10u`.
 * @param[in] tokens  Tokenized element statement.
 * @param[in] first   Index of the first option token to inspect.
 * @param[in] key     Upper-case option key to match.
 * @return  Last parsed numeric value matching `key`, or empty if absent.
 */
std::optional<double>
FindElementOptionValue(const std::vector<std::string> &tokens, const std::size_t first, const std::string_view key) {
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
 * @brief   Parses `D<name> <anode> <cathode> <model> [area]`.
 * @param[in,out] context  Parser deck context receiving the diode.
 * @param[in]     line     Tokenized diode statement.
 * @return  Empty expected on success, or a parse error message.
 */
std::expected<void, std::string> ReadDiode(DeckContext &context, const DeckLine &line) {
    const std::vector<std::string> &tokens = line.Tokens;
    if (tokens.size() < 4) {
        return std::unexpected(std::format("Malformed diode: '{}'", line.Text));
    }
    context.Builder.AddDiode(tokens.front(),
                             context.Nodes.Resolve(tokens[1]),
                             context.Nodes.Resolve(tokens[2]),
                             ReadModelParameters(context, tokens[3], BuildDiodeParameters),
                             ReadArea(tokens, 4));
    return {};
}

/**
 * @brief   Parses `Q<name> <c> <b> <e> <model> [area]`; a transistor whose model is not found is an NPN at the
 *          defaults.
 * @param[in,out] context  Parser deck context receiving the BJT.
 * @param[in]     line     Tokenized BJT statement.
 * @return  Empty expected on success, or a parse error message.
 */
std::expected<void, std::string> ReadBjt(DeckContext &context, const DeckLine &line) {
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
                           ReadModelParameters(context, tokens[4], BuildBjtParameters),
                           ReadArea(tokens, 5));
    return {};
}

/**
 * @brief   Parses `M<name> <d> <g> <s> <b> <model> [L=<length>] [W=<width>]`; one whose model is not found is an NMOS.
 * @param[in,out] context  Parser deck context receiving the MOSFET.
 * @param[in]     line     Tokenized MOSFET statement.
 * @return  Empty expected on success, or a parse error message.
 */
std::expected<void, std::string> ReadMosfet(DeckContext &context, const DeckLine &line) {
    constexpr std::size_t FIRST_OPTION = 6;
    const std::vector<std::string> &tokens = line.Tokens;
    if (tokens.size() < FIRST_OPTION) {
        return std::unexpected(std::format("Malformed MOSFET: '{}'", line.Text));
    }
    const ParsedModel *model = FindModel(context, tokens[5]);
    MosfetParameters parameters = ReadModelParameters(context, tokens[5], BuildMosfetParameters);
    parameters.Length = FindElementOptionValue(tokens, FIRST_OPTION, "L").value_or(parameters.Length);
    parameters.Width = FindElementOptionValue(tokens, FIRST_OPTION, "W").value_or(parameters.Width);
    const NetlistNodes &nodes = context.Nodes;
    context.Builder.AddMosfet(tokens.front(),
                              nodes.Resolve(tokens[1]),
                              nodes.Resolve(tokens[2]),
                              nodes.Resolve(tokens[3]),
                              model == nullptr || model->Kind != ParsedModel::ModelKind::MOSFET_PMOS,
                              parameters);
    return {};
}

/**
 * @brief   Parses `S<name> <a1> <a2> <c1> <c2> <model>`.
 * @param[in,out] context  Parser deck context receiving the switch.
 * @param[in]     line     Tokenized switch statement.
 * @return  Empty expected on success, or a parse error message.
 */
std::expected<void, std::string> ReadSwitch(DeckContext &context, const DeckLine &line) {
    const std::vector<std::string> &tokens = line.Tokens;
    if (tokens.size() < 6) {
        return std::unexpected(std::format("Malformed switch: '{}'", line.Text));
    }
    const NetlistNodes &nodes = context.Nodes;
    context.Builder.AddSwitch(tokens.front(),
                              nodes.Resolve(tokens[1]),
                              nodes.Resolve(tokens[2]),
                              nodes.Resolve(tokens[3]),
                              nodes.Resolve(tokens[4]),
                              ReadModelParameters(context, tokens[5], BuildSwitchParameters));
    return {};
}

/**
 * @brief   Lists the nodes of a `U` line: every token before the gate kind and the first `NAME=VALUE` option.
 * @param[in] line  Tokenized logic-gate statement.
 * @return  List of node names referenced by the gate.
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

/**
 * @brief   Parses `U<name> <out> <in1> [<in2> ...] <kind> [VHIGH=5] [VLOW=0] [VTH=2.5] [TPD=1n] [ROUT=50] [HYST=0.5]`.
 * @param[in,out] context  Parser deck context receiving the logic gate.
 * @param[in]     line     Tokenized logic-gate statement.
 * @return  Empty expected on success, or a parse error message.
 */
std::expected<void, std::string> ReadLogicGate(DeckContext &context, const DeckLine &line) {
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
            FindElementOptionValue(tokens, kind->Index + 1, option.Key).value_or(parameters.*(option.Target));
    }
    context.Builder.AddLogicGate(tokens.front(),
                                 kind->Type,
                                 context.Nodes.Resolve(tokens[1]),
                                 std::move(inputs),
                                 parameters);
    return {};
}

} // namespace Nodalys
