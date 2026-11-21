/**
 * @file    netlist_subcircuits.cpp
 * @brief   Places the subcircuits of a netlist: the X lines, over the scoped reader of subcircuits kept as text.
 */

#include "parser/netlist_subcircuits.h"

#include "parser/classified_lines.h"
#include "parser/subcircuit_expansion.h"
#include "parser/subcircuit_lines.h"
#include "parser/subcircuit_scope.h"
#include "text/text_case.h"

#include <format>
#include <utility>

namespace Nodalys {

/**
 * @brief   Adds the elements of one X line of a netlist to the circuit being built.
 * @details The line is expanded by the reader of subcircuits kept as text, in a scope that stands for the netlist: the
 *          definitions of the netlist (its .subckt blocks, .model cards and .param lines) are read in that scope, so
 *          the instance sees them as a block sees those around its own definition, and the elements and internal
 *          nodes are named X1.R1, X1.MID, X1.XLOAD.R2... as the reader names them.
 * @param[in,out] builder           Circuit to add the elements to.
 * @param[in]     definition_lines  The logical lines of the .subckt blocks (with their .subckt and .ends lines),
 *                                  .model cards and .param lines of the netlist, in the order they were written.
 * @param[in]     line              The X line, with its tokens.
 * @param[in]     resolve_node      Gives the node of the circuit for a node name written in the netlist.
 * @param[in]     observers         Told the name of each internal node and each source the instance makes.
 * @return  The warnings, such as the parameters of a model that the engine does not read, or the reason the line cannot
 *          be placed. The builder may hold part of the elements after a failure and must be discarded.
 */
std::expected<std::vector<std::string>, std::string>
AddNetlistInstance(CircuitBuilder &builder,
                   const std::vector<std::string> &definition_lines,
                   const DeckLine &line,
                   const std::function<NodeId(std::string_view)> &resolve_node,
                   const InstanceObservers &observers) {
    const std::optional<InstanceLine> instance = SplitInstanceLine(line.Tokens);
    if (!instance) {
        return std::unexpected(std::format("Malformed subcircuit call: '{}'", line.Text));
    }
    ExpansionContext context{.Builder = builder,
                             .SubcircuitName = instance->SubcircuitName,
                             .Warnings = {},
                             .ReportedModels = {},
                             .Observers = observers};
    TextScope netlist(context, nullptr, {});
    for (const std::string &name : instance->NodeNames) {
        netlist.MapNode(ToUpper(name), resolve_node(name));
    }
    const TextElement call{.Line = line.Text, .Tokens = line.Tokens};
    if (const auto expanded = ExpandTextInstance(netlist, definition_lines, call); !expanded) {
        return std::unexpected(expanded.error());
    }
    return std::move(context.Warnings);
}

} // namespace Nodalys
