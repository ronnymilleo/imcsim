/**
 * @file    circuit_currents.cpp
 * @brief   Names the currents a part reports and the probes that measure them.
 * @details Defines the current probe functions of circuit.h.
 */

#include "circuit/circuit.h"

#include "circuit/behavioral_expression.h"
#include "circuit/components/behavioral_source.h"
#include "circuit/components/bjt.h"
#include "circuit/components/clock.h"
#include "circuit/components/controlled_source.h"
#include "circuit/components/diode.h"
#include "circuit/components/fuse.h"
#include "circuit/components/logic_input.h"
#include "circuit/components/logic_part.h"
#include "circuit/components/mosfet.h"
#include "circuit/components/net_label.h"
#include "circuit/components/op_amp.h"
#include "circuit/components/potentiometer.h"
#include "circuit/components/relay.h"
#include "circuit/components/source.h"
#include "circuit/components/subcircuit.h"
#include "circuit/components/switch.h"
#include "circuit/components/transformer.h"
#include "circuit/components/voltage_controlled_switch.h"
#include "circuit/subcircuit.h"
#include "spice/spice_value.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <iterator>
#include <set>
#include <sstream>
#include <utility>

namespace Core {

/**
 * @brief   Supplies and voltage sources carry their own branch current, which a CCCS or CCVS can follow directly.
 * @param[in] type  Type of the part.
 * @return  True for supplies, voltage sources, clocks and logic inputs.
 */
bool HasBranchCurrent(const ComponentType type) {
    return type == ComponentType::VCC || type == ComponentType::VOLTAGE_SOURCE || type == ComponentType::CLOCK ||
           type == ComponentType::LOGIC_INPUT;
}

/**
 * @brief   Lists the terminals of a component whose current is measured with a probe.
 * @param[in] component  Any component.
 * @return  The anode of a diode part, the first terminal of a switch or a fuse, every terminal of a transistor or a
 *          potentiometer, the contact and the coil of a relay, the two windings of a transformer, and the first output
 * terminal of a controlled source or the output of an op-amp; nothing for the others, whose currents ngspice reports
 * directly.
 */
std::vector<CurrentProbe> GetCurrentProbes(const Component &component) {
    const ComponentType type = component.GetType();
    if (IsDiode(type) || IsControlledSource(type) || IsBehavioralSource(type) || IsSwitch(type) ||
        type == ComponentType::V_SWITCH || type == ComponentType::FUSE || type == ComponentType::OP_AMP) {
        return {{0, ""}};
    }
    if (IsLogicGate(type)) {
        return {{static_cast<const LogicPart &>(component).GetInputCount(), ""}};
    }
    if (IsFlipFlop(type)) {
        const auto outputs = static_cast<int>(GetLogicInputLabels(type).size());
        return {{outputs, "Q"}, {outputs + 1, "QN"}};
    }
    if (type == ComponentType::RELAY) {
        return {{0, "S"}, {2, "C"}};
    }
    if (type == ComponentType::TRANSFORMER) {
        return {{0, "P"}, {2, "S"}};
    }
    if (IsBJT(component.GetType())) {
        return {{0, "C"}, {1, "B"}, {2, "E"}};
    }
    if (IsMOSFET(component.GetType())) {
        return {{0, "D"}, {1, "G"}, {2, "S"}};
    }
    if (component.GetType() == ComponentType::POTENTIOMETER) {
        return {{0, "A"}, {1, "B"}, {2, "W"}};
    }
    return {};
}

/**
 * @brief   Returns the name of the 0 V source that measures the current of a terminal in a netlist with probes.
 * @param[in] component  Part the probe belongs to.
 * @param[in] label      Label of the probed terminal, from GetCurrentProbes().
 * @return  "Vprobe-" followed by the part name and, when there is one, "-" and the label, such as "Vprobe-Q1-C".
 *          The dash keeps it apart from any name IsValidName() accepts.
 */
std::string GetCurrentProbeName(const Component &component, const std::string_view label) {
    if (label.empty()) {
        return std::format("Vprobe-{}", component.GetName());
    }
    return std::format("Vprobe-{}-{}", component.GetName(), label);
}

/**
 * @brief   Returns the name a probed terminal current has in the simulation results.
 * @param[in] component  Part the probe belongs to.
 * @param[in] label      Label of the probed terminal, from GetCurrentProbes().
 * @return  The part name and, when there is one, "." and the label, such as "Q1.C"; just "D1" for a diode.
 */
std::string GetCurrentName(const Component &component, const std::string_view label) {
    if (label.empty()) {
        return component.GetName();
    }
    return std::format("{}.{}", component.GetName(), label);
}

/**
 * @brief   Tells whether a part with no probes reports one current, through its two terminals.
 * @param[in] type  Type of the part.
 * @return  True for resistors, capacitors, inductors and every source; false for ground, net labels and the parts that
 *          report their currents through probes or not at all, such as the logic parts.
 */
bool HasSingleCurrent(const ComponentType type) {
    static constexpr std::array SINGLE_CURRENT_TYPES = {
        ComponentType::RESISTOR,    ComponentType::CAPACITOR,      ComponentType::INDUCTOR,
        ComponentType::VCC,         ComponentType::VOLTAGE_SOURCE, ComponentType::CLOCK,
        ComponentType::LOGIC_INPUT, ComponentType::CURRENT_SOURCE,
    };
    return std::ranges::contains(SINGLE_CURRENT_TYPES, type);
}

/**
 * @brief   Lists the currents a component reports in simulation results, in the order they appear.
 * @param[in] component  Any component.
 * @return  The component name for a two-terminal part or a supply, one name per terminal for a transistor
 *          (such as "Q1.C"), and nothing for ground.
 */
std::vector<std::string> GetCurrentNames(const Component &component) {
    std::vector<std::string> names;
    const std::vector<CurrentProbe> probes = GetCurrentProbes(component);
    for (const CurrentProbe &probe : probes) {
        names.push_back(GetCurrentName(component, probe.Label));
    }
    if (probes.empty() && HasSingleCurrent(component.GetType())) {
        names.push_back(component.GetName());
    }
    return names;
}

/**
 * @brief   Tells which terminal each current of GetCurrentNames() flows into.
 * @param[in] component  Any component.
 * @return  One terminal index per current, in the same order: the probed terminal for a part with probes, and the
 *          first terminal for a part whose single current enters there and leaves by the second.
 */
std::vector<std::size_t> GetCurrentTerminals(const Component &component) {
    std::vector<std::size_t> terminals;
    const std::vector<CurrentProbe> probes = GetCurrentProbes(component);
    if (probes.empty()) {
        terminals.assign(GetCurrentNames(component).size(), 0);
        return terminals;
    }
    for (const CurrentProbe &probe : probes) {
        terminals.push_back(static_cast<std::size_t>(probe.Terminal));
    }
    return terminals;
}

/**
 * @brief   Finds the voltage source a CCCS or CCVS follows to get a current of the circuit.
 * @param[in] circuit   Circuit the current belongs to.
 * @param[in] current   A current as the results name it, such as "R1", "Vin1" or "Q1.C".
 * @return  The supply or voltage source itself, or the probe on the part's terminal, which ToSpiceNetlist() adds
 *          for every current a CCCS or CCVS follows; no value when no part reports that current.
 */
std::optional<std::string> FindControllingSource(const Circuit &circuit, const std::string_view current) {
    for (const CircuitEntry &entry : circuit.GetEntries()) {
        const Component &component = *entry.Part;
        if (component.GetName().empty()) {
            continue;
        }
        const std::vector<CurrentProbe> probes = GetCurrentProbes(component);
        for (const CurrentProbe &probe : probes) {
            if (current == GetCurrentName(component, probe.Label)) {
                return GetCurrentProbeName(component, probe.Label);
            }
        }
        if (probes.empty() && current == component.GetName()) {
            return HasBranchCurrent(component.GetType()) ? component.GetName() : GetCurrentProbeName(component, "");
        }
    }
    return std::nullopt;
}

} // namespace Core
