/**
 * @file    compiler_context.h
 * @brief   Internal compilation scope, resolved entry context, and probe lookup helpers shared across Nodalys
 *          compiler modules.
 */

#ifndef IMCSIM_NODALYS_COMPILER_CONTEXT_H
#define IMCSIM_NODALYS_COMPILER_CONTEXT_H

#include "circuit/circuit.h"
#include <expected>
#include <map>
#include <string>
#include <vector>

namespace Core {

using Parameters = std::map<std::string, double>;
using ProbeNodes = std::map<int, std::map<int, int>>;

/**
 * @struct  ProbeSpec
 * @brief   Identifies one terminal of one circuit entry whose branch current must be instrumented with a 0 V probe.
 */
struct ProbeSpec {
    int Entry;
    int Terminal;
};

/**
 * @struct  CompileScope
 * @brief   One circuit level being compiled: the top circuit or the body of one subcircuit instance.
 */
struct CompileScope {
    /// The circuit whose entries are compiled, where the nets and the controlling currents are looked up.
    const Circuit &Owner;
    const std::vector<int> &NodeMap; ///< The node of the compiled circuit that each node of the owner stands for.
    const Parameters &Values; ///< The value of each parameter the instance of this level passes; empty at the top.
    /// Dotted name of the instance, empty at the top, which keeps the names of the devices apart between instances.
    const std::string &Prefix;
    /// entry_index -> terminal_index -> probe_node, the nodes that replace a terminal whose current is read.
    ProbeNodes Probes;
};

/**
 * @struct  EntryContext
 * @brief   What every device of one entry needs, worked out once: the name, the nodes in the compiled circuit and the
 *          value.
 */
struct EntryContext {
    const Component &Part;
    ComponentType Type;
    std::string Name;
    /// The compiled nodes of the terminals, with a probe node in place of each probed terminal.
    std::vector<int> Nodes;
    double Value = 0.0; ///< The value with its parameter references resolved; zero for a part that has none.
};

// Shared probe and error helpers
[[nodiscard]] std::expected<ProbeSpec, std::string> FindControllingCurrent(const Circuit &circuit,
                                                                           const std::string &name);
[[nodiscard]] std::string ProbeName(const std::string &prefix, int entry, int terminal);
[[nodiscard]] std::string NoDeviceMessage(const EntryContext &entry);

} // namespace Core

#endif // IMCSIM_NODALYS_COMPILER_CONTEXT_H
