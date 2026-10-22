/**
 * @file    circuit.cpp
 * @brief   Circuit topology handed to the simulator: components and the nodes their terminals connect to.
 */

#include "circuit.h"

#include "components/bjt.h"
#include "components/clock.h"
#include "components/controlled_source.h"
#include "components/diode.h"
#include "components/logic_input.h"
#include "components/logic_part.h"
#include "components/mosfet.h"
#include "components/op_amp.h"
#include "components/potentiometer.h"
#include "components/relay.h"
#include "components/source.h"
#include "components/switch.h"
#include "components/transformer.h"
#include "components/voltage_controlled_switch.h"
#include "spice_value.h"
#include <algorithm>
#include <format>
#include <iterator>
#include <utility>

namespace Core {

namespace {

bool IsACSource(const Component &component) {
    return IsSource(component.GetType()) &&
           static_cast<const Source &>(component).GetSourceType() == Source::SourceType::AC;
}

// The two halves of a potentiometer track never get to 0 Ohm, which SPICE rejects, however close to an end the wiper is
constexpr double MinTrackResistance = 1e-3;

// A switch is a resistor, which closed is nearly a wire and open nearly nothing at all
constexpr double ClosedSwitchResistance = 1e-3;
constexpr double OpenSwitchResistance = 1e12;

// One line serves every analysis: .op reads the DC value, .ac the AC magnitude and .tran the waveform. Every
// waveform writes the value it starts from, so the operating point matches the beginning of the transient. The name
// tells SPICE whether it is a voltage or current source
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
    case Source::SourceType::EXP: {
        const EXPParameters &exp = source.GetEXP();
        const std::string initial = FormatSpiceValue(exp.InitialValue);
        return std::format("{} DC {} EXP({} {} {} {} {} {})\n", start, initial, initial,
                           FormatSpiceValue(exp.PeakValue), FormatSpiceValue(exp.RiseDelay),
                           FormatSpiceValue(exp.RiseTimeConstant), FormatSpiceValue(exp.FallDelay),
                           FormatSpiceValue(exp.FallTimeConstant));
    }
    case Source::SourceType::SFFM: {
        const SFFMParameters &sffm = source.GetSFFM();
        const std::string offset = FormatSpiceValue(sffm.Offset);
        return std::format("{} DC {} SFFM({} {} {} {} {})\n", start, offset, offset, FormatSpiceValue(sffm.Amplitude),
                           FormatSpiceValue(sffm.CarrierFrequency), FormatSpiceValue(sffm.ModulationIndex),
                           FormatSpiceValue(sffm.SignalFrequency));
    }
    case Source::SourceType::AM: {
        const AMParameters &am = source.GetAM();
        const std::string offset = FormatSpiceValue(am.Offset);
        // ngspice reads AM(offset mod_offset amplitude mod_frequency carrier_frequency delay); the modulation offset
        // stays 0, so the source is the offset plus the amplitude times the two sines
        return std::format("{} DC {} AM({} 0 {} {} {} {})\n", start, offset, offset, FormatSpiceValue(am.Amplitude),
                           FormatSpiceValue(am.ModulatingFrequency), FormatSpiceValue(am.CarrierFrequency),
                           FormatSpiceValue(am.Delay));
    }
    case Source::SourceType::PWL: {
        const PWLParameters &pwl = source.GetPWL();
        std::string points;
        for (const PWLPoint &point : pwl.Points) {
            if (!points.empty()) {
                points += " ";
            }
            points += std::format("{} {}", FormatSpiceValue(point.Time), FormatSpiceValue(point.Value));
        }
        const std::string first = FormatSpiceValue(pwl.Points.front().Value);
        return std::format("{} DC {} PWL({})\n", start, first, points);
    }
    }
    return "";
}

// Supplies and voltage sources carry their own branch current, which a CCCS or CCVS can follow directly
bool HasBranchCurrent(const ComponentType type) {
    return type == ComponentType::VCC || type == ComponentType::VoltageSource || type == ComponentType::Clock ||
           type == ComponentType::LogicInput;
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

// A gate or flip-flop is a digital code model, which only meets digital nodes: each input reaches it through an
// analog-to-digital bridge, and each output leaves through a digital-to-analog bridge and a resistor, the output
// resistance of the family. Every input also gets a resistor to ground, so one left unconnected reads 0
constexpr double LogicOutputResistance = 50.0;
constexpr double LogicInputPullDown = 1e9;

std::string FormatLogicModels(const LogicPart &part) {
    const LogicParameters &family = part.GetParameters();
    const std::string name = part.GetName();
    const std::string delay = FormatSpiceValue(family.PropagationDelay);
    std::string models = std::format(".model ADC-{} adc_bridge(in_low={} in_high={})\n", name,
                                     FormatSpiceValue(family.LogicLevel * LogicLowFraction),
                                     FormatSpiceValue(family.LogicLevel * LogicHighFraction));
    models += std::format(".model DAC-{} dac_bridge(out_low=0 out_high={} out_undef={})\n", name,
                          FormatSpiceValue(family.LogicLevel), FormatSpiceValue(family.LogicLevel / 2.0));
    if (IsFlipFlop(part.GetType())) {
        models +=
            std::format(".model LOGIC-{} {}(clk_delay={} set_delay={} reset_delay={} rise_delay={} fall_delay={})\n",
                        name, GetLogicModelKind(part.GetType()), delay, delay, delay, delay, delay);
    } else {
        models += std::format(".model LOGIC-{} {}(rise_delay={} fall_delay={})\n", name,
                              GetLogicModelKind(part.GetType()), delay, delay);
    }
    return models;
}

// The terminal nodes are in the order of GetInputLabels() and GetLogicOutputLabels(); the digital nodes and the
// analog nodes behind the output resistors are numbered from next_extra_node
std::string FormatLogicPart(const LogicPart &part, const std::vector<int> &nodes, int &next_extra_node) {
    const std::string &name = part.GetName();
    const std::vector<std::string> inputs = part.GetInputLabels();
    const std::vector<std::string> outputs = GetLogicOutputLabels(part.GetType());
    std::string lines;
    std::vector<int> digital_inputs;
    for (std::size_t input = 0; input < inputs.size(); ++input) {
        const int digital = next_extra_node++;
        digital_inputs.push_back(digital);
        lines += std::format("A-{0}-{1} [{2}] [{3}] ADC-{0}\n", name, inputs[input], nodes[input], digital);
        lines +=
            std::format("R-{}-{} {} 0 {}\n", name, inputs[input], nodes[input], FormatSpiceValue(LogicInputPullDown));
    }
    std::vector<int> digital_outputs;
    for (std::size_t output = 0; output < outputs.size(); ++output) {
        digital_outputs.push_back(next_extra_node++);
    }
    // A single-input gate takes its input as a scalar, the others as a vector
    std::string ports;
    if (IsLogicGate(part.GetType()) && inputs.size() == 1) {
        ports = std::to_string(digital_inputs[0]);
    } else if (IsLogicGate(part.GetType())) {
        ports = "[";
        for (const int digital : digital_inputs) {
            ports += std::format("{} ", digital);
        }
        ports.back() = ']';
    } else {
        for (const int digital : digital_inputs) {
            ports += std::format("{} ", digital);
        }
        ports.pop_back();
    }
    for (const int digital : digital_outputs) {
        ports += std::format(" {}", digital);
    }
    lines += std::format("A-{0} {1} LOGIC-{0}\n", name, ports);
    for (std::size_t output = 0; output < outputs.size(); ++output) {
        const int analog = next_extra_node++;
        lines += std::format("A-{0}-{1} [{2}] [{3}] DAC-{0}\n", name, outputs[output], digital_outputs[output], analog);
        lines += std::format("R-{}-{} {} {} {}\n", name, outputs[output], analog, nodes[inputs.size() + output],
                             FormatSpiceValue(LogicOutputResistance));
    }
    return lines;
}

// The model of a logic part, and a bridge and a resistor on every pin, as FormatLogicPart() writes them
std::vector<std::string> GetLogicElementNames(const Component &part) {
    const std::string &name = part.GetName();
    std::vector<std::string> names{std::format("A-{}", name)};
    std::vector<std::string> pins = static_cast<const LogicPart &>(part).GetInputLabels();
    std::ranges::move(GetLogicOutputLabels(part.GetType()), std::back_inserter(pins));
    for (const std::string &pin : pins) {
        names.push_back(std::format("A-{}-{}", name, pin));
        names.push_back(std::format("R-{}-{}", name, pin));
    }
    return names;
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
 * @brief   Tells whether any part starts from an initial condition.
 * @return  True when at least one capacitor or inductor has one set; a transient then runs with uic.
 */
bool Circuit::HasInitialConditions() const {
    return std::ranges::any_of(m_Entries, [](const CircuitEntry &entry) { return entry.Part->HasInitialCondition(); });
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
 *          voltage sources. An AC source writes its offset, AC magnitude and sine together, and every other
 *          waveform its starting value and its shape, so the same netlist works for .op, .ac and .tran. A clock
 *          writes a pulse whose rise and fall times are a fixed share of its period, and a logic input a DC level,
 *          both from their terminal to ground. A capacitor or inductor with an initial condition writes it as IC=,
 * which only a transient with uic uses. Probes and the gain stage of each ideal op-amp use nodes numbered after
 * GetNodeCount(), so the circuit nodes keep their numbers. Op-amp macromodels are subcircuits, written after the
 * models.
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
            netlist += std::format("{} {} {} {}\n", component.GetName(), nodes[0], nodes[1], value);
            break;
        case ComponentType::Capacitor:
        case ComponentType::Inductor: {
            // A capacitor's initial condition is a voltage and an inductor's a current; ngspice reads both as IC=
            // and only uses them in a transient with uic
            netlist += std::format("{} {} {} {}", component.GetName(), nodes[0], nodes[1], value);
            if (const std::optional<double> initial = component.GetInitialCondition()) {
                netlist += std::format(" IC={}", FormatSpiceValue(*initial));
            }
            netlist += "\n";
            break;
        }
        case ComponentType::VCC:
            netlist += std::format("{} {} 0 DC {}\n", component.GetName(), nodes[0], value);
            break;
        case ComponentType::VoltageSource:
        case ComponentType::CurrentSource:
            netlist += FormatSource(static_cast<const Source &>(component), nodes);
            break;
        case ComponentType::Clock: {
            // The part fixes its own rise and fall times, a small share of the period, so the user never sets them
            const auto &clock = static_cast<const Clock &>(component);
            const ClockParameters &figures = clock.GetParameters();
            const double period = 1.0 / figures.Frequency;
            const double edge = ClockEdgeFraction * period;
            // Half of each edge lies above half the level, so the flat top is a whole edge shorter than the duty share
            const double width = std::max(figures.DutyCycle * period - edge, 0.0);
            netlist += std::format("{} {} 0 DC 0 PULSE(0 {} {} {} {} {} {})\n", component.GetName(), nodes[0],
                                   FormatSpiceValue(figures.LogicLevel), FormatSpiceValue(figures.StartDelay),
                                   FormatSpiceValue(edge), FormatSpiceValue(edge), FormatSpiceValue(width),
                                   FormatSpiceValue(period));
            break;
        }
        case ComponentType::LogicInput: {
            // A constant source from its terminal to ground: the logic level when high, 0 V when low
            const auto &input = static_cast<const LogicInput &>(component);
            const double level = input.IsHigh() ? input.GetParameters().LogicLevel : 0.0;
            netlist += std::format("{} {} 0 DC {}\n", component.GetName(), nodes[0], FormatSpiceValue(level));
            break;
        }
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
        case ComponentType::Potentiometer: {
            // The wiper, terminal 3, splits the track into two resistors
            const auto &pot = static_cast<const Potentiometer &>(component);
            const std::string first = FormatSpiceValue(std::max(pot.GetValue() * pot.GetWiper(), MinTrackResistance));
            const std::string second =
                FormatSpiceValue(std::max(pot.GetValue() * (1.0 - pot.GetWiper()), MinTrackResistance));
            netlist += std::format("R-{}a {} {} {}\n", pot.GetName(), nodes[0], nodes[2], first);
            netlist += std::format("R-{}b {} {} {}\n", pot.GetName(), nodes[2], nodes[1], second);
            break;
        }
        case ComponentType::Switch:
        case ComponentType::PushButton: {
            const bool closed = static_cast<const Switch &>(component).IsClosed();
            netlist += std::format("R-{} {} {} {}\n", component.GetName(), nodes[0], nodes[1],
                                   FormatSpiceValue(closed ? ClosedSwitchResistance : OpenSwitchResistance));
            break;
        }
        case ComponentType::VSwitch: {
            const auto &controlled = static_cast<const VoltageControlledSwitch &>(component);
            const std::string model_name = std::format("SW-{}", controlled.GetName());
            netlist += std::format("{} {} {} {} {} {}\n", controlled.GetName(), nodes[0], nodes[1], nodes[2], nodes[3],
                                   model_name);
            AddModelLine(model_name, FormatSwitchModel(model_name, controlled.GetParameters()), model_names, models);
            break;
        }
        case ComponentType::And:
        case ComponentType::Or:
        case ComponentType::Not:
        case ComponentType::Nand:
        case ComponentType::Nor:
        case ComponentType::Xor:
        case ComponentType::Xnor:
        case ComponentType::Buffer:
        case ComponentType::DFlipFlop:
        case ComponentType::JKFlipFlop:
        case ComponentType::TFlipFlop:
        case ComponentType::SRFlipFlop: {
            const auto &logic = static_cast<const LogicPart &>(component);
            netlist += FormatLogicPart(logic, nodes, next_extra_node);
            models += FormatLogicModels(logic);
            break;
        }
        case ComponentType::Relay: {
            // The coil is its resistance and its inductance in series, and the contact follows the voltage on the
            // resistance, which is the coil current, so it closes and opens as the current builds and dies away
            const auto &relay = static_cast<const Relay &>(component);
            const RelayParameters &figures = relay.GetParameters();
            const int coil_middle = next_extra_node++;
            const std::string model_name = std::format("SW-{}", relay.GetName());
            netlist += std::format("R-{0}coil {1} {2} {3}\nL-{0}coil {2} {4} {5}\n", relay.GetName(), nodes[2],
                                   coil_middle, FormatSpiceValue(figures.CoilResistance), nodes[3],
                                   FormatSpiceValue(figures.CoilInductance));
            netlist += std::format("S-{} {} {} {} {} {}\n", relay.GetName(), nodes[0], nodes[1], nodes[2], coil_middle,
                                   model_name);
            AddModelLine(model_name, FormatSwitchModel(model_name, relay.GetContactParameters()), model_names, models);
            break;
        }
        case ComponentType::Transformer: {
            // Two inductors, each behind the resistance of its winding, coupled by a K element
            const auto &transformer = static_cast<const Transformer &>(component);
            const std::string resistance = FormatSpiceValue(transformer.GetParameters().WindingResistance);
            const int primary_middle = next_extra_node++;
            const int secondary_middle = next_extra_node++;
            const std::string &name = transformer.GetName();
            netlist += std::format("R-{0}p {1} {2} {3}\nL-{0}p {2} {4} {5}\n", name, nodes[0], primary_middle,
                                   resistance, nodes[1], FormatSpiceValue(transformer.GetValue()));
            netlist += std::format("R-{0}s {1} {2} {3}\nL-{0}s {2} {4} {5}\n", name, nodes[2], secondary_middle,
                                   resistance, nodes[3], FormatSpiceValue(transformer.GetSecondaryInductance()));
            netlist +=
                std::format("K-{0} L-{0}p L-{0}s {1}\n", name, FormatSpiceValue(transformer.GetParameters().Coupling));
            break;
        }
        case ComponentType::Ground:
        case ComponentType::NetLabel:
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
 * @return  The anode of a diode part, the first terminal of a switch, every terminal of a transistor or a
 *          potentiometer, the contact and the coil of a relay, the two windings of a transformer, and the first output
 * terminal of a controlled source or the output of an op-amp; nothing for the others, whose currents ngspice reports
 * directly.
 */
std::vector<CurrentProbe> GetCurrentProbes(const Component &component) {
    const ComponentType type = component.GetType();
    if (IsDiode(type) || IsControlledSource(type) || IsSwitch(type) || type == ComponentType::VSwitch ||
        type == ComponentType::OpAmp) {
        return {{0, ""}};
    }
    if (IsLogicGate(type)) {
        return {{static_cast<const LogicPart &>(component).GetInputCount(), ""}};
    }
    if (IsFlipFlop(type)) {
        const auto outputs = static_cast<int>(GetLogicInputLabels(type).size());
        return {{outputs, "Q"}, {outputs + 1, "QN"}};
    }
    if (type == ComponentType::Relay) {
        return {{0, "S"}, {2, "C"}};
    }
    if (type == ComponentType::Transformer) {
        return {{0, "P"}, {2, "S"}};
    }
    if (IsBJT(component.GetType())) {
        return {{0, "C"}, {1, "B"}, {2, "E"}};
    }
    if (IsMOSFET(component.GetType())) {
        return {{0, "D"}, {1, "G"}, {2, "S"}};
    }
    if (component.GetType() == ComponentType::Potentiometer) {
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

/**
 * @brief   Lists the elements ToSpiceNetlist() writes for a component, without its current probes.
 * @param[in] component  Any component.
 * @return  The component name for a part that is one element, such as "R1" or "Q1"; the elements of a part written
 *          as several, such as "R-RV1a" and "R-RV1b" for a potentiometer or "X-U1" for an op-amp macromodel, whose
 *          own elements ngspice names after it ("q.x-u1.q1"), and the model, bridges and resistors of a logic part;
 *          nothing for ground and net labels.
 */
std::vector<std::string> GetElementNames(const Component &component) {
    const std::string &name = component.GetName();
    const ComponentType type = component.GetType();
    if (type == ComponentType::Ground || type == ComponentType::NetLabel) {
        return {};
    }
    if (IsLogicGate(type) || IsFlipFlop(type)) {
        return GetLogicElementNames(component);
    }
    if (type == ComponentType::OpAmp && static_cast<const OpAmp &>(component).IsIdeal()) {
        return {std::format("G-{}", name),   std::format("R-{}", name),   std::format("C-{}", name),
                std::format("D-{}-P", name), std::format("D-{}-N", name), std::format("E-{}", name)};
    }
    if (type == ComponentType::OpAmp) {
        return {std::format("X-{}", name)};
    }
    if (type == ComponentType::Potentiometer) {
        return {std::format("R-{}a", name), std::format("R-{}b", name)};
    }
    if (IsSwitch(type)) {
        return {std::format("R-{}", name)};
    }
    if (type == ComponentType::Relay) {
        return {std::format("R-{}coil", name), std::format("L-{}coil", name), std::format("S-{}", name)};
    }
    if (type == ComponentType::Transformer) {
        return {std::format("R-{}p", name), std::format("L-{}p", name), std::format("R-{}s", name),
                std::format("L-{}s", name), std::format("K-{}", name)};
    }
    // Every other part is one element named after it
    return {name};
}

} // namespace Core
