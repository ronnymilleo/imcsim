/**
 * @file    netlist_context.h
 * @brief   What the directives and the elements of a netlist read and change while it is parsed.
 */

#ifndef NODALYS_PARSER_NETLIST_CONTEXT_H
#define NODALYS_PARSER_NETLIST_CONTEXT_H

#include "nodalys/circuit_builder.h"
#include "nodalys/netlist_parser.h"
#include "nodalys/types.h"
#include "parser/netlist/netlist_deck.h"
#include "parser/netlist/netlist_nodes.h"

#include <optional>
#include <string>
#include <vector>

namespace Nodalys {

/// The temperature SPICE simulates at, and measures the model parameters at, unless the netlist says otherwise.
inline constexpr double DEFAULT_TEMPERATURE_CELSIUS = 27.0;

/**
 * @struct  BlockSource
 * @brief   An independent source inside a subcircuit instance, which a .pz directive may name by its nodes.
 */
struct BlockSource {
    std::string Name;
    NodeId Positive;
    NodeId Negative;
};

/**
 * @struct  DeckSettings
 * @brief   The settings of .options and .temp, applied once the whole netlist is read, since they may follow the
 *          analyses they change.
 */
struct DeckSettings {
    std::optional<IntegrationMethod> Method;
    double TemperatureCelsius = DEFAULT_TEMPERATURE_CELSIUS;
    double NominalCelsius = DEFAULT_TEMPERATURE_CELSIUS;
    std::optional<unsigned int> Seed;
};

/**
 * @struct  DeckContext
 * @brief   The netlist being parsed and what its lines build, which every directive and element reads or adds to.
 */
struct DeckContext {
    ParsedNetlist &Result;
    CircuitBuilder &Builder;
    NetlistNodes &Nodes;
    const Deck &Lines;
    /// The sources of the subcircuit instances, which are placed before the other lines are read.
    std::vector<BlockSource> BlockSources;
    DeckSettings Settings;
};

} // namespace Nodalys

#endif // NODALYS_PARSER_NETLIST_CONTEXT_H
