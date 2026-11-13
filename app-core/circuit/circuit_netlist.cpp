/**
 * @file    circuit_netlist.cpp
 * @brief   Writes a circuit as a SPICE netlist and names the elements each part becomes in it.
 */

#include "circuit/circuit_netlist.h"

#include "circuit/behavioral_expression.h"
#include "circuit/circuit.h"
#include "circuit/circuit_currents.h"
#include "circuit/components/behavioral_source.h"
#include "circuit/components/bjt.h"
#include "circuit/components/clock.h"
#include "circuit/components/component.h"
#include "circuit/components/controlled_source.h"
#include "circuit/components/diode.h"
#include "circuit/components/fuse.h"
#include "circuit/components/logic_input.h"
#include "circuit/components/logic_part.h"
#include "circuit/components/mosfet.h"
#include "circuit/components/op_amp.h"
#include "circuit/components/potentiometer.h"
#include "circuit/components/relay.h"
#include "circuit/components/source.h"
#include "circuit/components/subcircuit.h"
#include "circuit/components/switch.h"
#include "circuit/components/transformer.h"
#include "circuit/components/voltage_controlled_switch.h"
#include "circuit/library_model.h"
#include "circuit/netlist_logic.h"
#include "circuit/netlist_sources.h"
#include "circuit/netlist_subcircuits.h"
#include "circuit/subcircuit.h"
#include "spice/spice_value.h"
#include <algorithm>
#include <cstddef>
#include <format>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Core {

namespace {

/**
 * @brief   The currents that CCCS and CCVS sources of the circuit follow and that behavioral sources read.
 */
std::vector<std::string> ListControllingCurrents(const std::vector<CircuitEntry> &entries) {
    std::vector<std::string> currents;
    for (const CircuitEntry &entry : entries) {
        if (IsCurrentControlled(entry.Part->GetType())) {
            currents.push_back(static_cast<const ControlledSource &>(*entry.Part).GetControllingCurrent());
        } else if (IsBehavioralSource(entry.Part->GetType())) {
            std::ranges::move(
                ListBehavioralCurrents(static_cast<const BehavioralSource &>(*entry.Part).GetExpression()),
                std::back_inserter(currents));
        }
    }
    return currents;
}

/**
 * @brief   A current names its part, alone or followed by "." and a terminal label.
 */
bool CarriesCurrent(const Component &component, const std::vector<std::string> &currents) {
    const std::string &name = component.GetName();
    return !name.empty() && std::ranges::any_of(currents, [&name](const std::string &current) {
        return current == name ||
               (current.starts_with(name) && current.size() > name.size() && current[name.size()] == '.');
    });
}

/**
 * @brief   The probes of a part: those it always reports, when probes are asked for or when one of its currents
 *          controls a source; a part without probes of its own gets one on its first terminal when it controls a
 *          source, unless it has a branch current already.
 */
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

/**
 * @brief   Moves every probed terminal onto a node of its own, joined to its circuit node by a 0 V source whose current
 *          flows into the terminal, and returns the lines of those sources.
 */
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

/**
 * @brief   Parts that share a ready model share its line, written the first time the model comes up.
 */
void AddModelLine(std::string spice_name, const std::string &line, std::vector<std::string> &written_names,
                  std::string &models) {
    if (std::ranges::contains(written_names, spice_name)) {
        return;
    }
    models += line;
    written_names.push_back(std::move(spice_name));
}

std::string FormatComponentValue(const Component &component) {
    if (const auto &expression = component.GetValueExpression()) {
        return *expression;
    }
    return FormatSpiceValue(component.GetValue());
}

/**
 * @brief   The line of a diode or a transistor, with the line of its model added to models the first time it comes up.
 * @details A library model is written once, with the other library models, after the parts. The body of a MOSFET is
 *          tied to its source, past its probe, so the source current includes it; a library model brings its own
 *          channel size.
 */
std::string FormatModeledPart(const Component &component, const std::vector<int> &nodes,
                              std::vector<std::string> &model_names, std::string &models) {
    const ComponentType type = component.GetType();
    if (IsDiode(type)) {
        const auto &diode = static_cast<const Diode &>(component);
        const std::string model_name = diode.GetSpiceModelName();
        if (!diode.HasLibraryModel()) {
            AddModelLine(model_name, FormatDiodeModel(model_name, diode.GetParameters()), model_names, models);
        }
        return std::format("{} {} {} {}\n", diode.GetName(), nodes[0], nodes[1], model_name);
    }
    if (IsBJT(type)) {
        const auto &bjt = static_cast<const BJT &>(component);
        const std::string model_name = bjt.GetSpiceModelName();
        if (!bjt.HasLibraryModel()) {
            AddModelLine(model_name, FormatBJTModel(type, model_name, bjt.GetParameters()), model_names, models);
        }
        return std::format("{} {} {} {} {}\n", bjt.GetName(), nodes[0], nodes[1], nodes[2], model_name);
    }
    const auto &mosfet = static_cast<const MOSFET &>(component);
    const std::string model_name = mosfet.GetSpiceModelName();
    if (mosfet.HasLibraryModel()) {
        return std::format("{} {} {} {} {} {}\n", mosfet.GetName(), nodes[0], nodes[1], nodes[2], nodes[2], model_name);
    }
    const MOSFETParameters &parameters = mosfet.GetParameters();
    AddModelLine(model_name, FormatMOSFETModel(type, model_name, parameters), model_names, models);
    return std::format("{} {} {} {} {} {} W={:.6g} L={:.6g}\n", mosfet.GetName(), nodes[0], nodes[1], nodes[2],
                       nodes[2], model_name, parameters.Width, parameters.Length);
}

/**
 * @brief   Every library model of a circuit, under the names the parts use, after the models of the parts.
 */
std::string FormatLibraryModels(const std::vector<LibraryModel> &library_models) {
    std::string text;
    for (const LibraryModel &model : library_models) {
        text += FormatLibraryModel(model);
    }
    return text;
}

} // namespace

/**
 * @brief   Writes the circuit as a SPICE netlist, ready to hand to ngspice.
 * @param[in] circuit             Circuit to write.
 * @param[in] analysis            Optional analysis command, such as ".op", written right before ".end".
 * @param[in] add_current_probes  Adds a 0 V source in series with every terminal GetCurrentProbes() lists, named
 *                                by GetCurrentProbeName(), whose branch current is the terminal current; for
 *                                simulation only.
 * @return  One line per component, plus its probes, with node numbers as SPICE node names (0 is ground), then one
 *          ".model" line per model in use and one ".subckt" block per project-local definition, ending in ".end".
 *          Subcircuit instances use X lines, with their pins in definition order and their parameter overrides.
 * @note    Ground components produce no line; they only make their node 0. A supply rail becomes a DC voltage source
 *          from its node to ground. A part whose current controls a CCCS or CCVS gets a probe even without
 *          add_current_probes, unless it is a voltage source itself, since ngspice follows currents through voltage
 *          sources. An AC source writes its offset, AC magnitude and sine together, and every other waveform its
 *          starting value and its shape, so the same netlist works for .op, .ac and .tran. A clock writes a pulse whose
 *          rise and fall times are a fixed share of its period, and a logic input a DC level, both from their terminal
 *          to ground. A capacitor or inductor with an initial condition writes it as IC=, which only a transient with
 *          uic uses. Probes and the gain stage of each ideal op-amp use nodes numbered after GetNodeCount(), so the
 *          circuit nodes keep their numbers. Op-amp macromodels are subcircuits, written after the models. A fuse is an
 *          instance of a subcircuit written once with the models, and a circuit with a fuse, in a block or not, writes
 *          ".options method=gear" once.
 */
std::string WriteSpiceNetlist(const Circuit &circuit, const std::string_view analysis, const bool add_current_probes) {
    std::string netlist = "* imcsim netlist\n";
    const std::vector<std::string> controlling_currents = ListControllingCurrents(circuit.GetEntries());
    std::vector<std::string> model_names;
    std::string models;
    int next_extra_node = circuit.GetNodeCount();
    for (const CircuitEntry &entry : circuit.GetEntries()) {
        const Component &component = *entry.Part;
        const std::string value = FormatComponentValue(component);
        // Probed parts connect through the probe nodes; the probe lines follow the part
        std::vector<int> nodes = entry.Nodes;
        const std::string probes = ProbeTerminals(
            component, ProbesFor(component, add_current_probes, CarriesCurrent(component, controlling_currents)), nodes,
            next_extra_node);
        switch (component.GetType()) {
        case ComponentType::RESISTOR:
            netlist += std::format("{} {} {} {}\n", component.GetName(), nodes[0], nodes[1], value);
            break;
        case ComponentType::CAPACITOR:
        case ComponentType::INDUCTOR: {
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
        case ComponentType::VOLTAGE_SOURCE:
        case ComponentType::CURRENT_SOURCE:
            netlist += FormatSource(static_cast<const Source &>(component), nodes);
            break;
        case ComponentType::CLOCK: {
            // The part fixes its own rise and fall times, a small share of the period, so the user never sets them
            const auto &clock = static_cast<const Clock &>(component);
            const ClockParameters &figures = clock.GetParameters();
            const double period = 1.0 / figures.Frequency;
            const double edge = CLOCK_EDGE_FRACTION * period;
            // Half of each edge lies above half the level, so the flat top is a whole edge shorter than the duty share
            const double width = std::max(figures.DutyCycle * period - edge, 0.0);
            netlist += std::format("{} {} 0 DC 0 PULSE(0 {} {} {} {} {} {})\n", component.GetName(), nodes[0],
                                   FormatSpiceValue(figures.LogicLevel), FormatSpiceValue(figures.StartDelay),
                                   FormatSpiceValue(edge), FormatSpiceValue(edge), FormatSpiceValue(width),
                                   FormatSpiceValue(period));
            break;
        }
        case ComponentType::LOGIC_INPUT: {
            // A constant source from its terminal to ground: the logic level when high, 0 V when low
            const auto &input = static_cast<const LogicInput &>(component);
            netlist +=
                std::format("{} {} 0 DC {}\n", component.GetName(), nodes[0], FormatSpiceValue(input.GetVoltage()));
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
            const std::string control = FindControllingSource(circuit, current).value_or(current);
            netlist += std::format("{} {} {} {} {}\n", component.GetName(), nodes[0], nodes[1], control, value);
            break;
        }
        case ComponentType::BEHAVIORAL_VOLTAGE:
        case ComponentType::BEHAVIORAL_CURRENT: {
            const auto &source = static_cast<const BehavioralSource &>(component);
            const auto expression = TranslateBehavioralExpression(
                source.GetExpression(), [&circuit](const std::string_view net) { return circuit.FindNodeOfNet(net); },
                [&circuit](const std::string_view current) { return FindControllingSource(circuit, current); });
            // A source the checks refuse still shows in the Netlist window, as the user wrote it
            netlist += std::format("{} {} {} {}={}\n", component.GetName(), nodes[0], nodes[1],
                                   component.GetType() == ComponentType::BEHAVIORAL_VOLTAGE ? "V" : "I",
                                   expression.value_or(source.GetExpression()));
            break;
        }
        case ComponentType::SUBCIRCUIT: {
            const auto &instance = static_cast<const SubcircuitInstance &>(component);
            netlist += component.GetName();
            for (const int node : nodes) {
                netlist += std::format(" {}", node);
            }
            netlist += std::format(" {}", instance.GetDefinitionName());
            for (const auto &[name, parameter] : instance.GetParameters()) {
                netlist += std::format(" {}={}", name, FormatSpiceValue(parameter));
            }
            netlist += "\n";
            break;
        }
        case ComponentType::OP_AMP: {
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
        case ComponentType::DIODE:
        case ComponentType::ZENER_DIODE:
        case ComponentType::LED:
        case ComponentType::NPN:
        case ComponentType::PNP:
        case ComponentType::NMOS:
        case ComponentType::PMOS:
            netlist += FormatModeledPart(component, nodes, model_names, models);
            break;
        case ComponentType::POTENTIOMETER: {
            // The wiper, terminal 3, splits the track into two resistors
            const auto &pot = static_cast<const Potentiometer &>(component);
            netlist += std::format("R-{}a {} {} {}\n", pot.GetName(), nodes[0], nodes[2],
                                   FormatSpiceValue(pot.GetFirstTrackResistance()));
            netlist += std::format("R-{}b {} {} {}\n", pot.GetName(), nodes[2], nodes[1],
                                   FormatSpiceValue(pot.GetSecondTrackResistance()));
            break;
        }
        case ComponentType::SWITCH:
        case ComponentType::PUSH_BUTTON: {
            const auto &contact = static_cast<const Switch &>(component);
            netlist += std::format("R-{} {} {} {}\n", component.GetName(), nodes[0], nodes[1],
                                   FormatSpiceValue(contact.GetResistance()));
            break;
        }
        case ComponentType::V_SWITCH: {
            const auto &controlled = static_cast<const VoltageControlledSwitch &>(component);
            const std::string model_name = std::format("SW-{}", controlled.GetName());
            netlist += std::format("{} {} {} {} {} {}\n", controlled.GetName(), nodes[0], nodes[1], nodes[2], nodes[3],
                                   model_name);
            AddModelLine(model_name, FormatSwitchModel(model_name, controlled.GetParameters()), model_names, models);
            break;
        }
        case ComponentType::AND:
        case ComponentType::OR:
        case ComponentType::NOT:
        case ComponentType::NAND:
        case ComponentType::NOR:
        case ComponentType::XOR:
        case ComponentType::XNOR:
        case ComponentType::BUFFER:
        case ComponentType::D_FLIP_FLOP:
        case ComponentType::JK_FLIP_FLOP:
        case ComponentType::T_FLIP_FLOP:
        case ComponentType::SR_FLIP_FLOP: {
            const auto &logic = static_cast<const LogicPart &>(component);
            netlist += FormatLogicPart(logic, nodes, next_extra_node);
            models += FormatLogicModels(logic);
            break;
        }
        case ComponentType::RELAY: {
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
        case ComponentType::FUSE: {
            // A subcircuit that heats with the current through it; see FormatFuseSubcircuit()
            const auto &fuse = static_cast<const Fuse &>(component);
            const FuseParameters &figures = fuse.GetParameters();
            netlist += std::format("X-{} {} {} {} Ih={} Tau={} Rc={} Rb={}\n", fuse.GetName(), nodes[0], nodes[1],
                                   GetFuseSubcircuitName(), FormatSpiceValue(figures.RatedCurrent),
                                   FormatSpiceValue(GetCoolingTimeConstant(figures)),
                                   FormatSpiceValue(figures.ColdResistance), FormatSpiceValue(figures.BlownResistance));
            AddModelLine(GetFuseSubcircuitName(), FormatFuseSubcircuit(), model_names, models);
            break;
        }
        case ComponentType::TRANSFORMER: {
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
        case ComponentType::GROUND:
        case ComponentType::NET_LABEL:
        case ComponentType::PIN:
        case ComponentType::LOGIC_OUTPUT:
        case ComponentType::SEVEN_SEGMENT:
            break;
        }
        netlist += probes;
    }
    netlist += models;
    if (circuit.HasFuse()) {
        netlist += FUSE_INTEGRATION_OPTION;
    }
    netlist += FormatLibraryModels(circuit.GetLibraryModels());
    for (const SubcircuitDefinition *definition : OrderByUse(circuit.GetSubcircuits())) {
        netlist += FormatSubcircuit(*definition);
    }
    if (!analysis.empty()) {
        netlist += std::format("{}\n", analysis);
    }
    netlist += ".end\n";
    return netlist;
}

/**
 * @brief   Lists the elements ToSpiceNetlist() writes for a component, without its current probes.
 * @param[in] component  Any component.
 * @return  The component name for a part that is one element, such as "R1" or "Q1"; the elements of a part written
 *          as several, such as "R-RV1a" and "R-RV1b" for a potentiometer or "X-U1" for an op-amp macromodel, whose
 *          own elements ngspice names after it ("q.x-u1.q1"), "X-FU1" for a fuse subcircuit, and the model, bridges and
 * resistors of a logic part; nothing for ground, net labels, logic displays and subcircuit pin markers.
 */
std::vector<std::string> GetElementNames(const Component &component) {
    const std::string &name = component.GetName();
    const ComponentType type = component.GetType();
    if (type == ComponentType::GROUND || type == ComponentType::NET_LABEL || IsLogicDisplay(type) ||
        IsSubcircuitPin(type)) {
        return {};
    }
    if (IsLogicGate(type) || IsFlipFlop(type)) {
        return GetLogicElementNames(component);
    }
    if (type == ComponentType::OP_AMP && static_cast<const OpAmp &>(component).IsIdeal()) {
        return {std::format("G-{}", name),   std::format("R-{}", name),   std::format("C-{}", name),
                std::format("D-{}-P", name), std::format("D-{}-N", name), std::format("E-{}", name)};
    }
    if (type == ComponentType::OP_AMP || type == ComponentType::FUSE) {
        return {std::format("X-{}", name)};
    }
    if (type == ComponentType::POTENTIOMETER) {
        return {std::format("R-{}a", name), std::format("R-{}b", name)};
    }
    if (IsSwitch(type)) {
        return {std::format("R-{}", name)};
    }
    if (type == ComponentType::RELAY) {
        return {std::format("R-{}coil", name), std::format("L-{}coil", name), std::format("S-{}", name)};
    }
    if (type == ComponentType::TRANSFORMER) {
        return {std::format("R-{}p", name), std::format("L-{}p", name), std::format("R-{}s", name),
                std::format("L-{}s", name), std::format("K-{}", name)};
    }
    // Every other part is one element named after it
    return {name};
}

} // namespace Core
