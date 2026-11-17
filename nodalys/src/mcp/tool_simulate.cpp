/**
 * @file    tool_simulate.cpp
 * @brief   The simulate tool: runs one analysis on a netlist and answers compact figures or the full data.
 */

#include "mcp/tool_simulate.h"

#include "cli/analysis_runner.h"
#include "mcp/analysis_names.h"
#include "mcp/json_arguments.h"
#include "mcp/node_labels.h"
#include "mcp/simulation_request.h"
#include "mcp/simulation_summary.h"
#include "nodalys/exporters.h"
#include "nodalys/netlist_parser.h"

#include <algorithm>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Nodalys {

namespace {

/// The largest full answer written into the reply; a longer one goes to a file, so it never floods the AI's context.
constexpr std::size_t MAX_INLINE_FULL_CHARACTERS = 100000;
/// How many nodes the figures cover when the call names none.
constexpr std::size_t MAX_DEFAULT_NODES = 24;

constexpr const char *DESCRIPTION =
    "Runs one analysis on a SPICE netlist with the Nodalys engine and answers compact figures, not vectors of points. "
    "Without 'analysis' it runs the first directive of the netlist (.op if it has none). With 'analysis' it runs that "
    "kind, taking its settings from the netlist's directive of the same kind when there is one and from the "
    "arguments otherwise; arguments always replace the directive's values. "
    "Analyses: op (node voltages and branch currents), tran (min, max, peak-to-peak, final, mean and RMS per node; "
    "needs stop_time and time_step), ac (gain, phase, peak, -3 dB cutoffs, unity-gain frequency and phase margin per "
    "node), dc (extremes per node over the swept source; needs source, start, stop, step), noise (integrated output "
    "and input noise and the largest contributors; needs output_node and input_source), tf (small-signal gain, input "
    "and output impedance; needs output_node and input_source), sens (DC sensitivity of output_node to every "
    "element value), sens_ac (AC sensitivity in dB per 1 % per element; needs output_node and input_source), pz "
    "(poles and zeros in rad/s; needs output_node and input_source), sp (S-parameters of the circuit's ports, "
    "magnitudes in dB), hb (harmonic balance: DC, harmonic amplitudes and THD per node; needs fundamental_frequency). "
    "Numbers may be JSON numbers or SPICE strings such as \"1k\" or \"10u\". 'nodes' lists the nodes to report "
    "(default: the named nodes of the circuit, up to a limit). "
    "Set detail to \"full\" for all the data in the JSON export format of the nodalys command line (refused above a "
    "size limit), or give output_file to write that same JSON to a file and keep the answer short. "
    "A bad netlist, bad arguments or a run that does not converge come back as an error result with the reason.";

/// Schema of a number that may also be written as a SPICE string such as "10u".
nlohmann::json NumberProperty(const std::string &description) {
    return {{"type", {"number", "string"}}, {"description", description}};
}

nlohmann::json MakeInputSchema() {
    nlohmann::json analyses = nlohmann::json::array();
    for (const std::string_view name : GetAnalysisNames()) {
        analyses.push_back(std::string(name));
    }
    nlohmann::json properties = {
        {"netlist", {{"type", "string"}, {"description", "The complete SPICE netlist text, title line first."}}},
        {"analysis",
         {{"type", "string"},
          {"enum", analyses},
          {"description", "Analysis to run. Omit to run the first directive of the netlist. sens is the DC "
                          "sensitivity, sens_ac the AC one."}}},
        {"nodes",
         {{"type", "array"},
          {"items", {{"type", "string"}}},
          {"description", "Names of the nodes to report, such as [\"out\", \"n1\"]. Ground is 0."}}},
        {"detail",
         {{"type", "string"},
          {"enum", {"summary", "full"}},
          {"description", "summary (default) answers a few figures; full adds all the data of the run."}}},
        {"output_file",
         {{"type", "string"},
          {"description", "Path of a file to write the full data to, as JSON. The answer stays a summary."}}},
        {"stop_time", NumberProperty("tran: end of the run in seconds.")},
        {"time_step", NumberProperty("tran: output step in seconds.")},
        {"start_frequency", NumberProperty("ac, noise, sens_ac, sp: first frequency in hertz.")},
        {"stop_frequency", NumberProperty("ac, noise, sens_ac, sp: last frequency in hertz.")},
        {"points",
         {{"type", "integer"},
          {"description", "ac, noise, sens_ac: points per decade or octave, or in total for lin; sp: points."}}},
        {"sweep",
         {{"type", "string"},
          {"enum", {"dec", "oct", "lin"}},
          {"description", "Frequency spacing of ac, noise, sens_ac and sp; default dec."}}},
        {"source", {{"type", "string"}, {"description", "dc: name of the source to sweep, such as \"V1\"."}}},
        {"start", NumberProperty("dc: first value of the swept source.")},
        {"stop", NumberProperty("dc: last value of the swept source.")},
        {"step", NumberProperty("dc: increment of the swept source.")},
        {"output_node",
         {{"type", "string"},
          {"description", "tf, noise, pz, sens, sens_ac: the output node (positive side of the output voltage)."}}},
        {"reference_node",
         {{"type", "string"},
          {"description", "tf, noise, sens, sens_ac: the negative side of the output voltage; default ground."}}},
        {"input_source",
         {{"type", "string"},
          {"description", "tf, noise, pz, sens_ac: name of the source that drives the circuit, such as \"V1\"."}}},
        {"fundamental_frequency", NumberProperty("hb: fundamental frequency in hertz.")},
        {"harmonics", {{"type", "integer"}, {"description", "hb: highest harmonic to solve for; default 8."}}},
    };
    return {{"type", "object"},
            {"properties", std::move(properties)},
            {"required", {"netlist"}},
            {"additionalProperties", false}};
}

/// Picks the nodes to report: the ones the call names, or the named nodes of the circuit up to a limit.
std::expected<std::vector<NodeId>, std::string> ChooseNodes(const std::optional<std::vector<std::string>> &requested,
                                                            const NodeLabels &labels, std::size_t &omitted_count) {
    std::vector<NodeId> nodes;
    if (requested) {
        for (const std::string &name : *requested) {
            const std::optional<NodeId> node = labels.FindNode(name);
            if (!node) {
                return std::unexpected(std::format("Argument 'nodes': the netlist has no node named '{}'", name));
            }
            nodes.push_back(*node);
        }
        return nodes;
    }
    nodes = labels.GetNamedNodes();
    if (nodes.size() > MAX_DEFAULT_NODES) {
        omitted_count = nodes.size() - MAX_DEFAULT_NODES;
        nodes.resize(MAX_DEFAULT_NODES);
    }
    return nodes;
}

/// Adds the full data to the answer: in the reply, to a file, or neither when it is too long for the reply.
std::expected<void, std::string> AttachFullData(nlohmann::json &answer, const SimulationResult &result,
                                                const NodeLabels &labels, const bool is_full_requested,
                                                const std::optional<std::string> &output_file) {
    if (!output_file && !is_full_requested) {
        return {};
    }
    nlohmann::json full = MakeFullData(result, labels);
    const std::string text = full.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    if (output_file) {
        if (!SaveStringToFile(*output_file, text)) {
            return std::unexpected(std::format("Could not write the output file '{}'", *output_file));
        }
        answer["output_file"] = *output_file;
        answer["output_file_bytes"] = text.size();
    }
    if (is_full_requested) {
        if (text.size() > MAX_INLINE_FULL_CHARACTERS) {
            answer["full_data_omitted"] = std::format(
                "The full data is {} characters, over the limit of {} for a reply; give output_file to write it to a "
                "file, or ask for a shorter run",
                text.size(), MAX_INLINE_FULL_CHARACTERS);
        } else {
            answer["full"] = std::move(full);
        }
    }
    return {};
}

ToolResult Simulate(const nlohmann::json &arguments) {
    ArgumentReader reader(arguments);
    const std::optional<std::string> netlist = reader.GetString("netlist");
    const std::optional<std::vector<std::string>> requested_nodes = reader.GetStringList("nodes");
    const std::optional<std::string> detail = reader.GetString("detail");
    const std::optional<std::string> output_file = reader.GetString("output_file");
    if (reader.HasError()) {
        return MakeErrorResult(reader.GetError());
    }
    if (!netlist) {
        return MakeErrorResult("Argument 'netlist' is required: pass the text of the SPICE netlist");
    }
    if (detail && *detail != "summary" && *detail != "full") {
        return MakeErrorResult("Argument 'detail' must be \"summary\" or \"full\"");
    }

    const std::expected<ParsedNetlist, std::string> parsed = ParseNetlist(*netlist);
    if (!parsed) {
        return MakeErrorResult("Netlist error: " + parsed.error());
    }
    const NodeLabels labels(*parsed);

    const std::expected<ParsedAnalysis, std::string> analysis = BuildAnalysis(reader, *parsed, labels);
    if (!analysis) {
        return MakeErrorResult(analysis.error());
    }
    std::size_t omitted_nodes = 0;
    const std::expected<std::vector<NodeId>, std::string> nodes = ChooseNodes(requested_nodes, labels, omitted_nodes);
    if (!nodes) {
        return MakeErrorResult(nodes.error());
    }

    std::vector<DiagnosticMessage> diagnostics;
    const std::expected<SimulationResult, std::string> result = RunAnalysis(*parsed, *analysis, &diagnostics);
    if (!result) {
        return MakeErrorResult(std::format("Simulation error: {}", result.error()));
    }

    nlohmann::json answer = SummarizeResult(*result, labels, *nodes);
    if (omitted_nodes > 0) {
        answer["nodes_omitted"] = omitted_nodes;
    }
    // The routine progress lines of the solver are left out; only what it flags as an error is worth the AI's context
    nlohmann::json messages = nlohmann::json::array();
    for (const DiagnosticMessage &message : diagnostics) {
        if (message.IsError) {
            messages.push_back(message.Text);
        }
    }
    if (!messages.empty()) {
        answer["solver_messages"] = std::move(messages);
    }
    // Directives the call did not run are named, so the AI knows the netlist asks for more
    if (parsed->Analyses.size() > 1) {
        nlohmann::json directives = nlohmann::json::array();
        for (const ParsedAnalysis &directive : parsed->Analyses) {
            directives.push_back(std::string(GetAnalysisName(directive.Kind)));
        }
        answer["netlist_analyses"] = std::move(directives);
    }

    if (const auto attached = AttachFullData(answer, *result, labels, detail == "full", output_file); !attached) {
        return MakeErrorResult(attached.error());
    }
    return MakeJsonResult(answer);
}

} // namespace

/**
 * @return  The simulate tool.
 */
McpTool MakeSimulateTool() {
    return {.Name = "simulate", .Description = DESCRIPTION, .InputSchema = MakeInputSchema(), .Run = Simulate};
}

} // namespace Nodalys
