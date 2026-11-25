/**
 * @file    subcircuit_text.cpp
 * @brief   Adds a subcircuit written as SPICE text, such as a vendor's op-amp model, to a circuit being built.
 */

#include "nodalys/subcircuit_text.h"

#include "parser/subcircuit/subcircuit_expansion.h"
#include "parser/subcircuit/subcircuit_scope.h"
#include "parser/syntax/spice_text.h"
#include "text/text_case.h"

#include <cstddef>
#include <format>
#include <utility>

namespace Nodalys {

/**
 * @brief   Adds a subcircuit written as SPICE text to a circuit being built, as one instance.
 * @param[in,out] builder   Circuit to add the elements to.
 * @param[in]     text      The subcircuit.
 * @param[in]     instance  Where its ports connect, the name its elements start with and its parameter values.
 * @return  The warnings, such as the parameters of a model that the engine does not read, or the reason the text
 *          cannot be simulated. The builder may hold part of the elements after a failure and must be discarded.
 */
std::expected<std::vector<std::string>, std::string>
AddSubcircuitText(CircuitBuilder &builder, const SubcircuitText &text, const SubcircuitTextInstance &instance) {
    if (instance.PortNodes.size() != text.Ports.size()) {
        return std::unexpected(std::format("Nodalys cannot simulate the subcircuit {}: it has {} ports and the "
                                           "instance connects {}",
                                           text.Name,
                                           text.Ports.size(),
                                           instance.PortNodes.size()));
    }
    ExpansionContext context{.Builder = builder,
                             .SubcircuitName = text.Name,
                             .Warnings = {},
                             .ReportedModels = {},
                             .Observers = instance.Observers};
    TextScope scope(context, nullptr, instance.Prefix);
    for (const auto &[name, value] : instance.Parameters) {
        scope.SetParameter(ToUpper(name), value);
    }
    for (std::size_t port = 0; port < text.Ports.size(); ++port) {
        scope.MapNode(ToUpper(text.Ports[port]), instance.PortNodes[port]);
    }
    if (const auto expanded = ExpandText(scope, SplitLogicalLines(text.Body)); !expanded) {
        return std::unexpected(expanded.error());
    }
    return std::move(context.Warnings);
}

/**
 * @brief   Tells whether the engine can simulate a subcircuit text, without a circuit to put it in.
 * @param[in] text        The subcircuit.
 * @param[in] parameters  The value of each parameter of its .subckt line.
 * @return  The reason it cannot, written for the user, or none when it can.
 */
std::optional<std::string> CheckSubcircuitText(const SubcircuitText &text,
                                               const std::map<std::string, double> &parameters) {
    CircuitBuilder scratch;
    SubcircuitTextInstance instance{.Prefix = "check", .PortNodes = {}, .Parameters = parameters, .Observers = {}};
    for (std::size_t port = 0; port < text.Ports.size(); ++port) {
        instance.PortNodes.push_back(scratch.AddNode());
    }
    const auto added = AddSubcircuitText(scratch, text, instance);
    return added ? std::nullopt : std::optional<std::string>(added.error());
}

} // namespace Nodalys
