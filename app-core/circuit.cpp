/**
 * @file    circuit.cpp
 * @brief   Circuit topology handed to the simulator: components and the nodes their terminals connect to.
 */

#include "circuit.h"

#include "components/diode.h"
#include "components/source.h"
#include "spice_value.h"
#include <algorithm>
#include <format>
#include <optional>
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

// With a probe, a 0 V source in series measures the diode current; ngspice saves no usable diode current in .ac
std::string FormatDiode(const Diode &diode, const std::vector<int> &nodes, const std::optional<int> probe_node) {
    if (!probe_node) {
        return std::format("{} {} {} {}\n", diode.GetName(), nodes[0], nodes[1], diode.GetSpiceModelName());
    }
    return std::format("{} {} {} {}\n{} {} {} DC 0\n", diode.GetName(), nodes[0], *probe_node,
                       diode.GetSpiceModelName(), GetCurrentProbeName(diode), *probe_node, nodes[1]);
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
 * @param[in] add_current_probes  Adds a 0 V source in series with every diode, named by GetCurrentProbeName(),
 *                                whose branch current is the diode current; for simulation only.
 * @return  One line per component, with node numbers as SPICE node names (0 is ground), then one ".model" line
 *          per diode model in use, ending in ".end".
 * @note    Ground components produce no line; they only make their node 0. A supply rail becomes a DC voltage
 *          source from its node to ground. An AC source writes its offset, AC magnitude and sine together, and a
 *          pulse source its low level and pulse, so the same netlist works for .op, .ac and .tran. Probes connect
 *          through nodes numbered after GetNodeCount(), so the circuit nodes keep their numbers.
 */
std::string Circuit::ToSpiceNetlist(const std::string_view analysis, const bool add_current_probes) const {
    std::string netlist = "* imcsim netlist\n";
    // Diodes that share a ready model share its line
    std::vector<std::string> model_names;
    std::string models;
    int next_probe_node = m_NodeCount;
    for (const CircuitEntry &entry : m_Entries) {
        const Component &component = *entry.Part;
        const std::string value = FormatSpiceValue(component.GetValue());
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
            const std::optional<int> probe_node =
                add_current_probes ? std::optional<int>(next_probe_node++) : std::nullopt;
            netlist += FormatDiode(diode, entry.Nodes, probe_node);
            if (std::string model_name = diode.GetSpiceModelName();
                std::ranges::find(model_names, model_name) == model_names.end()) {
                models += FormatDiodeModel(model_name, diode.GetParameters());
                model_names.push_back(std::move(model_name));
            }
            break;
        }
        case ComponentType::Ground:
            break;
        }
    }
    netlist += models;
    if (!analysis.empty()) {
        netlist += std::format("{}\n", analysis);
    }
    netlist += ".end\n";
    return netlist;
}

/**
 * @brief   Returns the name of the 0 V source that measures the current of a diode in a netlist with probes.
 * @param[in] diode  Diode the probe is in series with.
 * @return  "Vprobe-" followed by the diode name; the dash keeps it apart from any name IsValidName() accepts.
 */
std::string GetCurrentProbeName(const Component &diode) {
    return std::format("Vprobe-{}", diode.GetName());
}

} // namespace Core
