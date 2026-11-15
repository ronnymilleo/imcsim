/**
 * @file    netlist_subcircuits.cpp
 * @brief   Writes the .subckt block of each block definition, its models scoped to it, in the order of use.
 */

#include "circuit/netlist_subcircuits.h"

#include "circuit/circuit.h"
#include "circuit/circuit_currents.h"
#include "circuit/circuit_netlist.h"
#include "circuit/components/component.h"
#include "circuit/components/fuse.h"
#include "circuit/components/subcircuit.h"
#include "circuit/subcircuit.h"
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

std::string ToLowerCase(const std::string_view text) {
    std::string lower(text);
    std::ranges::transform(lower, lower.begin(),
                           [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return lower;
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

} // namespace

/**
 * @brief   Writes the ".subckt" block of a block definition.
 * @details A drawn block lists the nodes of its pins and writes its body as a netlist; a block kept as SPICE text lists
 *          the ports of its .subckt line and writes its text as it came.
 * @param[in] definition  Block definition to write.
 * @return  The block, from ".subckt" to ".ends", each line ending in a line break.
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
