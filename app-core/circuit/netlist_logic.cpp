/**
 * @file    netlist_logic.cpp
 * @brief   Writes the netlist lines of a logic part: its digital model, its bridges and the elements they make.
 */

#include "circuit/netlist_logic.h"

#include "circuit/circuit_currents.h"
#include "circuit/circuit_netlist.h"
#include "circuit/components/bjt.h"
#include "circuit/components/clock.h"
#include "circuit/components/component.h"
#include "circuit/components/diode.h"
#include "circuit/components/fuse.h"
#include "circuit/components/logic_input.h"
#include "circuit/components/logic_part.h"
#include "circuit/components/mosfet.h"
#include "circuit/components/op_amp.h"
#include "circuit/components/relay.h"
#include "circuit/components/subcircuit.h"
#include "circuit/components/transformer.h"
#include "circuit/components/voltage_controlled_switch.h"
#include "circuit/netlist_subcircuits.h"
#include "spice/spice_value.h"
#include <algorithm>
#include <cstddef>
#include <format>
#include <iterator>
#include <string>
#include <vector>

namespace Core {

namespace {

/// Output resistance of the logic family, in series with the digital-to-analog bridge of every output of a gate or
/// flip-flop, whose digital code model only meets digital nodes.
constexpr double LOGIC_OUTPUT_RESISTANCE = 50.0;

/// Resistance from every logic input to ground, so an input left unconnected reads 0.
constexpr double LOGIC_INPUT_PULL_DOWN = 1e9;

} // namespace

/**
 * @brief   Writes the models a logic part needs: its digital code model and the bridges of its family.
 * @param[in] part  Logic part whose family gives the delays, levels and thresholds.
 * @return  The ".model" lines, each ending in a line break.
 */
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
 * @brief   Writes the lines of a logic part: a bridge on every input, the digital model and a bridge and a resistor on
 *          every output.
 * @param[in] part                 Logic part to write.
 * @param[in] nodes                Nodes of its terminals, in the order of GetInputLabels() and then
 *                                 GetLogicOutputLabels().
 * @param[in,out] next_extra_node  First free node number; the digital nodes and the analog nodes behind the output
 *                                 resistors are numbered from it, and it is left past the last one used.
 * @return  The netlist lines, each ending in a line break.
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
 * @brief   Names the netlist elements a logic part becomes, as FormatLogicPart() writes them.
 * @param[in] part  Logic part.
 * @return  The name of its model instance, then of the bridge and the resistor of every pin.
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

} // namespace Core
