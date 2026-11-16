/**
 * @file    initial_voltage_targets.cpp
 * @brief   Finds the node each initial voltage marker sets in the circuit as every instance of a block expands it.
 */

#include "circuit/initial_voltage_targets.h"

#include "circuit/circuit.h"
#include "circuit/components/component.h"
#include "circuit/components/subcircuit.h"
#include "circuit/subcircuit.h"
#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace Core {

namespace {

/// Blocks nested deeper than this are left alone, which only a cycle of definitions reaches; the checks refuse it.
constexpr std::size_t MAX_BLOCK_DEPTH = 32;

/// A node of the expanded circuit: the instances down to its block, its number there, and the circuit of that number.
struct NodeAddress {
    std::vector<std::string> Path;
    int Node = 0;
    const Circuit *Owner = nullptr;
};

/**
 * @brief   Where each node of a block body lands: a pin on the outer node of the instance, ground on ground, and any
 *          other node on the node of this instance only.
 */
std::vector<NodeAddress> AddressBody(const Circuit &root, const SubcircuitDefinition &definition,
                                     const CircuitEntry &instance, const std::vector<NodeAddress> &outer,
                                     const std::vector<std::string> &path) {
    int node_count = definition.Body.GetNodeCount();
    for (const SubcircuitPin &pin : definition.Pins) {
        node_count = std::max(node_count, pin.Node + 1);
    }
    std::vector<NodeAddress> addresses;
    for (int node = 0; node < node_count; ++node) {
        addresses.push_back({.Path = path, .Node = node, .Owner = &definition.Body});
    }
    addresses[0] = {.Path = {}, .Node = 0, .Owner = &root};
    for (std::size_t pin = 0; pin < definition.Pins.size() && pin < instance.Nodes.size(); ++pin) {
        addresses[static_cast<std::size_t>(definition.Pins[pin].Node)] =
            outer[static_cast<std::size_t>(instance.Nodes[pin])];
    }
    return addresses;
}

void Collect(const Circuit &root, const Circuit &level, const std::vector<std::string> &path,
             const std::vector<NodeAddress> &addresses, std::vector<InitialVoltageTarget> &targets) {
    for (const CircuitInitialVoltage &initial_voltage : level.GetInitialVoltages()) {
        const NodeAddress &address = addresses[static_cast<std::size_t>(initial_voltage.Node)];
        targets.push_back({.Path = address.Path,
                           .Node = address.Node,
                           .Volts = initial_voltage.Volts,
                           .MarkerPath = path,
                           .Owner = address.Owner});
    }
    if (path.size() >= MAX_BLOCK_DEPTH) {
        return;
    }
    for (const CircuitEntry &entry : level.GetEntries()) {
        if (entry.Part->GetType() != ComponentType::SUBCIRCUIT) {
            continue;
        }
        const auto &instance = static_cast<const SubcircuitInstance &>(*entry.Part);
        const std::shared_ptr<const SubcircuitDefinition> definition =
            root.FindSubcircuit(instance.GetDefinitionName());
        if (!definition || definition->Spice) {
            continue;
        }
        std::vector<std::string> inner_path = path;
        inner_path.push_back(instance.GetName());
        Collect(root, definition->Body, inner_path, AddressBody(root, *definition, entry, addresses, inner_path),
                targets);
    }
}

} // namespace

/**
 * @brief   Lists every initial voltage of the circuit with the node it sets once the blocks are expanded.
 * @param[in] circuit  Top circuit, whose definitions are looked up by the name each instance gives.
 * @return  The markers of the top circuit, then those of each block instance in order, nested ones included. A block
 *          body that no instance places has no entry.
 */
std::vector<InitialVoltageTarget> ResolveInitialVoltages(const Circuit &circuit) {
    std::vector<NodeAddress> addresses;
    for (int node = 0; node < circuit.GetNodeCount(); ++node) {
        addresses.push_back({.Path = {}, .Node = node, .Owner = &circuit});
    }
    std::vector<InitialVoltageTarget> targets;
    Collect(circuit, circuit, {}, addresses, targets);
    return targets;
}

/**
 * @brief   Joins the names of the instances the way ngspice names the nodes inside them.
 * @param[in] path  Instance names, outermost first.
 * @return  The names joined by dots, such as "X1.X2"; empty for an empty path.
 */
std::string FormatInstancePath(const std::vector<std::string> &path) {
    std::string text;
    for (const std::string &name : path) {
        text += text.empty() ? name : "." + name;
    }
    return text;
}

} // namespace Core
