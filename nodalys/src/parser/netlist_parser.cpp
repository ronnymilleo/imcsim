/**
 * @file    netlist_parser.cpp
 * @brief   Parses SPICE netlists into compiled circuits and simulation requests.
 */

#include "nodalys/netlist_parser.h"

#include "nodalys/circuit_builder.h"
#include "parser/netlist_context.h"
#include "parser/netlist_deck.h"
#include "parser/netlist_directives.h"
#include "parser/netlist_elements.h"
#include "parser/netlist_nodes.h"
#include "parser/netlist_subcircuits.h"

#include <cctype>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Nodalys {

namespace {

bool IsDirective(const DeckLine &line) {
    return line.Tokens.front().front() == '.';
}

/**
 * @brief   Notes every node the lines of the netlist name, so the nodes can be numbered before anything is built.
 */
NetlistNodes RegisterCircuitNodes(const Deck &deck) {
    NetlistNodes nodes;
    for (const DeckLine &line : deck.CircuitLines) {
        for (const std::string &name : IsDirective(line) ? ListDirectiveNodes(line) : ListElementNodes(line)) {
            nodes.Register(name);
        }
    }
    return nodes;
}

/**
 * @brief   Places every X line, before the other lines are read, since the directives may name the internal nodes of
 *          an instance (X1.MID) and its sources, wherever they stand in the netlist.
 */
std::expected<void, std::string> PlaceSubcircuitInstances(DeckContext &context) {
    const InstanceObservers observers{
        .OnInternalNode = [&context](const std::string &name, const NodeId node) { context.Nodes.Bind(name, node); },
        .OnSource = [&context](const std::string &name,
                               const NodeId positive,
                               const NodeId negative) { context.BlockSources.push_back({name, positive, negative}); },
    };
    for (const DeckLine &line : context.Lines.CircuitLines) {
        if (std::toupper(static_cast<unsigned char>(line.Tokens.front().front())) != 'X') {
            continue;
        }
        const auto added = AddNetlistInstance(
            context.Builder,
            context.Lines.DefinitionLines,
            line,
            [&context](const std::string_view name) { return context.Nodes.Resolve(name); },
            observers);
        if (!added) {
            return std::unexpected(added.error());
        }
        for (const std::string &warning : *added) {
            AddUniqueDiagnostic(context.Result.Diagnostics, warning);
        }
    }
    return {};
}

std::expected<void, std::string> ReadCircuitLines(DeckContext &context) {
    for (const DeckLine &line : context.Lines.CircuitLines) {
        if (auto read = IsDirective(line) ? ReadDirective(context, line) : ReadElement(context, line); !read) {
            return read;
        }
    }
    return {};
}

} // namespace

/**
 * @brief   Parses a SPICE netlist into a circuit and the analyses it asks for.
 * @details The lines are sorted first, then every node is numbered, the subcircuit instances are placed, and the
 *          directives and elements are read in order. The settings of .options and .temp apply once all of them
 *          are read, since they may follow the analyses they change. A netlist with no analysis gets an operating
 *          point.
 * @param[in] netlist_text  The netlist, title line first.
 * @return  The circuit, its analyses, options and diagnostics; the reason the netlist cannot be read otherwise.
 */
std::expected<ParsedNetlist, std::string> ParseNetlist(const std::string_view netlist_text) {
    std::expected<Deck, std::string> deck = ReadDeck(netlist_text);
    if (!deck) {
        return std::unexpected(deck.error());
    }
    ParsedNetlist result;
    result.Title = deck->Title;
    result.Diagnostics = std::move(deck->Diagnostics);
    CircuitBuilder builder;
    NetlistNodes nodes = RegisterCircuitNodes(*deck);
    builder.SetCircuitNodeCount(nodes.AssignIds());
    DeckContext context{.Result = result,
                        .Builder = builder,
                        .Nodes = nodes,
                        .Lines = *deck,
                        .BlockSources = {},
                        .Settings = {}};
    if (auto placed = PlaceSubcircuitInstances(context); !placed) {
        return std::unexpected(placed.error());
    }
    if (auto read = ReadCircuitLines(context); !read) {
        return std::unexpected(read.error());
    }
    ApplyDeckSettings(context.Settings, builder, result.Analyses);
    auto circuit = builder.Build();
    if (!circuit) {
        return std::unexpected(circuit.error());
    }
    result.CircuitInstance = std::move(*circuit);
    if (result.Analyses.empty()) {
        result.Analyses.push_back({AnalysisKind::OPERATING_POINT, std::monostate{}});
    }
    return result;
}

} // namespace Nodalys
