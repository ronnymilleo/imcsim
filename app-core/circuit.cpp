/**
 * @file    circuit.cpp
 * @brief   Circuit topology handed to the simulator: components and the nodes their terminals connect to.
 */

#include "circuit.h"

#include "components/voltage_source.h"
#include "spice_value.h"
#include <algorithm>
#include <format>
#include <utility>

namespace Core {

namespace {

bool IsACSource(const Component &component) {
    return component.GetType() == ComponentType::VoltageSource &&
           static_cast<const VoltageSource &>(component).GetSourceType() == VoltageSource::SourceType::AC;
}

// One line serves every analysis: .op reads the DC value, .ac the AC magnitude and .tran the waveform. A pulse
// starts at its low level, so that is its DC value
std::string FormatVoltageSource(const VoltageSource &source, const std::vector<int> &nodes) {
    const std::string start = std::format("{} {} {}", source.GetName(), nodes[0], nodes[1]);
    switch (source.GetSourceType()) {
    case VoltageSource::SourceType::DC:
        return std::format("{} DC {}\n", start, FormatSpiceValue(source.GetValue()));
    case VoltageSource::SourceType::AC: {
        const std::string offset = FormatSpiceValue(source.GetOffset());
        const std::string amplitude = FormatSpiceValue(source.GetAmplitude());
        return std::format("{} DC {} AC {} SIN({} {} {})\n", start, offset, amplitude, offset, amplitude,
                           FormatSpiceValue(source.GetFrequency()));
    }
    case VoltageSource::SourceType::Pulse: {
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
 * @return  True when it contains at least one voltage source set to AC.
 */
bool Circuit::HasACSource() const {
    return std::ranges::any_of(m_Entries, [](const CircuitEntry &entry) { return IsACSource(*entry.Part); });
}

/**
 * @brief   Writes the circuit as a SPICE netlist, ready to hand to ngspice.
 * @param[in] analysis  Optional analysis command, such as ".op", written right before ".end".
 * @return  One line per component, with node numbers as SPICE node names (0 is ground), ending in ".end".
 * @note    Ground components produce no line; they only make their node 0. A supply rail becomes a DC voltage
 *          source from its node to ground. An AC source writes its offset, AC magnitude and sine together, and a
 *          pulse source its low level and pulse, so the same netlist works for .op, .ac and .tran.
 */
std::string Circuit::ToSpiceNetlist(const std::string_view analysis) const {
    std::string netlist = "* imcsim netlist\n";
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
            netlist += FormatVoltageSource(static_cast<const VoltageSource &>(component), entry.Nodes);
            break;
        case ComponentType::Ground:
            break;
        }
    }
    if (!analysis.empty()) {
        netlist += std::format("{}\n", analysis);
    }
    netlist += ".end\n";
    return netlist;
}

} // namespace Core
