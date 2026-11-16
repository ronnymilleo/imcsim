/**
 * @file    netlist_subcircuits.cpp
 * @brief   Places the subcircuits of a netlist: the X lines, over the scoped reader of subcircuits kept as text.
 */

#include "parser/netlist_subcircuits.h"

#include "nodalys/subcircuit_text.h"
#include "parser/spice_text.h"

#include <cstddef>
#include <format>

namespace Nodalys {

namespace {

/// The first node of an X line comes right after its name.
constexpr std::size_t FIRST_NODE_INDEX = 1;

/// Name of the port that stands for the node at a position of the X line, in the text that places the instance.
std::string PortName(const std::size_t position) {
    return std::format("PORT{}", position);
}

} // namespace

/**
 * @brief   Splits an X line into its nodes, the subcircuit it calls and its parameters.
 * @param[in] tokens  The tokens of the line, as SplitTokens() gives them.
 * @return  The parts, or none when the line is not written X<name> <nodes> <subcircuit>.
 */
std::optional<InstanceLine> SplitInstanceLine(const std::vector<std::string> &tokens) {
    std::size_t parameters_start = FIRST_NODE_INDEX + 1;
    while (parameters_start < tokens.size() && !IsParameterStart(tokens[parameters_start])) {
        ++parameters_start;
    }
    // At least the name of the subcircuit has to sit between the name of the instance and the parameters
    if (parameters_start <= FIRST_NODE_INDEX + 1) {
        return std::nullopt;
    }
    InstanceLine instance;
    const auto nodes_end = tokens.begin() + static_cast<std::ptrdiff_t>(parameters_start - 1);
    instance.NodeNames.assign(tokens.begin() + static_cast<std::ptrdiff_t>(FIRST_NODE_INDEX), nodes_end);
    instance.SubcircuitName = *nodes_end;
    instance.ParameterTokens.assign(tokens.begin() + static_cast<std::ptrdiff_t>(parameters_start), tokens.end());
    return instance;
}

/**
 * @brief   Adds the elements of one X line of a netlist to the circuit being built.
 * @details The line is placed by the reader of subcircuits kept as text: the definitions of the netlist (its .subckt
 *          blocks, .model cards and .param lines) become the text around the instance, so the instance sees them as a
 *          block sees those around its own definition, and the elements and internal nodes are named X1.R1, X1.MID,
 *          X1.XLOAD.R2... as the reader names them.
 * @param[in,out] builder           Circuit to add the elements to.
 * @param[in]     definition_lines  The logical lines of the .subckt blocks (with their .subckt and .ends lines),
 *                                  .model cards and .param lines of the netlist, in the order they were written.
 * @param[in]     line              The X line.
 * @param[in]     resolve_node      Gives the node of the circuit for a node name written in the netlist.
 * @param[in]     observers         Told the name of each internal node and each source the instance makes.
 * @return  The warnings, such as the parameters of a model that the engine does not read, or the reason the line cannot
 *          be placed. The builder may hold part of the elements after a failure and must be discarded.
 */
std::expected<std::vector<std::string>, std::string>
AddNetlistInstance(CircuitBuilder &builder, const std::vector<std::string> &definition_lines,
                   const std::string_view line, const std::function<NodeId(std::string_view)> &resolve_node,
                   const InstanceObservers &observers) {
    const std::vector<std::string> tokens = SplitTokens(line);
    const std::optional<InstanceLine> instance = SplitInstanceLine(tokens);
    if (!instance) {
        return std::unexpected(std::format("Malformed subcircuit call: '{}'", line));
    }

    SubcircuitText text{.Name = instance->SubcircuitName, .Ports = {}, .Body = {}};
    SubcircuitTextInstance placement{.Prefix = {},
                                     .PortNodes = {},
                                     .Parameters = {},
                                     .OnInternalNode = observers.OnInternalNode,
                                     .OnSource = observers.OnSource};
    std::string call = tokens.front();
    for (std::size_t position = 0; position < instance->NodeNames.size(); ++position) {
        text.Ports.push_back(PortName(position));
        placement.PortNodes.push_back(resolve_node(instance->NodeNames[position]));
        call += ' ' + PortName(position);
    }
    call += ' ' + instance->SubcircuitName;
    for (const std::string &parameter : instance->ParameterTokens) {
        call += ' ' + parameter;
    }
    for (const std::string &definition : definition_lines) {
        text.Body += definition + '\n';
    }
    text.Body += call;
    return AddSubcircuitText(builder, text, placement);
}

} // namespace Nodalys
