/**
 * @file    analysis_inputs.cpp
 * @brief   What an analysis takes from the circuit and the stored settings: the sources, nodes and inputs it runs with.
 */

#include "analysis_inputs.h"

#include <algorithm>

namespace GUI {

namespace {

// A source that is empty, or that left the circuit or its AC setting, falls back to the first source that can be
// the input; sources must not be empty
std::string ResolveInputSource(const std::string &stored, const std::vector<std::string> &sources) {
    return std::ranges::contains(sources, stored) ? stored : sources.front();
}

// Nodes an ideal voltage source holds against ground, such as a supply rail or the output of a source, where no
// noise can show
std::vector<int> ListHeldNodes(const Core::Circuit &circuit) {
    std::vector<int> nodes;
    for (const Core::CircuitEntry &entry : circuit.GetEntries()) {
        const Core::ComponentType type = entry.Part->GetType();
        if (type == Core::ComponentType::VCC || type == Core::ComponentType::LOGIC_INPUT ||
            type == Core::ComponentType::CLOCK ||
            (type == Core::ComponentType::VOLTAGE_SOURCE && entry.Nodes[1] == 0)) {
            nodes.push_back(entry.Nodes[0]);
        } else if (type == Core::ComponentType::VOLTAGE_SOURCE && entry.Nodes[0] == 0) {
            nodes.push_back(entry.Nodes[1]);
        }
    }
    return nodes;
}

} // namespace

/**
 * @brief   Lists the sources a DC sweep can drive.
 * @param[in] circuit  Circuit to look in.
 * @return  The voltage sources, current sources and VCC rails, in circuit order, with the unit of each.
 */
std::vector<SweepSource> ListSweepSources(const Core::Circuit &circuit) {
    std::vector<SweepSource> sources;
    for (const std::string &name : Core::GetSweepableSources(circuit)) {
        const auto entry = std::ranges::find_if(circuit.GetEntries(), [&name](const Core::CircuitEntry &candidate) {
            return candidate.Part->GetName() == name;
        });
        sources.push_back({name, entry->Part->GetType() == Core::ComponentType::CURRENT_SOURCE ? "A" : "V"});
    }
    return sources;
}

/**
 * @brief   Picks the source a range sweeps.
 * @param[in] range            Range with the stored source name, which may be empty or name a source that left.
 * @param[in] sources          Sources the circuit offers; it must not be empty.
 * @param[in] preferred_index  Source to use when the stored one is not offered, so a new schematic is ready to run.
 * @return  The stored name when the circuit still offers it, else the preferred source or the last one if there are
 *          fewer. The stored name is kept in the settings, so the choice comes back with its source.
 */
const std::string &ResolveSweepSource(const Core::SweepRange &range, const std::vector<SweepSource> &sources,
                                      const std::size_t preferred_index) {
    if (std::ranges::contains(sources, range.Source, &SweepSource::Name)) {
        return range.Source;
    }
    return sources[std::min(preferred_index, sources.size() - 1)].Name;
}

/**
 * @brief   Picks the input source of an analysis that has one.
 * @param[in] stored   Name stored in the settings.
 * @param[in] sources  Sources that can be the input.
 * @return  The stored name when it is still among the sources, else the first source; an empty name when no source
 *          can be the input.
 */
std::string ResolveOptionalInput(const std::string &stored, const std::vector<std::string> &sources) {
    return sources.empty() ? std::string() : ResolveInputSource(stored, sources);
}

/**
 * @brief   Picks the node an analysis measures at.
 * @param[in] point      Point stored in the settings, which follows its node through edits.
 * @param[in] circuit    Circuit to measure.
 * @param[in] schematic  Schematic with the nodes and the measured voltages.
 * @return  The node under the stored point; else the first measured voltage that no voltage source holds, which is
 *          most likely the output the user looks at; else the last node that none holds. No value when the circuit
 *          has no node besides ground.
 */
std::optional<int> ResolveOutputNode(const std::optional<GridPoint> &point, const Core::Circuit &circuit,
                                     Schematic &schematic) {
    if (point) {
        if (const std::optional<int> node = schematic.GetConnectivity().GetNode(*point); node && *node > 0) {
            return node;
        }
    }
    const std::vector<int> held = ListHeldNodes(circuit);
    for (int node = 1; node < circuit.GetNodeCount(); ++node) {
        if (schematic.IsVoltageMeasured(node) && !std::ranges::contains(held, node)) {
            return node;
        }
    }
    for (int node = circuit.GetNodeCount() - 1; node > 0; --node) {
        if (!std::ranges::contains(held, node)) {
            return node;
        }
    }
    return circuit.GetNodeCount() > 1 ? std::optional<int>(circuit.GetNodeCount() - 1) : std::nullopt;
}

/**
 * @brief   Lists the nodes a truth table reads.
 * @param[in] circuit    Circuit to read.
 * @param[in] schematic  Schematic that knows which voltages are measured.
 * @return  The measured nodes, in node order.
 */
std::vector<int> ListMeasuredNodes(const Core::Circuit &circuit, const Schematic &schematic) {
    std::vector<int> nodes;
    for (int node = 1; node < circuit.GetNodeCount(); ++node) {
        if (schematic.IsVoltageMeasured(node)) {
            nodes.push_back(node);
        }
    }
    return nodes;
}

/**
 * @brief   Picks the logic inputs a truth table varies.
 * @param[in] stored     Names stored in the settings.
 * @param[in] available  Logic inputs the circuit has, in circuit order.
 * @return  The stored ones that are still in the circuit, in circuit order; every logic input when none is left.
 */
std::vector<std::string> ResolveTruthTableInputs(const std::vector<std::string> &stored,
                                                 const std::vector<std::string> &available) {
    std::vector<std::string> chosen;
    for (const std::string &name : available) {
        if (std::ranges::contains(stored, name)) {
            chosen.push_back(name);
        }
    }
    return chosen.empty() ? available : chosen;
}

/**
 * @brief   Names the unit of the values a stepped analysis gives a parameter.
 * @param[in] circuit  Circuit with the part.
 * @param[in] part     Name of the stepped part, or empty for the temperature.
 * @return  The unit of the part, or degrees Celsius for the temperature; empty for a part that left the circuit,
 *          which the run then reports.
 */
std::string GetParameterUnit(const Core::Circuit &circuit, const std::string &part) {
    if (part.empty()) {
        return "°C";
    }
    for (const Core::CircuitEntry &entry : circuit.GetEntries()) {
        if (entry.Part->GetName() == part) {
            return entry.Part->GetUnit();
        }
    }
    return "";
}

} // namespace GUI
