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
#include "circuit/components/diac.h"
#include "circuit/components/diode.h"
#include "circuit/components/fuse.h"
#include "circuit/components/igbt.h"
#include "circuit/components/logic_input.h"
#include "circuit/components/logic_part.h"
#include "circuit/components/mosfet.h"
#include "circuit/components/op_amp.h"
#include "circuit/components/potentiometer.h"
#include "circuit/components/relay.h"
#include "circuit/components/source.h"
#include "circuit/components/subcircuit.h"
#include "circuit/components/switch.h"
#include "circuit/components/thyristor.h"
#include "circuit/components/transformer.h"
#include "circuit/components/transmission_line.h"
#include "circuit/components/voltage_controlled_switch.h"
#include "circuit/library_model.h"
#include "circuit/netlist_logic.h"
#include "circuit/netlist_sources.h"
#include "circuit/netlist_subcircuits.h"
#include "circuit/node_voltage_targets.h"
#include "circuit/solver_options.h"
#include "circuit/subcircuit.h"
#include "spice/spice_value.h"
#include <algorithm>
#include <cstddef>
#include <format>
#include <iterator>
#include <map>
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
        return current == name
            || (current.starts_with(name) && current.size() > name.size() && current[name.size()] == '.');
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
std::string ProbeTerminals(const Component &component,
                           const std::vector<CurrentProbe> &probes,
                           std::vector<int> &nodes,
                           int &next_probe_node) {
    std::string lines;
    for (const CurrentProbe &probe : probes) {
        const auto terminal = static_cast<std::size_t>(probe.Terminal);
        lines += std::format("{} {} {} DC 0\n",
                             GetCurrentProbeName(component, probe.Label),
                             nodes[terminal],
                             next_probe_node);
        nodes[terminal] = next_probe_node++;
    }
    return lines;
}

/**
 * @brief   Parts that share a ready model share its line, written the first time the model comes up.
 */
void AddModelLine(std::string spice_name,
                  const std::string &line,
                  std::vector<std::string> &written_names,
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
 * @brief   An instance of the subcircuit the Netlist window shows for an IGBT; see FormatIGBTSubcircuit().
 * @details Its parameters are the channel and the capacitances of its datasheet figures, split as the datasheet gives
 *          them: \f$C_{gc} = C_{res}\f$, \f$C_{ge} = C_{ies} - C_{res}\f$ and \f$C_{ce} = C_{oes} - C_{res}\f$.
 */
std::string FormatIGBT(const IGBT &igbt,
                       const std::vector<int> &nodes,
                       std::vector<std::string> &model_names,
                       std::string &models) {
    const IGBTParameters &figures = igbt.GetParameters();
    AddModelLine(GetIGBTSubcircuitName(), FormatIGBTSubcircuit(), model_names, models);
    return std::format("X-{} {} {} {} {} VTO={} KP={} CGC={} CGE={} CCE={}\n",
                       igbt.GetName(),
                       nodes[0],
                       nodes[1],
                       nodes[2],
                       GetIGBTSubcircuitName(),
                       FormatSpiceValue(figures.GateThresholdVoltage),
                       FormatSpiceValue(GetChannelGain(figures)),
                       FormatSpiceValue(figures.ReverseTransferCapacitance),
                       FormatSpiceValue(figures.InputCapacitance - figures.ReverseTransferCapacitance),
                       FormatSpiceValue(figures.OutputCapacitance - figures.ReverseTransferCapacitance));
}

/**
 * @brief   The line of a diode, a transistor or an IGBT, with the line of its model added to models the first time it
 * comes up.
 * @details A library model is written once, with the other library models, after the parts. The body of a MOSFET is
 *          tied to its source, past its probe, so the source current includes it; a library model brings its own
 *          channel size.
 */
std::string FormatModeledPart(const Component &component,
                              const std::vector<int> &nodes,
                              std::vector<std::string> &model_names,
                              std::string &models) {
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
    if (IsIGBT(type)) {
        return FormatIGBT(static_cast<const IGBT &>(component), nodes, model_names, models);
    }
    const auto &mosfet = static_cast<const MOSFET &>(component);
    const std::string model_name = mosfet.GetSpiceModelName();
    if (mosfet.HasLibraryModel()) {
        return std::format("{} {} {} {} {} {}\n", mosfet.GetName(), nodes[0], nodes[1], nodes[2], nodes[2], model_name);
    }
    const MOSFETParameters &parameters = mosfet.GetParameters();
    AddModelLine(model_name, FormatMOSFETModel(type, model_name, parameters), model_names, models);
    return std::format("{} {} {} {} {} {} W={:.6g} L={:.6g}\n",
                       mosfet.GetName(),
                       nodes[0],
                       nodes[1],
                       nodes[2],
                       nodes[2],
                       model_name,
                       parameters.Width,
                       parameters.Length);
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

/// The netlist option a circuit with a fuse or a thyristor writes: the Gear integration, since the trapezoidal rule
/// rings for ever on an inductor whose current an opened fuse or a released thyristor cuts.
constexpr std::string_view GEAR_INTEGRATION_OPTION = ".options method=gear\n";

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
 * @brief   One ".ic" or ".nodeset" line per node with a marker of the kind; a node two markers give the same voltage is
 *          written once, and CheckCircuit() refuses two different ones.
 * @details A node inside a block is written at the top, named by the instances down to it ("X1.X2.5"), as ngspice
 *          does not take either card inside a ".subckt": one line per instance.
 */
std::string FormatNodeVoltages(const Circuit &circuit, const NodeVoltageKind kind) {
    const std::string_view directive = kind == NodeVoltageKind::INITIAL ? ".ic" : ".nodeset";
    std::map<std::pair<std::vector<std::string>, int>, double> volts_by_node;
    for (const NodeVoltageTarget &target : ResolveNodeVoltages(circuit, kind)) {
        volts_by_node.try_emplace({target.Path, target.Node}, target.Volts);
    }
    std::string text;
    for (const auto &[address, volts] : volts_by_node) {
        const auto &[path, node] = address;
        const std::string name =
            path.empty() ? std::format("{}", node) : std::format("{}.{}", FormatInstancePath(path), node);
        text += std::format("{} V({})={}\n", directive, name, FormatSpiceValue(volts));
    }
    return text;
}

/**
 * @brief   The ".options" line of what the user changed in the solver, or nothing when every option is the default.
 * @details A circuit with a fuse that leaves the method to the program asks for Gear on a line of its own, so an option
 *          line never says the method twice.
 */
std::string FormatSolverOptions(const Circuit &circuit) {
    const SolverOptions &options = circuit.GetSolverOptions();
    std::string line;
    const auto add = [&line](const std::string_view name, const double value, const double default_value) {
        if (value != default_value) {
            line += std::format(" {}={}", name, FormatSpiceValue(value));
        }
    };
    add("reltol", options.RelativeTolerance, DEFAULT_RELATIVE_TOLERANCE);
    add("abstol", options.CurrentToleranceAmperes, DEFAULT_CURRENT_TOLERANCE_AMPERES);
    add("vntol", options.VoltageToleranceVolts, DEFAULT_VOLTAGE_TOLERANCE_VOLTS);
    add("tnom", options.NominalTemperatureCelsius, DEFAULT_NOMINAL_TEMPERATURE_CELSIUS);
    if (options.Method != IntegrationMethod::DEFAULT) {
        line += options.Method == IntegrationMethod::GEAR ? " method=gear" : " method=trap";
    } else if (circuit.HasFuse() || circuit.HasThyristor()) {
        return std::string(GEAR_INTEGRATION_OPTION) + (line.empty() ? "" : ".options" + line + "\n");
    }
    return line.empty() ? std::string() : ".options" + line + "\n";
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
    return std::format("{} {} 0 DC 0 PULSE(0 {} {} {} {} {} {})\n",
                       clock.GetName(),
                       nodes[0],
                       FormatSpiceValue(figures.LogicLevel),
                       FormatSpiceValue(figures.StartDelay),
                       FormatSpiceValue(edge),
                       FormatSpiceValue(edge),
                       FormatSpiceValue(width),
                       FormatSpiceValue(period));
}

/**
 * @brief   ngspice follows the current through a voltage source: the controlling part's own, or its probe.
 */
std::string
FormatCurrentControlledSource(const Circuit &circuit, const ControlledSource &source, const std::vector<int> &nodes) {
    const std::string &current = source.GetControllingCurrent();
    const std::string control = FindControllingSource(circuit, current).value_or(current);
    return std::format("{} {} {} {} {}\n", source.GetName(), nodes[0], nodes[1], control, FormatComponentValue(source));
}

/**
 * @brief   A source the checks refuse still shows in the Netlist window, with its expression as the user wrote it.
 */
std::string
FormatBehavioralSource(const Circuit &circuit, const BehavioralSource &source, const std::vector<int> &nodes) {
    const auto expression = TranslateBehavioralExpression(
        source.GetExpression(),
        [&circuit](const std::string_view net) { return circuit.FindNodeOfNet(net); },
        [&circuit](const std::string_view current) { return FindControllingSource(circuit, current); });
    return std::format("{} {} {} {}={}\n",
                       source.GetName(),
                       nodes[0],
                       nodes[1],
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
    AddModelLine(subcircuit,
                 FormatOpAmpSubcircuit(subcircuit, op_amp.GetParameters()),
                 extras.ModelNames,
                 extras.Models);
    return std::format("X-{} {} {} {} {} {} {}\n",
                       op_amp.GetName(),
                       nodes[0],
                       nodes[1],
                       nodes[2],
                       nodes[3],
                       nodes[4],
                       subcircuit);
}

/**
 * @brief   The wiper, terminal 3, splits the track into two resistors.
 */
std::string FormatPotentiometer(const Potentiometer &pot, const std::vector<int> &nodes) {
    return std::format("R-{}a {} {} {}\n",
                       pot.GetName(),
                       nodes[0],
                       nodes[2],
                       FormatSpiceValue(pot.GetFirstTrackResistance()))
         + std::format("R-{}b {} {} {}\n",
                       pot.GetName(),
                       nodes[2],
                       nodes[1],
                       FormatSpiceValue(pot.GetSecondTrackResistance()));
}

std::string
FormatVoltageSwitch(const VoltageControlledSwitch &controlled, const std::vector<int> &nodes, NetlistExtras &extras) {
    const std::string model_name = std::format("SW-{}", controlled.GetName());
    AddModelLine(model_name,
                 FormatSwitchModel(model_name, controlled.GetParameters()),
                 extras.ModelNames,
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
    AddModelLine(model_name,
                 FormatSwitchModel(model_name, relay.GetContactParameters()),
                 extras.ModelNames,
                 extras.Models);
    return std::format("R-{0}coil {1} {2} {3}\nL-{0}coil {2} {4} {5}\n",
                       relay.GetName(),
                       nodes[2],
                       coil_middle,
                       FormatSpiceValue(figures.CoilResistance),
                       nodes[3],
                       FormatSpiceValue(figures.CoilInductance))
         + std::format("S-{} {} {} {} {} {}\n", relay.GetName(), nodes[0], nodes[1], nodes[2], coil_middle, model_name);
}

/**
 * @brief   A subcircuit that heats with the current through it; see FormatFuseSubcircuit().
 */
std::string FormatFuse(const Fuse &fuse, const std::vector<int> &nodes, NetlistExtras &extras) {
    const FuseParameters &figures = fuse.GetParameters();
    AddModelLine(GetFuseSubcircuitName(), FormatFuseSubcircuit(), extras.ModelNames, extras.Models);
    return std::format("X-{} {} {} {} Ih={} Tau={} Rc={} Rb={}\n",
                       fuse.GetName(),
                       nodes[0],
                       nodes[1],
                       GetFuseSubcircuitName(),
                       FormatSpiceValue(figures.RatedCurrent),
                       FormatSpiceValue(GetCoolingTimeConstant(figures)),
                       FormatSpiceValue(figures.ColdResistance),
                       FormatSpiceValue(figures.BlownResistance));
}

/**
 * @struct  ThyristorSubcircuit
 * @brief   The name and the text of the subcircuit a kind of thyristor is written as.
 */
struct ThyristorSubcircuit {
    const char *Name;
    std::string Text;
};

/**
 * @brief   Picks the subcircuit of a kind of thyristor: the two-transistor pair of an SCR, the anti-parallel pairs of a
 *          TRIAC, or the pair with a turn-off path of a GTO.
 */
ThyristorSubcircuit GetThyristorSubcircuit(const ComponentType type) {
    if (type == ComponentType::TRIAC) {
        return {.Name = GetTriacSubcircuitName(), .Text = FormatTriacSubcircuit()};
    }
    if (type == ComponentType::GTO) {
        return {.Name = GetGtoSubcircuitName(), .Text = FormatGtoSubcircuit()};
    }
    return {.Name = GetThyristorSubcircuitName(), .Text = FormatThyristorSubcircuit()};
}

/**
 * @brief   An instance of the subcircuit of an SCR, a TRIAC or a GTO, which takes the gate and slope resistances; see
 *          FormatThyristorSubcircuit(), FormatTriacSubcircuit() and FormatGtoSubcircuit().
 */
std::string FormatThyristor(const Thyristor &thyristor, const std::vector<int> &nodes, NetlistExtras &extras) {
    const ThyristorParameters &figures = thyristor.GetParameters();
    const ThyristorSubcircuit subcircuit = GetThyristorSubcircuit(thyristor.GetType());
    AddModelLine(subcircuit.Name, subcircuit.Text, extras.ModelNames, extras.Models);
    return std::format("X-{} {} {} {} {} RG={} RT={}\n",
                       thyristor.GetName(),
                       nodes[0],
                       nodes[1],
                       nodes[2],
                       subcircuit.Name,
                       FormatSpiceValue(figures.GateResistance),
                       FormatSpiceValue(figures.OnResistance));
}

/**
 * @brief   An instance of the subcircuit that breaks over either way and holds; see FormatDiacSubcircuit().
 */
std::string FormatDiac(const Diac &diac, const std::vector<int> &nodes, NetlistExtras &extras) {
    const DiacParameters &figures = diac.GetParameters();
    AddModelLine(GetDiacSubcircuitName(), FormatDiacSubcircuit(), extras.ModelNames, extras.Models);
    return std::format("X-{} {} {} {} VBO={} V0={} RT={} IH={} ROFF={}\n",
                       diac.GetName(),
                       nodes[0],
                       nodes[1],
                       GetDiacSubcircuitName(),
                       FormatSpiceValue(figures.BreakoverVoltage),
                       FormatSpiceValue(figures.OnThresholdVoltage),
                       FormatSpiceValue(figures.OnResistance),
                       FormatSpiceValue(figures.HoldingCurrent),
                       FormatSpiceValue(figures.OffResistance));
}

/**
 * @brief   Two inductors, each behind the resistance of its winding, coupled by a K element.
 */
std::string FormatTransformer(const Transformer &transformer, const std::vector<int> &nodes, NetlistExtras &extras) {
    const std::string resistance = FormatSpiceValue(transformer.GetParameters().WindingResistance);
    const int primary_middle = extras.NextExtraNode++;
    const int secondary_middle = extras.NextExtraNode++;
    const std::string &name = transformer.GetName();
    return std::format("R-{0}p {1} {2} {3}\nL-{0}p {2} {4} {5}\n",
                       name,
                       nodes[0],
                       primary_middle,
                       resistance,
                       nodes[1],
                       FormatSpiceValue(transformer.GetValue()))
         + std::format("R-{0}s {1} {2} {3}\nL-{0}s {2} {4} {5}\n",
                       name,
                       nodes[2],
                       secondary_middle,
                       resistance,
                       nodes[3],
                       FormatSpiceValue(transformer.GetSecondaryInductance()))
         + std::format("K-{0} L-{0}p L-{0}s {1}\n", name, FormatSpiceValue(transformer.GetParameters().Coupling));
}

/**
 * @brief   A two-port TEM transmission line card read natively by Nodalys, keeping optional loss parameters omitted
 *          while lossless so standard SPICE readers recognize the `Z0`/`TD` form.
 */
std::string FormatTransmissionLine(const TransmissionLine &line, const std::vector<int> &nodes) {
    const TransmissionLineParameters &figures = line.GetParameters();
    std::string text = std::format("{} {} {} {} {} Z0={} TD={}",
                                   line.GetName(),
                                   nodes[0],
                                   nodes[1],
                                   nodes[2],
                                   nodes[3],
                                   FormatSpiceValue(line.GetValue()),
                                   FormatSpiceValue(figures.Delay));
    if (figures.DcResistance > 0.0) {
        text += std::format(" RDC={}", FormatSpiceValue(figures.DcResistance));
    }
    if (figures.SkinResistance > 0.0) {
        text += std::format(" RSKIN={} FREF={}",
                            FormatSpiceValue(figures.SkinResistance),
                            FormatSpiceValue(figures.ReferenceFrequency));
    }
    if (figures.DcConductance > 0.0) {
        text += std::format(" GDC={}", FormatSpiceValue(figures.DcConductance));
    }
    if (figures.LossTangent > 0.0) {
        text += std::format(" TAND={}", FormatSpiceValue(figures.LossTangent));
    }
    return text + "\n";
}

/**
 * @brief   Resistors, reactive parts, potentiometers and switches, which write a plain value or a fixed resistance.
 */
std::string FormatPassivePart(const Component &component, const std::vector<int> &nodes) {
    switch (component.GetType()) {
    case ComponentType::CAPACITOR:
    case ComponentType::INDUCTOR:
        return FormatReactivePart(component, nodes);
    case ComponentType::POTENTIOMETER:
        return FormatPotentiometer(static_cast<const Potentiometer &>(component), nodes);
    case ComponentType::SWITCH:
    case ComponentType::PUSH_BUTTON:
        return std::format("R-{} {} {} {}\n",
                           component.GetName(),
                           nodes[0],
                           nodes[1],
                           FormatSpiceValue(static_cast<const Switch &>(component).GetResistance()));
    case ComponentType::RESISTOR:
    default:
        return std::format("{} {} {} {}\n", component.GetName(), nodes[0], nodes[1], FormatComponentValue(component));
    }
}

std::string FormatSourcePart(const Component &component, const std::vector<int> &nodes) {
    switch (component.GetType()) {
    case ComponentType::VOLTAGE_SOURCE:
    case ComponentType::CURRENT_SOURCE:
        return FormatSource(static_cast<const Source &>(component), nodes);
    case ComponentType::CLOCK:
        return FormatClock(static_cast<const Clock &>(component), nodes);
    case ComponentType::LOGIC_INPUT:
        // A constant source from its terminal to ground: the logic level when high, 0 V when low
        return std::format("{} {} 0 DC {}\n",
                           component.GetName(),
                           nodes[0],
                           FormatSpiceValue(static_cast<const LogicInput &>(component).GetVoltage()));
    case ComponentType::VCC:
    default:
        return std::format("{} {} 0 DC {}\n", component.GetName(), nodes[0], FormatComponentValue(component));
    }
}

std::string
FormatControlledSourcePart(const Circuit &circuit, const Component &component, const std::vector<int> &nodes) {
    switch (component.GetType()) {
    case ComponentType::CCCS:
    case ComponentType::CCVS:
        return FormatCurrentControlledSource(circuit, static_cast<const ControlledSource &>(component), nodes);
    case ComponentType::VCVS:
    case ComponentType::VCCS:
    default:
        return std::format("{} {} {} {} {} {}\n",
                           component.GetName(),
                           nodes[0],
                           nodes[1],
                           nodes[2],
                           nodes[3],
                           FormatComponentValue(component));
    }
}

std::string FormatSwitchingPart(const Component &component, const std::vector<int> &nodes, NetlistExtras &extras) {
    switch (component.GetType()) {
    case ComponentType::RELAY:
        return FormatRelay(static_cast<const Relay &>(component), nodes, extras);
    case ComponentType::FUSE:
        return FormatFuse(static_cast<const Fuse &>(component), nodes, extras);
    case ComponentType::THYRISTOR:
    case ComponentType::TRIAC:
    case ComponentType::GTO:
        return FormatThyristor(static_cast<const Thyristor &>(component), nodes, extras);
    case ComponentType::DIAC:
        return FormatDiac(static_cast<const Diac &>(component), nodes, extras);
    case ComponentType::V_SWITCH:
    default:
        return FormatVoltageSwitch(static_cast<const VoltageControlledSwitch &>(component), nodes, extras);
    }
}

/**
 * @brief   Gates and flip-flops write their model once, then the elements that build the part from it.
 */
std::string FormatLogicFamilyPart(const Component &component, const std::vector<int> &nodes, NetlistExtras &extras) {
    const auto &logic = static_cast<const LogicPart &>(component);
    extras.Models += FormatLogicModels(logic);
    return FormatLogicPart(logic, nodes, extras.NextExtraNode);
}

/**
 * @brief   The lines of one part, on the nodes its terminals reach, which are the probe nodes where it is probed.
 * @details Ground, labels, pin markers and the parts that only read a node write no line.
 */
std::string
FormatPart(const Circuit &circuit, const Component &component, const std::vector<int> &nodes, NetlistExtras &extras) {
    switch (GetFamily(component.GetType())) {
    case ComponentFamily::PASSIVE:
        return FormatPassivePart(component, nodes);
    case ComponentFamily::SOURCE:
        return FormatSourcePart(component, nodes);
    case ComponentFamily::CONTROLLED_SOURCE:
        return FormatControlledSourcePart(circuit, component, nodes);
    case ComponentFamily::BEHAVIORAL_SOURCE:
        return FormatBehavioralSource(circuit, static_cast<const BehavioralSource &>(component), nodes);
    case ComponentFamily::SEMICONDUCTOR:
        return FormatModeledPart(component, nodes, extras.ModelNames, extras.Models);
    case ComponentFamily::SWITCHING:
        return FormatSwitchingPart(component, nodes, extras);
    case ComponentFamily::TRANSFORMER:
        return FormatTransformer(static_cast<const Transformer &>(component), nodes, extras);
    case ComponentFamily::TRANSMISSION_LINE:
        return FormatTransmissionLine(static_cast<const TransmissionLine &>(component), nodes);
    case ComponentFamily::AMPLIFIER:
        return FormatOpAmpPart(static_cast<const OpAmp &>(component), nodes, extras);
    case ComponentFamily::BLOCK:
        return FormatSubcircuitInstance(static_cast<const SubcircuitInstance &>(component), nodes);
    case ComponentFamily::LOGIC_GATE:
    case ComponentFamily::FLIP_FLOP:
        return FormatLogicFamilyPart(component, nodes, extras);
    case ComponentFamily::MARKER:
    case ComponentFamily::LOGIC_DISPLAY:
    default:
        return {};
    }
}

/// Whether a part only marks or reads its node, so the netlist has no element for it.
bool WritesNoElement(const ComponentType type) {
    const ComponentFamily family = GetFamily(type);
    return family == ComponentFamily::MARKER || family == ComponentFamily::LOGIC_DISPLAY;
}

} // namespace

/**
 * @brief   Writes the parts of a circuit as SPICE lines, with the models they use, and nothing around them.
 * @details This is what both the top netlist and the body of a ".subckt" block share: no title, options, initial
 *          voltages, library models, block definitions or ".end". The top netlist and each block add what belongs to
 *          them around it.
 * @param[in] circuit             Circuit to write.
 * @param[in] add_current_probes  As in WriteSpiceNetlist().
 * @return  One line per component, plus its probes, then one ".model" line per model in use and the subcircuits of
 *          the op-amp macromodels and fuses it needs.
 */
std::string WriteNetlistBody(const Circuit &circuit, const bool add_current_probes) {
    std::string body;
    const std::vector<std::string> controlling_currents = ListControllingCurrents(circuit.GetEntries());
    NetlistExtras extras{.ModelNames = {}, .Models = {}, .NextExtraNode = circuit.GetNodeCount()};
    for (const CircuitEntry &entry : circuit.GetEntries()) {
        const Component &component = *entry.Part;
        // Probed parts connect through the probe nodes; the probe lines follow the part
        std::vector<int> nodes = entry.Nodes;
        const std::string probes =
            ProbeTerminals(component,
                           ProbesFor(component, add_current_probes, CarriesCurrent(component, controlling_currents)),
                           nodes,
                           extras.NextExtraNode);
        body += FormatPart(circuit, component, nodes, extras);
        body += probes;
    }
    body += extras.Models;
    return body;
}

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
 *          uic uses. The initial voltage of a node is one ".ic V(node)=value" line, which ngspice holds the node at
 * while it finds the operating point a transient starts from, and which every other analysis ignores. A nodeset is one
 * ".nodeset V(node)=value" line, the guess ngspice starts every operating point from. The solver options the user
 * changed are one ".options" line with reltol, abstol, vntol, tnom and method. Probes and the
 * gain stage of each ideal op-amp use nodes numbered after GetNodeCount(), so the circuit nodes keep their numbers.
 * Op-amp macromodels are subcircuits, written after the models. A fuse is an instance of a subcircuit written once with
 * the models, and a circuit with a fuse or a thyristor, in a block or not, writes
 *          ".options method=gear" once: the trapezoidal rule rings for ever on an inductor whose current an opened
 *          fuse or a released thyristor cuts. A thyristor is an instance of the imcsim_scr two-transistor subcircuit,
 *          written once with the models too.
 */
std::string WriteSpiceNetlist(const Circuit &circuit, const std::string_view analysis, const bool add_current_probes) {
    std::string netlist = "* imcsim netlist\n";
    netlist += WriteNetlistBody(circuit, add_current_probes);
    netlist += FormatSolverOptions(circuit);
    netlist += FormatNodeVoltages(circuit, NodeVoltageKind::INITIAL);
    netlist += FormatNodeVoltages(circuit, NodeVoltageKind::NODESET);
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
 *          own elements ngspice names after it ("q.x-u1.q1"), "X-FU1" for a fuse subcircuit, "X-Q1" for the shown
 *          subcircuit of an IGBT, and the model, bridges and resistors of a logic part; nothing for ground, net labels,
 *          initial voltages, logic displays and subcircuit pin markers.
 */
std::vector<std::string> GetElementNames(const Component &component) {
    const std::string &name = component.GetName();
    const ComponentType type = component.GetType();
    if (WritesNoElement(type)) {
        return {};
    }
    if (IsLogicGate(type) || IsFlipFlop(type)) {
        return GetLogicElementNames(component);
    }
    if (type == ComponentType::OP_AMP && static_cast<const OpAmp &>(component).IsIdeal()) {
        return {std::format("G-{}", name),
                std::format("R-{}", name),
                std::format("C-{}", name),
                std::format("D-{}-P", name),
                std::format("D-{}-N", name),
                std::format("E-{}", name)};
    }
    if (type == ComponentType::OP_AMP || type == ComponentType::FUSE || IsThyristor(type) || type == ComponentType::DIAC
        || IsIGBT(type)) {
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
        return {std::format("R-{}p", name),
                std::format("L-{}p", name),
                std::format("R-{}s", name),
                std::format("L-{}s", name),
                std::format("K-{}", name)};
    }
    // Every other part is one element named after it
    return {name};
}

} // namespace Core
