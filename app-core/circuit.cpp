/**
 * @file    circuit.cpp
 * @brief   Circuit topology handed to the simulator: components and the nodes their terminals connect to.
 */

#include "circuit.h"

#include "components/bjt.h"
#include "components/controlled_source.h"
#include "components/diode.h"
#include "components/mosfet.h"
#include "components/op_amp.h"
#include "components/source.h"
#include "spice_value.h"
#include <algorithm>
#include <format>
#include <utility>

namespace Core {

namespace {

bool IsACSource(const Component &component) {
    return IsSource(component.GetType()) &&
           static_cast<const Source &>(component).GetSourceType() == Source::SourceType::AC;
}

// One line serves every analysis: .op reads the DC value, .ac the AC magnitude and .tran the waveform. A pulse
// starts at its low level, so that is its DC value. The name tells SPICE whether it is a voltage or current source
std::string FormatSource(const Source &source, const std::vector<int> &nodes) {
    const std::string start = std::format("{} {} {}", source.GetName(), nodes[0], nodes[1]);
    switch (source.GetSourceType()) {
    case Source::SourceType::DC:
        return std::format("{} DC {}\n", start, FormatSpiceValue(source.GetValue()));
    case Source::SourceType::AC: {
        const ACParameters &ac = source.GetAC();
        const std::string offset = FormatSpiceValue(ac.Offset);
        return std::format("{} DC {} AC {} SIN({} {} {})\n", start, offset, FormatSpiceValue(ac.Magnitude), offset,
                           FormatSpiceValue(ac.Amplitude), FormatSpiceValue(ac.Frequency));
    }
    case Source::SourceType::Pulse: {
        const PulseParameters &pulse = source.GetPulse();
        const std::string low = FormatSpiceValue(pulse.Low);
        return std::format("{} DC {} PULSE({} {} {} {} {} {} {})\n", start, low, low, FormatSpiceValue(pulse.High),
                           FormatSpiceValue(pulse.Delay), FormatSpiceValue(pulse.RiseTime),
                           FormatSpiceValue(pulse.FallTime), FormatSpiceValue(pulse.Width),
                           FormatSpiceValue(pulse.Period));
    }
    }
    return "";
}

// Supplies and voltage sources carry their own branch current, which a CCCS or CCVS can follow directly
bool HasBranchCurrent(const ComponentType type) {
    return type == ComponentType::VCC || type == ComponentType::VoltageSource;
}

// The currents that CCCS and CCVS sources of the circuit follow
std::vector<std::string> ListControllingCurrents(const std::vector<CircuitEntry> &entries) {
    std::vector<std::string> currents;
    for (const CircuitEntry &entry : entries) {
        if (IsCurrentControlled(entry.Part->GetType())) {
            currents.push_back(static_cast<const ControlledSource &>(*entry.Part).GetControllingCurrent());
        }
    }
    return currents;
}

// A current names its part, alone or followed by "." and a terminal label
bool CarriesCurrent(const Component &component, const std::vector<std::string> &currents) {
    const std::string &name = component.GetName();
    return !name.empty() && std::ranges::any_of(currents, [&name](const std::string &current) {
        return current == name ||
               (current.starts_with(name) && current.size() > name.size() && current[name.size()] == '.');
    });
}

// The probes of a part: those it always reports, when probes are asked for or when one of its currents controls a
// source; a part without probes of its own gets one on its first terminal when it controls a source, unless it has
// a branch current already
std::vector<CurrentProbe> ProbesFor(const Component &component, const bool add_current_probes, const bool controlling) {
    if (!add_current_probes && !controlling) {
        return {};
    }
    std::vector<CurrentProbe> probes = GetCurrentProbes(component);
    if (probes.empty() && controlling && !HasBranchCurrent(component.GetType())) {
        probes.push_back({0, ""});
    }
    return probes;
}

// Moves every probed terminal onto a node of its own, joined to its circuit node by a 0 V source whose current
// flows into the terminal, and returns the lines of those sources
std::string ProbeTerminals(const Component &component, const std::vector<CurrentProbe> &probes, std::vector<int> &nodes,
                           int &next_probe_node) {
    std::string lines;
    for (const CurrentProbe &probe : probes) {
        const auto terminal = static_cast<std::size_t>(probe.Terminal);
        lines += std::format("{} {} {} DC 0\n", GetCurrentProbeName(component, probe.Label), nodes[terminal],
                             next_probe_node);
        nodes[terminal] = next_probe_node++;
    }
    return lines;
}

// Parts that share a ready model share its line, written the first time the model comes up
void AddModelLine(std::string spice_name, const std::string &line, std::vector<std::string> &written_names,
                  std::string &models) {
    if (std::ranges::contains(written_names, spice_name)) {
        return;
    }
    models += line;
    written_names.push_back(std::move(spice_name));
}

} // namespace

/**
 * @brief   Adds a component and the nodes its terminals connect to.
 * @param[in] component  Component to add; it must outlive the circuit.
 * @param[in] nodes      Node of each terminal, in terminal order; node 0 is ground.
 */
void Circuit::Add(const Component &component, std::vector<int> nodes) {
    for (const int node : nodes) {
        m_NodeCount = std::max(m_NodeCount, node + 1);
    }
    m_Entries.push_back({&component, std::move(nodes)});
}

/**
 * @brief   Returns the components of the circuit, in the order they were added.
 * @return  Every component with the nodes of its terminals.
 */
const std::vector<CircuitEntry> &Circuit::GetEntries() const {
    return m_Entries;
}

/**
 * @brief   Returns how many nodes the circuit uses, counting ground.
 * @return  The highest node number plus one, or 0 for an empty circuit.
 */
int Circuit::GetNodeCount() const {
    return m_NodeCount;
}

/**
 * @brief   Tells whether the circuit has a ground reference.
 * @return  True when it contains at least one ground component; without one, node voltages are undefined.
 */
bool Circuit::HasGround() const {
    return std::ranges::any_of(
        m_Entries, [](const CircuitEntry &entry) { return entry.Part->GetType() == ComponentType::Ground; });
}

/**
 * @brief   Tells whether the circuit has a source that an AC sweep can excite.
 * @return  True when it contains at least one voltage or current source set to AC.
 */
bool Circuit::HasACSource() const {
    return std::ranges::any_of(m_Entries, [](const CircuitEntry &entry) { return IsACSource(*entry.Part); });
}

/**
 * @brief   Writes the circuit as a SPICE netlist, ready to hand to ngspice.
 * @param[in] analysis            Optional analysis command, such as ".op", written right before ".end".
 * @param[in] add_current_probes  Adds a 0 V source in series with every terminal GetCurrentProbes() lists, named
 *                                by GetCurrentProbeName(), whose branch current is the terminal current; for
 *                                simulation only.
 * @return  One line per component, plus its probes, with node numbers as SPICE node names (0 is ground), then one
 *          ".model" line per model in use, ending in ".end".
 * @note    Ground components produce no line; they only make their node 0. A supply rail becomes a DC voltage
 *          source from its node to ground. A part whose current controls a CCCS or CCVS gets a probe even without
 *          add_current_probes, unless it is a voltage source itself, since ngspice follows currents through
 *          voltage sources. An AC source writes its offset, AC magnitude and sine together, and a
 *          pulse source its low level and pulse, so the same netlist works for .op, .ac and .tran. Probes and the gain
 *          stage of each ideal op-amp use nodes numbered after GetNodeCount(), so the circuit nodes keep their
 *          numbers. Op-amp macromodels are subcircuits, written after the models.
 */
std::string Circuit::ToSpiceNetlist(const std::string_view analysis, const bool add_current_probes) const {
    std::string netlist = "* imcsim netlist\n";
    const std::vector<std::string> controlling_currents = ListControllingCurrents(m_Entries);
    std::vector<std::string> model_names;
    std::string models;
    int next_extra_node = m_NodeCount;
    for (const CircuitEntry &entry : m_Entries) {
        const Component &component = *entry.Part;
        const std::string value = FormatSpiceValue(component.GetValue());
        // Probed parts connect through the probe nodes; the probe lines follow the part
        std::vector<int> nodes = entry.Nodes;
        const std::string probes = ProbeTerminals(
            component, ProbesFor(component, add_current_probes, CarriesCurrent(component, controlling_currents)), nodes,
            next_extra_node);
        switch (component.GetType()) {
        case ComponentType::Resistor:
        case ComponentType::Capacitor:
        case ComponentType::Inductor:
            netlist += std::format("{} {} {} {}\n", component.GetName(), nodes[0], nodes[1], value);
            break;
        case ComponentType::VCC:
            netlist += std::format("{} {} 0 DC {}\n", component.GetName(), nodes[0], value);
            break;
        case ComponentType::VoltageSource:
        case ComponentType::CurrentSource:
            netlist += FormatSource(static_cast<const Source &>(component), nodes);
            break;
        case ComponentType::VCVS:
        case ComponentType::VCCS:
            netlist +=
                std::format("{} {} {} {} {} {}\n", component.GetName(), nodes[0], nodes[1], nodes[2], nodes[3], value);
            break;
        case ComponentType::CCCS:
        case ComponentType::CCVS: {
            // ngspice follows the current through a voltage source: the controlling part's own, or its probe
            const std::string &current = static_cast<const ControlledSource &>(component).GetControllingCurrent();
            const std::string control = FindControllingSource(*this, current).value_or(current);
            netlist += std::format("{} {} {} {} {}\n", component.GetName(), nodes[0], nodes[1], control, value);
            break;
        }
        case ComponentType::OpAmp: {
            // A macromodel is a subcircuit, written once for every op-amp of its model
            const auto &op_amp = static_cast<const OpAmp &>(component);
            if (op_amp.IsIdeal()) {
                netlist += FormatIdealOpAmp(op_amp, nodes, next_extra_node++);
                AddModelLine(GetIdealOpAmpModelName(), FormatIdealOpAmpModel(), model_names, models);
                break;
            }
            std::string subcircuit = op_amp.GetSpiceModelName();
            netlist += std::format("X-{} {} {} {} {} {} {}\n", op_amp.GetName(), nodes[0], nodes[1], nodes[2], nodes[3],
                                   nodes[4], subcircuit);
            AddModelLine(subcircuit, FormatOpAmpSubcircuit(subcircuit, op_amp.GetParameters()), model_names, models);
            break;
        }
        case ComponentType::Diode:
        case ComponentType::ZenerDiode:
        case ComponentType::LED: {
            const auto &diode = static_cast<const Diode &>(component);
            std::string model_name = diode.GetSpiceModelName();
            netlist += std::format("{} {} {} {}\n", diode.GetName(), nodes[0], nodes[1], model_name);
            AddModelLine(model_name, FormatDiodeModel(model_name, diode.GetParameters()), model_names, models);
            break;
        }
        case ComponentType::NPN:
        case ComponentType::PNP: {
            const auto &bjt = static_cast<const BJT &>(component);
            std::string model_name = bjt.GetSpiceModelName();
            netlist += std::format("{} {} {} {} {}\n", bjt.GetName(), nodes[0], nodes[1], nodes[2], model_name);
            AddModelLine(model_name, FormatBJTModel(bjt.GetType(), model_name, bjt.GetParameters()), model_names,
                         models);
            break;
        }
        case ComponentType::NMOS:
        case ComponentType::PMOS: {
            // The body is tied to the source, past its probe, so the source current includes it
            const auto &mosfet = static_cast<const MOSFET &>(component);
            const MOSFETParameters &parameters = mosfet.GetParameters();
            std::string model_name = mosfet.GetSpiceModelName();
            netlist += std::format("{} {} {} {} {} {} W={:.6g} L={:.6g}\n", mosfet.GetName(), nodes[0], nodes[1],
                                   nodes[2], nodes[2], model_name, parameters.Width, parameters.Length);
            AddModelLine(model_name, FormatMOSFETModel(mosfet.GetType(), model_name, parameters), model_names, models);
            break;
        }
        case ComponentType::Ground:
            break;
        }
        netlist += probes;
    }
    netlist += models;
    if (!analysis.empty()) {
        netlist += std::format("{}\n", analysis);
    }
    netlist += ".end\n";
    return netlist;
}

/**
 * @brief   Lists the terminals of a component whose current is measured with a probe.
 * @param[in] component  Any component.
 * @return  The anode of a diode part, every terminal of a transistor, and the first output terminal of a controlled
 *          source or the output of an op-amp; nothing for the others, whose currents ngspice reports directly.
 */
std::vector<CurrentProbe> GetCurrentProbes(const Component &component) {
    const ComponentType type = component.GetType();
    if (IsDiode(type) || IsControlledSource(type) || type == ComponentType::OpAmp) {
        return {{0, ""}};
    }
    if (IsBJT(component.GetType())) {
        return {{0, "C"}, {1, "B"}, {2, "E"}};
    }
    if (IsMOSFET(component.GetType())) {
        return {{0, "D"}, {1, "G"}, {2, "S"}};
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
