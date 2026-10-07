/**
 * @file    circuit.cpp
 * @brief   Circuit topology handed to the simulator: components and the nodes their terminals connect to.
 */

#include "circuit.h"

#include "components/bjt.h"
#include "components/diode.h"
#include "components/mosfet.h"
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
        const std::string amplitude = FormatSpiceValue(ac.Amplitude);
        return std::format("{} DC {} AC {} SIN({} {} {})\n", start, offset, amplitude, offset, amplitude,
                           FormatSpiceValue(ac.Frequency));
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

// Moves every probed terminal onto a node of its own, joined to its circuit node by a 0 V source whose current
// flows into the terminal, and returns the lines of those sources
std::string ProbeTerminals(const Component &component, std::vector<int> &nodes, int &next_probe_node) {
    std::string lines;
    for (const CurrentProbe &probe : GetCurrentProbes(component)) {
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
 *          source from its node to ground. An AC source writes its offset, AC magnitude and sine together, and a
 *          pulse source its low level and pulse, so the same netlist works for .op, .ac and .tran. Probes connect
 *          through nodes numbered after GetNodeCount(), so the circuit nodes keep their numbers.
 */
std::string Circuit::ToSpiceNetlist(const std::string_view analysis, const bool add_current_probes) const {
    std::string netlist = "* imcsim netlist\n";
    std::vector<std::string> model_names;
    std::string models;
    int next_probe_node = m_NodeCount;
    for (const CircuitEntry &entry : m_Entries) {
        const Component &component = *entry.Part;
        const std::string value = FormatSpiceValue(component.GetValue());
        // Probed parts connect through the probe nodes; the probe lines follow the part
        std::vector<int> nodes = entry.Nodes;
        const std::string probes = add_current_probes ? ProbeTerminals(component, nodes, next_probe_node) : "";
        switch (component.GetType()) {
        case ComponentType::Resistor:
        case ComponentType::Capacitor:
        case ComponentType::Inductor:
            netlist += std::format("{} {} {} {}\n", component.GetName(), entry.Nodes[0], entry.Nodes[1], value);
            break;
        case ComponentType::VCC:
            netlist += std::format("{} {} 0 DC {}\n", component.GetName(), entry.Nodes[0], value);
            break;
        case ComponentType::VoltageSource:
        case ComponentType::CurrentSource:
            netlist += FormatSource(static_cast<const Source &>(component), entry.Nodes);
            break;
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
 * @return  The anode of a diode part, and every terminal of a transistor; nothing for the others, whose currents
 *          ngspice reports directly.
 */
std::vector<CurrentProbe> GetCurrentProbes(const Component &component) {
    if (IsDiode(component.GetType())) {
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

} // namespace Core
