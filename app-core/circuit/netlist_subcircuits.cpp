/**
 * @file    netlist_subcircuits.cpp
 * @brief   Writes the .subckt block of each block definition, its models scoped to it, in the order of use.
 */

#include "circuit/netlist_subcircuits.h"

#include "circuit/circuit.h"
#include "circuit/circuit_currents.h"
#include "circuit/circuit_netlist.h"
#include "circuit/components/component.h"
#include "circuit/components/subcircuit.h"
#include "circuit/subcircuit.h"
#include "spice/spice_text.h"
#include "spice/spice_value.h"
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <format>
#include <functional>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Core {

namespace {

bool IsModelNameCharacter(const char character) {
    return std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '_' || character == '-';
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
        directive = ToLower(directive);
        if (directive == ".model" || directive == ".subckt") {
            declared.insert(ToLower(name));
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
        scoped += !is_element_name && declared.contains(ToLower(token)) ? prefix + token : token;
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
 * @brief   The ".subckt" line of a block: its name, the nodes or ports of its pins and its parameters with defaults.
 */
std::string FormatSubcircuitHeader(const SubcircuitDefinition &definition) {
    std::string header = std::format(".subckt {}", definition.Name);
    if (definition.Spice) {
        for (const std::string &port : definition.Spice->Ports) {
            header += std::format(" {}", port);
        }
    } else {
        for (const SubcircuitPin &pin : definition.Pins) {
            header += std::format(" {}", pin.Node);
        }
    }
    if (!definition.Parameters.empty()) {
        header += " params:";
        for (const SubcircuitParameter &parameter : definition.Parameters) {
            header += std::format(" {}={}", parameter.Name, FormatSpiceValue(parameter.DefaultValue));
        }
    }
    header += "\n";
    return header;
}

/**
 * @brief   What a block holds, before its models are scoped: its SPICE text as it came, or its drawn parts as netlist
 *          lines.
 * @details The root netlist writes the options, the initial voltages and the nodesets of every instance, once and at
 *          the top, where ngspice reads them, so a drawn body is only its parts and their models.
 */
std::string FormatSubcircuitBody(const SubcircuitDefinition &definition) {
    if (definition.Spice) {
        std::string body = definition.Spice->Text;
        if (!body.empty() && !body.ends_with('\n')) {
            body += '\n';
        }
        return body;
    }
    // A pin that no part inside uses still owns its node, which the extra nodes of the body must not take
    Circuit numbered_body = definition.Body;
    for (const SubcircuitPin &pin : definition.Pins) {
        numbered_body.ReserveNode(pin.Node);
    }
    return WriteNetlistBody(numbered_body, false);
}

} // namespace

/**
 * @brief   Writes the ".subckt" block of a block definition.
 * @details A drawn block lists the nodes of its pins and writes its body as a netlist; a block kept as SPICE text lists
 *          the ports of its .subckt line and writes its text as it came.
 * @param[in] definition  Block definition to write.
 * @return  The block, from ".subckt" to ".ends", each line ending in a line break.
 */
std::string FormatSubcircuit(const SubcircuitDefinition &definition) {
    return FormatSubcircuitHeader(definition) + ScopeModels(FormatSubcircuitBody(definition), definition.Name)
         + std::format(".ends {}\n", definition.Name);
}

/**
 * @brief   Orders block definitions so that each one comes after the blocks its body places.
 * @details That is the order a reader of the netlist meets them in. The order of the list stays among the others, and
 *          a cycle, which the checks refuse, ends the walk.
 * @param[in] list  Definitions of the circuit, none of them null.
 * @return  The same definitions, not owned, in the order to write them.
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

} // namespace Core
