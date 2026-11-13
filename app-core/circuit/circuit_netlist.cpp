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

/**
 * @struct  NetlistExtras
 * @brief   What the lines of the parts add to a netlist beside themselves: the models they use, each written once,
 *          and the nodes they take past the ones of the circuit.
 */
struct NetlistExtras {
    std::vector<std::string> ModelNames;
    std::string Models;
    int NextExtraNode = 0;
};

/**
 * @brief   A capacitor's initial condition is a voltage and an inductor's a current; ngspice reads both as IC= and
 *          only uses them in a transient with uic.
 */
std::string FormatReactivePart(const Component &component, const std::vector<int> &nodes) {
    std::string line =
        std::format("{} {} {} {}", component.GetName(), nodes[0], nodes[1], FormatComponentValue(component));
    if (const std::optional<double> initial = component.GetInitialCondition()) {
        line += std::format(" IC={}", FormatSpiceValue(*initial));
    }
    return line + "\n";
}

/**
 * @brief   A pulse from the terminal to ground. The part fixes its own rise and fall times, a small share of the
 *          period, so the user never sets them.
 */
std::string FormatClock(const Clock &clock, const std::vector<int> &nodes) {
    const ClockParameters &figures = clock.GetParameters();
    const double period = 1.0 / figures.Frequency;
    const double edge = CLOCK_EDGE_FRACTION * period;
    // Half of each edge lies above half the level, so the flat top is a whole edge shorter than the duty share
    const double width = std::max(figures.DutyCycle * period - edge, 0.0);
    return std::format("{} {} 0 DC 0 PULSE(0 {} {} {} {} {} {})\n", clock.GetName(), nodes[0],
                       FormatSpiceValue(figures.LogicLevel), FormatSpiceValue(figures.StartDelay),
                       FormatSpiceValue(edge), FormatSpiceValue(edge), FormatSpiceValue(width),
                       FormatSpiceValue(period));
}

/**
 * @brief   ngspice follows the current through a voltage source: the controlling part's own, or its probe.
 */
std::string FormatCurrentControlledSource(const Circuit &circuit, const ControlledSource &source,
                                          const std::vector<int> &nodes) {
    const std::string &current = source.GetControllingCurrent();
    const std::string control = FindControllingSource(circuit, current).value_or(current);
    return std::format("{} {} {} {} {}\n", source.GetName(), nodes[0], nodes[1], control, FormatComponentValue(source));
}

/**
 * @brief   A source the checks refuse still shows in the Netlist window, with its expression as the user wrote it.
 */
std::string FormatBehavioralSource(const Circuit &circuit, const BehavioralSource &source,
                                   const std::vector<int> &nodes) {
    const auto expression = TranslateBehavioralExpression(
        source.GetExpression(), [&circuit](const std::string_view net) { return circuit.FindNodeOfNet(net); },
        [&circuit](const std::string_view current) { return FindControllingSource(circuit, current); });
    return std::format("{} {} {} {}={}\n", source.GetName(), nodes[0], nodes[1],
                       source.GetType() == ComponentType::BEHAVIORAL_VOLTAGE ? "V" : "I",
                       expression.value_or(source.GetExpression()));
}

std::string FormatSubcircuitInstance(const SubcircuitInstance &instance, const std::vector<int> &nodes) {
    std::string line = instance.GetName();
    for (const int node : nodes) {
        line += std::format(" {}", node);
    }
    line += std::format(" {}", instance.GetDefinitionName());
    for (const auto &[name, parameter] : instance.GetParameters()) {
        line += std::format(" {}={}", name, FormatSpiceValue(parameter));
    }
    return line + "\n";
}

/**
 * @brief   The ideal op-amp takes one extra node for its gain stage; a macromodel is a subcircuit, written once for
 *          every op-amp of its model.
 */
std::string FormatOpAmpPart(const OpAmp &op_amp, const std::vector<int> &nodes, NetlistExtras &extras) {
    if (op_amp.IsIdeal()) {
        AddModelLine(GetIdealOpAmpModelName(), FormatIdealOpAmpModel(), extras.ModelNames, extras.Models);
        return FormatIdealOpAmp(op_amp, nodes, extras.NextExtraNode++);
    }
    const std::string subcircuit = op_amp.GetSpiceModelName();
    AddModelLine(subcircuit, FormatOpAmpSubcircuit(subcircuit, op_amp.GetParameters()), extras.ModelNames,
                 extras.Models);
    return std::format("X-{} {} {} {} {} {} {}\n", op_amp.GetName(), nodes[0], nodes[1], nodes[2], nodes[3], nodes[4],
                       subcircuit);
}

/**
 * @brief   The wiper, terminal 3, splits the track into two resistors.
 */
std::string FormatPotentiometer(const Potentiometer &pot, const std::vector<int> &nodes) {
    return std::format("R-{}a {} {} {}\n", pot.GetName(), nodes[0], nodes[2],
                       FormatSpiceValue(pot.GetFirstTrackResistance())) +
           std::format("R-{}b {} {} {}\n", pot.GetName(), nodes[2], nodes[1],
                       FormatSpiceValue(pot.GetSecondTrackResistance()));
}

std::string FormatVoltageSwitch(const VoltageControlledSwitch &controlled, const std::vector<int> &nodes,
                                NetlistExtras &extras) {
    const std::string model_name = std::format("SW-{}", controlled.GetName());
    AddModelLine(model_name, FormatSwitchModel(model_name, controlled.GetParameters()), extras.ModelNames,
                 extras.Models);
    return std::format("{} {} {} {} {} {}\n", controlled.GetName(), nodes[0], nodes[1], nodes[2], nodes[3], model_name);
}

/**
 * @brief   The coil is its resistance and its inductance in series, and the contact follows the voltage on the
 *          resistance, which is the coil current, so it closes and opens as the current builds and dies away.
 */
std::string FormatRelay(const Relay &relay, const std::vector<int> &nodes, NetlistExtras &extras) {
    const RelayParameters &figures = relay.GetParameters();
    const int coil_middle = extras.NextExtraNode++;
    const std::string model_name = std::format("SW-{}", relay.GetName());
    AddModelLine(model_name, FormatSwitchModel(model_name, relay.GetContactParameters()), extras.ModelNames,
                 extras.Models);
    return std::format("R-{0}coil {1} {2} {3}\nL-{0}coil {2} {4} {5}\n", relay.GetName(), nodes[2], coil_middle,
                       FormatSpiceValue(figures.CoilResistance), nodes[3], FormatSpiceValue(figures.CoilInductance)) +
           std::format("S-{} {} {} {} {} {}\n", relay.GetName(), nodes[0], nodes[1], nodes[2], coil_middle, model_name);
}

/**
 * @brief   A subcircuit that heats with the current through it; see FormatFuseSubcircuit().
 */
std::string FormatFuse(const Fuse &fuse, const std::vector<int> &nodes, NetlistExtras &extras) {
    const FuseParameters &figures = fuse.GetParameters();
    AddModelLine(GetFuseSubcircuitName(), FormatFuseSubcircuit(), extras.ModelNames, extras.Models);
    return std::format("X-{} {} {} {} Ih={} Tau={} Rc={} Rb={}\n", fuse.GetName(), nodes[0], nodes[1],
                       GetFuseSubcircuitName(), FormatSpiceValue(figures.RatedCurrent),
                       FormatSpiceValue(GetCoolingTimeConstant(figures)), FormatSpiceValue(figures.ColdResistance),
                       FormatSpiceValue(figures.BlownResistance));
}

/**
 * @brief   Two inductors, each behind the resistance of its winding, coupled by a K element.
 */
std::string FormatTransformer(const Transformer &transformer, const std::vector<int> &nodes, NetlistExtras &extras) {
    const std::string resistance = FormatSpiceValue(transformer.GetParameters().WindingResistance);
    const int primary_middle = extras.NextExtraNode++;
    const int secondary_middle = extras.NextExtraNode++;
    const std::string &name = transformer.GetName();
    return std::format("R-{0}p {1} {2} {3}\nL-{0}p {2} {4} {5}\n", name, nodes[0], primary_middle, resistance, nodes[1],
                       FormatSpiceValue(transformer.GetValue())) +
           std::format("R-{0}s {1} {2} {3}\nL-{0}s {2} {4} {5}\n", name, nodes[2], secondary_middle, resistance,
                       nodes[3], FormatSpiceValue(transformer.GetSecondaryInductance())) +
           std::format("K-{0} L-{0}p L-{0}s {1}\n", name, FormatSpiceValue(transformer.GetParameters().Coupling));
}

/**
 * @brief   The lines of one part, on the nodes its terminals reach, which are the probe nodes where it is probed.
 * @details Ground, labels, pin markers and the parts that only read a node write no line.
 */
std::string FormatPart(const Circuit &circuit, const Component &component, const std::vector<int> &nodes,
                       NetlistExtras &extras) {
    const std::string &name = component.GetName();
    switch (component.GetType()) {
    case ComponentType::RESISTOR:
        return std::format("{} {} {} {}\n", name, nodes[0], nodes[1], FormatComponentValue(component));
    case ComponentType::CAPACITOR:
    case ComponentType::INDUCTOR:
        return FormatReactivePart(component, nodes);
    case ComponentType::VCC:
        return std::format("{} {} 0 DC {}\n", name, nodes[0], FormatComponentValue(component));
    case ComponentType::VOLTAGE_SOURCE:
    case ComponentType::CURRENT_SOURCE:
        return FormatSource(static_cast<const Source &>(component), nodes);
    case ComponentType::CLOCK:
        return FormatClock(static_cast<const Clock &>(component), nodes);
    case ComponentType::LOGIC_INPUT:
        // A constant source from its terminal to ground: the logic level when high, 0 V when low
        return std::format("{} {} 0 DC {}\n", name, nodes[0],
                           FormatSpiceValue(static_cast<const LogicInput &>(component).GetVoltage()));
    case ComponentType::VCVS:
    case ComponentType::VCCS:
        return std::format("{} {} {} {} {} {}\n", name, nodes[0], nodes[1], nodes[2], nodes[3],
                           FormatComponentValue(component));
    case ComponentType::CCCS:
    case ComponentType::CCVS:
        return FormatCurrentControlledSource(circuit, static_cast<const ControlledSource &>(component), nodes);
    case ComponentType::BEHAVIORAL_VOLTAGE:
    case ComponentType::BEHAVIORAL_CURRENT:
        return FormatBehavioralSource(circuit, static_cast<const BehavioralSource &>(component), nodes);
    case ComponentType::SUBCIRCUIT:
        return FormatSubcircuitInstance(static_cast<const SubcircuitInstance &>(component), nodes);
    case ComponentType::OP_AMP:
        return FormatOpAmpPart(static_cast<const OpAmp &>(component), nodes, extras);
    case ComponentType::DIODE:
    case ComponentType::ZENER_DIODE:
    case ComponentType::LED:
    case ComponentType::NPN:
    case ComponentType::PNP:
    case ComponentType::NMOS:
    case ComponentType::PMOS:
        return FormatModeledPart(component, nodes, extras.ModelNames, extras.Models);
    case ComponentType::POTENTIOMETER:
        return FormatPotentiometer(static_cast<const Potentiometer &>(component), nodes);
    case ComponentType::SWITCH:
    case ComponentType::PUSH_BUTTON:
        return std::format("R-{} {} {} {}\n", name, nodes[0], nodes[1],
                           FormatSpiceValue(static_cast<const Switch &>(component).GetResistance()));
    case ComponentType::V_SWITCH:
        return FormatVoltageSwitch(static_cast<const VoltageControlledSwitch &>(component), nodes, extras);
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
        extras.Models += FormatLogicModels(logic);
        return FormatLogicPart(logic, nodes, extras.NextExtraNode);
    }
    case ComponentType::RELAY:
        return FormatRelay(static_cast<const Relay &>(component), nodes, extras);
    case ComponentType::FUSE:
        return FormatFuse(static_cast<const Fuse &>(component), nodes, extras);
    case ComponentType::TRANSFORMER:
        return FormatTransformer(static_cast<const Transformer &>(component), nodes, extras);
    case ComponentType::GROUND:
    case ComponentType::NET_LABEL:
    case ComponentType::PIN:
    case ComponentType::LOGIC_OUTPUT:
    case ComponentType::SEVEN_SEGMENT:
    default:
        return {};
    }
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
    NetlistExtras extras{.ModelNames = {}, .Models = {}, .NextExtraNode = circuit.GetNodeCount()};
    for (const CircuitEntry &entry : circuit.GetEntries()) {
        const Component &component = *entry.Part;
        // Probed parts connect through the probe nodes; the probe lines follow the part
        std::vector<int> nodes = entry.Nodes;
        const std::string probes = ProbeTerminals(
            component, ProbesFor(component, add_current_probes, CarriesCurrent(component, controlling_currents)), nodes,
            extras.NextExtraNode);
        netlist += FormatPart(circuit, component, nodes, extras);
        netlist += probes;
    }
    netlist += extras.Models;
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
