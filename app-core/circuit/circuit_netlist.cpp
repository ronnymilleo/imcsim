/**
 * @file    circuit_netlist.cpp
 * @brief   Writes a circuit as a SPICE netlist, and lists the netlist elements of a part.
 * @details Defines Circuit::ToSpiceNetlist() and GetElementNames() of circuit.h.
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

namespace {

/**
 * @brief   The arguments of TRNOISE, with the parts that are off written as 0: ngspice ignores a source with no time
 *          step and no telegraph amplitude, and takes the step only for the white and 1/f noise.
 */
std::string FormatNoiseArguments(const NoiseParameters &noise) {
    const bool is_flicker_on = noise.FlickerAmplitude != 0.0;
    const bool is_telegraph_on = noise.TelegraphAmplitude != 0.0;
    const bool needs_step = noise.WhiteAmplitude != 0.0 || is_flicker_on;
    return std::format("{} {} {} {} {} {} {}", FormatSpiceValue(noise.WhiteAmplitude),
                       FormatSpiceValue(needs_step ? noise.TimeStep : 0.0),
                       FormatSpiceValue(is_flicker_on ? noise.FlickerExponent : 0.0),
                       FormatSpiceValue(noise.FlickerAmplitude), FormatSpiceValue(noise.TelegraphAmplitude),
                       FormatSpiceValue(is_telegraph_on ? noise.CaptureTime : 0.0),
                       FormatSpiceValue(is_telegraph_on ? noise.EmissionTime : 0.0));
}

/**
 * @brief   One line serves every analysis: .op reads the DC value, .ac the AC magnitude and .tran the waveform.
 * @details Every waveform writes the value it starts from, so the operating point matches the beginning of the
 *          transient. The name tells SPICE whether it is a voltage or current source.
 */
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
    case Source::SourceType::PULSE: {
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
    case Source::SourceType::NOISE:
        return std::format("{} DC {} TRNOISE({})\n", start, FormatSpiceValue(source.GetNoise().Offset),
                           FormatNoiseArguments(source.GetNoise()));
    case Source::SourceType::RANDOM: {
        const RandomParameters &random = source.GetRandom();
        // ngspice adds the DC value to what TRRANDOM gives, so the offset or mean is written once, as the DC value,
        // which also makes it the operating point; the offset of TRRANDOM itself stays 0
        return std::format("{} DC {} TRRANDOM({} {} {} {} 0)\n", start, FormatSpiceValue(random.Second),
                           static_cast<int>(random.Distribution), FormatSpiceValue(random.TimeStep),
                           FormatSpiceValue(random.Delay), FormatSpiceValue(random.First));
    }
    default:
        return "";
    }
}

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

/// Output resistance of the logic family, in series with the digital-to-analog bridge of every output of a gate or
/// flip-flop, whose digital code model only meets digital nodes.
constexpr double LOGIC_OUTPUT_RESISTANCE = 50.0;
/// Resistance from every logic input to ground, so an input left unconnected reads 0.
constexpr double LOGIC_INPUT_PULL_DOWN = 1e9;

std::string FormatLogicModels(const LogicPart &part) {
    const LogicParameters &family = part.GetParameters();
    const std::string name = part.GetName();
    const std::string delay = FormatSpiceValue(family.PropagationDelay);
    std::string models = std::format(".model ADC-{} adc_bridge(in_low={} in_high={})\n", name,
                                     FormatSpiceValue(family.LogicLevel * LOGIC_LOW_FRACTION),
                                     FormatSpiceValue(family.LogicLevel * LOGIC_HIGH_FRACTION));
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

/**
 * @brief   The terminal nodes are in the order of GetInputLabels() and GetLogicOutputLabels(); the digital nodes and
 *          the analog nodes behind the output resistors are numbered from next_extra_node.
 */
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
        lines += std::format("R-{}-{} {} 0 {}\n", name, inputs[input], nodes[input],
                             FormatSpiceValue(LOGIC_INPUT_PULL_DOWN));
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
                             FormatSpiceValue(LOGIC_OUTPUT_RESISTANCE));
    }
    return lines;
}

/**
 * @brief   The model of a logic part, and a bridge and a resistor on every pin, as FormatLogicPart() writes them.
 */
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

/// A fuse needs the Gear integration, since the trapezoidal rule rings for ever on an inductor that an open fuse cuts.
constexpr std::string_view FUSE_INTEGRATION_OPTION = ".options method=gear\n";

bool IsModelNameCharacter(const char character) {
    return std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '_' || character == '-';
}

std::string ToLowerCase(const std::string_view text) {
    std::string lower(text);
    std::ranges::transform(lower, lower.begin(),
                           [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return lower;
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
 * @brief   The names a body declares with .model or .subckt, lowercase, since SPICE reads names without regard to case.
 */
std::set<std::string> CollectDeclaredNames(const std::string &body) {
    std::set<std::string> declared;
    std::istringstream lines(body);
    std::string line;
    while (std::getline(lines, line)) {
        std::istringstream fields(line);
        std::string directive;
        std::string name;
        if (!(fields >> directive >> name)) {
            continue;
        }
        directive = ToLowerCase(directive);
        if (directive == ".model" || directive == ".subckt") {
            declared.insert(ToLowerCase(name));
        }
    }
    return declared;
}

/**
 * @brief   One line with the declared names prefixed.
 * @details A comment stays as it is, and the first word of an element line is left alone: it is the element's own name,
 *          whose first letter says what the element is.
 */
std::string ScopeLine(const std::string_view line, const std::set<std::string> &declared, const std::string &prefix) {
    const std::size_t first = line.find_first_not_of(" \t");
    if (first == std::string_view::npos || line[first] == '*') {
        return std::string(line);
    }
    bool is_first_word = line[first] != '.' && line[first] != '+';
    std::string scoped;
    for (std::size_t position = 0; position < line.size();) {
        if (!IsModelNameCharacter(line[position])) {
            scoped += line[position++];
            continue;
        }
        const std::size_t start = position;
        while (position < line.size() && IsModelNameCharacter(line[position])) {
            ++position;
        }
        const std::string token(line.substr(start, position - start));
        const bool is_element_name = std::exchange(is_first_word, false);
        scoped += !is_element_name && declared.contains(ToLowerCase(token)) ? prefix + token : token;
    }
    return scoped;
}

/**
 * @brief   Model names are global in ngspice, even when their declarations sit inside a subcircuit.
 * @details Prefix every declaration and reference of a definition so equal custom parts in two definitions cannot
 *          replace each other's models.
 */
std::string ScopeModels(const std::string &body, const std::string_view definition_name) {
    const std::set<std::string> declared = CollectDeclaredNames(body);
    const std::string prefix = std::format("imcsim_{}_", definition_name);
    std::string scoped;
    scoped.reserve(body.size() + declared.size() * prefix.size());
    std::size_t start = 0;
    while (start < body.size()) {
        const std::size_t end = body.find('\n', start);
        const std::size_t length = end == std::string::npos ? std::string::npos : end - start;
        scoped += ScopeLine(std::string_view(body).substr(start, length), declared, prefix);
        if (end == std::string::npos) {
            break;
        }
        scoped += '\n';
        start = end + 1;
    }
    return scoped;
}

/**
 * @brief   A drawn block lists the nodes of its pins and writes its body as a netlist; a block kept as SPICE text lists
 *          the ports of its .subckt line and writes its text as it came.
 */
std::string FormatSubcircuit(const SubcircuitDefinition &definition) {
    std::string text = std::format(".subckt {}", definition.Name);
    if (definition.Spice) {
        for (const std::string &port : definition.Spice->Ports) {
            text += std::format(" {}", port);
        }
    } else {
        for (const SubcircuitPin &pin : definition.Pins) {
            text += std::format(" {}", pin.Node);
        }
    }
    if (!definition.Parameters.empty()) {
        text += " params:";
        for (const SubcircuitParameter &parameter : definition.Parameters) {
            text += std::format(" {}={}", parameter.Name, FormatSpiceValue(parameter.DefaultValue));
        }
    }
    text += "\n";
    std::string body;
    if (definition.Spice) {
        body = definition.Spice->Text;
        if (!body.empty() && !body.ends_with('\n')) {
            body += '\n';
        }
    } else {
        // A pin that no part inside uses still owns its node, which the extra nodes of the body must not take
        Circuit numbered_body = definition.Body;
        for (const SubcircuitPin &pin : definition.Pins) {
            numbered_body.ReserveNode(pin.Node);
        }
        body = numbered_body.ToSpiceNetlist();
        body.erase(0, body.find('\n') + 1);
        if (body.ends_with(".end\n")) {
            body.erase(body.size() - 5);
        }
        // Options are global and the root netlist writes them once, for the fuses of the blocks too
        if (const std::size_t option = body.find(FUSE_INTEGRATION_OPTION); option != std::string::npos) {
            body.erase(option, FUSE_INTEGRATION_OPTION.size());
        }
    }
    text += ScopeModels(body, definition.Name);
    text += std::format(".ends {}\n", definition.Name);
    return text;
}

/**
 * @brief   The definitions in an order that writes each one after the blocks its body places, as a reader of the
 *          netlist meets them; the order of the list stays among the others, and a cycle, which the checks refuse, ends
 *          the walk.
 */
std::vector<const SubcircuitDefinition *> OrderByUse(const std::vector<std::shared_ptr<SubcircuitDefinition>> &list) {
    std::vector<const SubcircuitDefinition *> order;
    std::set<std::string, std::less<>> visited;
    const auto place = [&](const auto &self, const SubcircuitDefinition &definition) -> void {
        if (!visited.insert(definition.Name).second) {
            return;
        }
        for (const CircuitEntry &entry : definition.Body.GetEntries()) {
            if (entry.Part->GetType() != ComponentType::SUBCIRCUIT) {
                continue;
            }
            const std::string &name = static_cast<const SubcircuitInstance &>(*entry.Part).GetDefinitionName();
            if (const auto inner = std::ranges::find(list, name, &SubcircuitDefinition::Name); inner != list.end()) {
                self(self, **inner);
            }
        }
        order.push_back(&definition);
    };
    for (const auto &definition : list) {
        place(place, *definition);
    }
    return order;
}

} // namespace

/**
 * @brief   Writes the circuit as a SPICE netlist, ready to hand to ngspice.
 * @param[in] analysis            Optional analysis command, such as ".op", written right before ".end".
 * @param[in] add_current_probes  Adds a 0 V source in series with every terminal GetCurrentProbes() lists, named
 *                                by GetCurrentProbeName(), whose branch current is the terminal current; for
 *                                simulation only.
 * @return  One line per component, plus its probes, with node numbers as SPICE node names (0 is ground), then one
 *          ".model" line per model in use and one ".subckt" block per project-local definition, ending in ".end".
 *          Subcircuit instances use X lines, with their pins in definition order and their parameter overrides.
 * @note    Ground components produce no line; they only make their node 0. A supply rail becomes a DC voltage
 *          source from its node to ground. A part whose current controls a CCCS or CCVS gets a probe even without
 *          add_current_probes, unless it is a voltage source itself, since ngspice follows currents through
 *          voltage sources. An AC source writes its offset, AC magnitude and sine together, and every other
 *          waveform its starting value and its shape, so the same netlist works for .op, .ac and .tran. A clock
 *          writes a pulse whose rise and fall times are a fixed share of its period, and a logic input a DC level,
 *          both from their terminal to ground. A capacitor or inductor with an initial condition writes it as IC=,
 * which only a transient with uic uses. Probes and the gain stage of each ideal op-amp use nodes numbered after
 * GetNodeCount(), so the circuit nodes keep their numbers. Op-amp macromodels are subcircuits, written after the
 * models. A fuse is an instance of a subcircuit written once with the models, and a circuit with a fuse, in a block or
 * not, writes ".options method=gear" once.
 */
std::string Circuit::ToSpiceNetlist(const std::string_view analysis, const bool add_current_probes) const {
    std::string netlist = "* imcsim netlist\n";
    const std::vector<std::string> controlling_currents = ListControllingCurrents(m_Entries);
    std::vector<std::string> model_names;
    std::string models;
    int next_extra_node = m_NodeCount;
    for (const CircuitEntry &entry : m_Entries) {
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
            const std::string control = FindControllingSource(*this, current).value_or(current);
            netlist += std::format("{} {} {} {} {}\n", component.GetName(), nodes[0], nodes[1], control, value);
            break;
        }
        case ComponentType::BEHAVIORAL_VOLTAGE:
        case ComponentType::BEHAVIORAL_CURRENT: {
            const auto &source = static_cast<const BehavioralSource &>(component);
            const auto expression = TranslateBehavioralExpression(
                source.GetExpression(), [this](const std::string_view net) { return FindNodeOfNet(net); },
                [this](const std::string_view current) { return FindControllingSource(*this, current); });
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
    if (HasFuse()) {
        netlist += FUSE_INTEGRATION_OPTION;
    }
    netlist += FormatLibraryModels(m_LibraryModels);
    for (const SubcircuitDefinition *definition : OrderByUse(m_Subcircuits)) {
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
