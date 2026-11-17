/**
 * @file    tool_check_netlist.cpp
 * @brief   The check_netlist tool: reads a netlist and reports its mistakes or a short summary of it.
 */

#include "mcp/tool_check_netlist.h"

#include "mcp/analysis_names.h"
#include "mcp/json_arguments.h"
#include "mcp/node_labels.h"
#include "nodalys/netlist_parser.h"

#include <cctype>
#include <expected>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace Nodalys {

namespace {

constexpr const char *DESCRIPTION =
    "Reads a SPICE netlist with the same reader that 'simulate' uses and reports its mistakes, without running any "
    "analysis. On success it answers the title, the node names, the element count per kind (R, C, L, V, I, D, Q, M, "
    "X...), the subcircuits defined, the analyses its directives ask for and any warnings of the reader, such as an "
    "ignored line. On failure it answers an error that names the offending line or directive. Use it to validate a "
    "netlist you have just written before simulating it. The first line of a netlist is its title, not a circuit "
    "element. A netlist with no analysis directive is listed as having one operating point (op).";

nlohmann::json MakeInputSchema() {
    return {{"type", "object"},
            {"properties",
             {{"netlist",
               {{"type", "string"},
                {"description", "The complete SPICE netlist text: a title line, elements, .model cards, optional "
                                ".subckt definitions and analysis directives such as .ac or .tran."}}}}},
            {"required", {"netlist"}},
            {"additionalProperties", false}};
}

/// What the lines of the text show of the circuit: how many elements of each kind and which subcircuits are defined.
struct NetlistInventory {
    std::map<char, int> ElementsByKind;
    std::vector<std::string> Subcircuits;
};

/// Reads the first word of a line, as the reader does.
std::string FirstWord(const std::string &line) {
    std::istringstream words(line);
    std::string word;
    words >> word;
    return word;
}

/**
 * @brief   Counts the elements of the top-level circuit by their first letter.
 * @details The first line is the title, the lines inside a `.subckt` belong to the definition, and a `+` line continues
 *          the one before it.
 */
NetlistInventory TakeInventory(const std::string &netlist) {
    NetlistInventory inventory;
    std::istringstream lines(netlist);
    std::string line;
    bool is_title = true;
    int subcircuit_depth = 0;
    while (std::getline(lines, line)) {
        const std::string word = FirstWord(line);
        if (word.empty() || is_title) {
            is_title = is_title && word.empty();
            continue;
        }
        const char first = static_cast<char>(std::toupper(static_cast<unsigned char>(word.front())));
        if (first == '*' || first == ';' || first == '+') {
            continue;
        }
        if (first == '.') {
            std::string directive;
            for (const char c : word) {
                directive.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
            }
            if (directive == ".SUBCKT") {
                std::istringstream words(line);
                std::string ignored;
                std::string name;
                words >> ignored >> name;
                inventory.Subcircuits.push_back(name);
                ++subcircuit_depth;
            } else if (directive == ".ENDS" && subcircuit_depth > 0) {
                --subcircuit_depth;
            }
            continue;
        }
        if (subcircuit_depth == 0) {
            ++inventory.ElementsByKind[first];
        }
    }
    return inventory;
}

nlohmann::json DescribeElements(const NetlistInventory &inventory) {
    nlohmann::json elements = nlohmann::json::object();
    for (const auto &[kind, count] : inventory.ElementsByKind) {
        elements[std::string(1, kind)] = count;
    }
    return elements;
}

nlohmann::json DescribeAnalyses(const ParsedNetlist &parsed) {
    nlohmann::json analyses = nlohmann::json::array();
    for (const ParsedAnalysis &analysis : parsed.Analyses) {
        analyses.push_back(std::string(GetAnalysisName(analysis.Kind)));
    }
    return analyses;
}

ToolResult CheckNetlist(const nlohmann::json &arguments) {
    ArgumentReader reader(arguments);
    const std::optional<std::string> netlist = reader.GetString("netlist");
    if (reader.HasError()) {
        return MakeErrorResult(reader.GetError());
    }
    if (!netlist) {
        return MakeErrorResult("Argument 'netlist' is required: pass the text of the SPICE netlist");
    }

    const std::expected<ParsedNetlist, std::string> parsed = ParseNetlist(*netlist);
    if (!parsed) {
        return MakeErrorResult("Netlist error: " + parsed.error());
    }

    const NodeLabels labels(*parsed);
    const NetlistInventory inventory = TakeInventory(*netlist);
    nlohmann::json nodes = nlohmann::json::array();
    for (const NodeId node : labels.GetNamedNodes()) {
        nodes.push_back(labels.GetName(node));
    }
    nlohmann::json warnings = nlohmann::json::array();
    for (const DiagnosticMessage &message : parsed->Diagnostics) {
        warnings.push_back(message.Text);
    }

    return MakeJsonResult({{"valid", true},
                           {"title", parsed->Title},
                           {"nodes", std::move(nodes)},
                           {"elements", DescribeElements(inventory)},
                           {"subcircuits", inventory.Subcircuits},
                           {"analyses", DescribeAnalyses(*parsed)},
                           {"mna_unknowns", parsed->CircuitInstance.GetUnknownCount()},
                           {"warnings", std::move(warnings)}});
}

} // namespace

/**
 * @return  The check_netlist tool.
 */
McpTool MakeCheckNetlistTool() {
    return {.Name = "check_netlist", .Description = DESCRIPTION, .InputSchema = MakeInputSchema(), .Run = CheckNetlist};
}

} // namespace Nodalys
