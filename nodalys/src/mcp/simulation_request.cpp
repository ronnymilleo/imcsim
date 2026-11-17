/**
 * @file    simulation_request.cpp
 * @brief   Works out the analysis a simulate call asks for, from its arguments and the directives of the netlist.
 */

#include "mcp/simulation_request.h"

#include "mcp/analysis_names.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <optional>
#include <utility>
#include <variant>

namespace Nodalys {

namespace {

/// Frequency sweep settings shared by the analyses that sweep frequency.
struct SweepSettings {
    FrequencySweepType Type = FrequencySweepType::DECADE;
    double StartFrequency = 1.0;
    double StopFrequency = 1e6;
    int Points = 10;
};

std::string ToLowerCase(const std::string_view text) {
    std::string lower(text);
    std::ranges::transform(lower, lower.begin(),
                           [](const char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
    return lower;
}

/// Finds the analysis of the netlist that has a kind, or starts a default one.
ParsedAnalysis StartFromNetlist(const ParsedNetlist &parsed, const AnalysisKind kind) {
    const auto found = std::ranges::find(parsed.Analyses, kind, &ParsedAnalysis::Kind);
    if (found != parsed.Analyses.end()) {
        return *found;
    }
    ParsedAnalysis analysis{.Kind = kind, .Config = std::monostate{}};
    switch (kind) {
    case AnalysisKind::TRANSIENT:
        analysis.Config = TransientConfig{};
        break;
    case AnalysisKind::DC_SWEEP:
        analysis.Config = DcSweepConfig{};
        break;
    case AnalysisKind::AC_SWEEP:
        analysis.Config = AcSweepConfig{};
        break;
    case AnalysisKind::TRANSFER_FUNCTION:
        analysis.Config = TransferFunctionConfig{};
        break;
    case AnalysisKind::NOISE:
        analysis.Config = NoiseConfig{};
        break;
    case AnalysisKind::S_PARAMETERS:
        analysis.Config = SParameterConfig{};
        break;
    case AnalysisKind::POLE_ZERO:
        analysis.Config = PoleZeroConfig{};
        break;
    case AnalysisKind::SENSITIVITY:
        analysis.Config = SensitivityConfig{};
        break;
    case AnalysisKind::AC_SENSITIVITY:
        analysis.Config = AcSensitivityConfig{};
        break;
    case AnalysisKind::HARMONIC_BALANCE:
        analysis.Config = HarmonicBalanceConfig{};
        break;
    case AnalysisKind::OPERATING_POINT:
    default:
        break;
    }
    return analysis;
}

/// Reads a node argument by name; none when the argument is absent, an error when the netlist has no such node.
std::expected<std::optional<NodeId>, std::string> ReadNode(ArgumentReader &reader, const NodeLabels &labels,
                                                           const std::string_view key) {
    const std::optional<std::string> name = reader.GetString(key);
    if (!name) {
        return std::optional<NodeId>{};
    }
    const std::optional<NodeId> node = labels.FindNode(*name);
    if (!node) {
        return std::unexpected(std::format("Argument '{}': the netlist has no node named '{}'", key, *name));
    }
    return node;
}

/// Reads the sweep arguments over the settings of the directive.
std::expected<void, std::string> ReadSweep(ArgumentReader &reader, SweepSettings &sweep) {
    sweep.StartFrequency = reader.GetNumber("start_frequency").value_or(sweep.StartFrequency);
    sweep.StopFrequency = reader.GetNumber("stop_frequency").value_or(sweep.StopFrequency);
    sweep.Points = reader.GetInteger("points").value_or(sweep.Points);
    if (const std::optional<std::string> type = reader.GetString("sweep")) {
        const std::string lower = ToLowerCase(*type);
        if (lower == "dec") {
            sweep.Type = FrequencySweepType::DECADE;
        } else if (lower == "oct") {
            sweep.Type = FrequencySweepType::OCTAVE;
        } else if (lower == "lin") {
            sweep.Type = FrequencySweepType::LINEAR;
        } else {
            return std::unexpected("Argument 'sweep' must be \"dec\", \"oct\" or \"lin\"");
        }
    }
    if (sweep.StartFrequency <= 0.0 || sweep.StopFrequency <= sweep.StartFrequency || sweep.Points < 1) {
        return std::unexpected("The frequency sweep needs 0 < start_frequency < stop_frequency and points >= 1");
    }
    return {};
}

/// Replaces `target` with a node argument when the call gave one.
std::expected<void, std::string> ReadOutputNode(ArgumentReader &reader, const NodeLabels &labels,
                                                const std::string_view key, NodeId &target) {
    const auto node = ReadNode(reader, labels, key);
    if (!node) {
        return std::unexpected(node.error());
    }
    if (*node) {
        target = **node;
    }
    return {};
}

std::expected<void, std::string> RequireOutput(const NodeId node, const std::string_view analysis) {
    if (node == GROUND_NODE) {
        return std::unexpected(std::format("The {} analysis needs 'output_node', a node other than ground", analysis));
    }
    return {};
}

std::expected<void, std::string> RequireInput(const std::string &source, const std::string_view analysis) {
    if (source.empty()) {
        return std::unexpected(
            std::format("The {} analysis needs 'input_source', the name of the source that drives the circuit, "
                        "such as \"V1\"",
                        analysis));
    }
    return {};
}

std::expected<void, std::string> ApplyTransient(ArgumentReader &reader, TransientConfig &config) {
    config.StopTime = reader.GetNumber("stop_time").value_or(config.StopTime);
    config.TimeStep = reader.GetNumber("time_step").value_or(config.TimeStep);
    if (config.StopTime <= 0.0 || config.TimeStep <= 0.0) {
        return std::unexpected("The tran analysis needs 'stop_time' and 'time_step' in seconds, both above 0, or a "
                               ".tran directive in the netlist");
    }
    return {};
}

std::expected<void, std::string> ApplyDcSweep(ArgumentReader &reader, DcSweepConfig &config) {
    config.SweptSource = reader.GetString("source").value_or(config.SweptSource);
    config.SweptStart = reader.GetNumber("start").value_or(config.SweptStart);
    config.SweptStop = reader.GetNumber("stop").value_or(config.SweptStop);
    config.SweptStep = reader.GetNumber("step").value_or(config.SweptStep);
    if (config.SweptSource.empty() || config.SweptStep == 0.0) {
        return std::unexpected("The dc analysis needs 'source' (such as \"V1\"), 'start', 'stop' and 'step' (not 0), "
                               "or a .dc directive in the netlist");
    }
    return {};
}

std::expected<void, std::string> ApplyAc(ArgumentReader &reader, AcSweepConfig &config) {
    SweepSettings sweep{config.SweepType, config.StartFrequency, config.StopFrequency, config.PointsPerDecade};
    if (const auto read = ReadSweep(reader, sweep); !read) {
        return read;
    }
    config = {.StartFrequency = sweep.StartFrequency,
              .StopFrequency = sweep.StopFrequency,
              .PointsPerDecade = sweep.Points,
              .SweepType = sweep.Type};
    return {};
}

std::expected<void, std::string> ApplyTransferFunction(ArgumentReader &reader, const NodeLabels &labels,
                                                       TransferFunctionConfig &config) {
    if (const auto read = ReadOutputNode(reader, labels, "output_node", config.OutputPositiveNode); !read) {
        return read;
    }
    if (const auto read = ReadOutputNode(reader, labels, "reference_node", config.OutputNegativeNode); !read) {
        return read;
    }
    config.InputSource = reader.GetString("input_source").value_or(config.InputSource);
    if (const auto required = RequireOutput(config.OutputPositiveNode, "tf"); !required) {
        return required;
    }
    return RequireInput(config.InputSource, "tf");
}

std::expected<void, std::string> ApplyNoise(ArgumentReader &reader, const NodeLabels &labels, NoiseConfig &config) {
    if (const auto read = ReadOutputNode(reader, labels, "output_node", config.OutputPositiveNode); !read) {
        return read;
    }
    if (const auto read = ReadOutputNode(reader, labels, "reference_node", config.OutputNegativeNode); !read) {
        return read;
    }
    config.InputSource = reader.GetString("input_source").value_or(config.InputSource);
    if (const auto required = RequireOutput(config.OutputPositiveNode, "noise"); !required) {
        return required;
    }
    if (const auto required = RequireInput(config.InputSource, "noise"); !required) {
        return required;
    }
    SweepSettings sweep{config.SweepType, config.StartFrequency, config.StopFrequency, config.PointsPerDecade};
    if (const auto read = ReadSweep(reader, sweep); !read) {
        return read;
    }
    config.SweepType = sweep.Type;
    config.StartFrequency = sweep.StartFrequency;
    config.StopFrequency = sweep.StopFrequency;
    config.PointsPerDecade = sweep.Points;
    return {};
}

std::expected<void, std::string> ApplySParameters(ArgumentReader &reader, SParameterConfig &config) {
    SweepSettings sweep{config.SweepType, config.StartFrequency, config.StopFrequency, config.Points};
    if (const auto read = ReadSweep(reader, sweep); !read) {
        return read;
    }
    config.SweepType = sweep.Type;
    config.StartFrequency = sweep.StartFrequency;
    config.StopFrequency = sweep.StopFrequency;
    config.Points = sweep.Points;
    return {};
}

std::expected<void, std::string> ApplyPoleZero(ArgumentReader &reader, const NodeLabels &labels,
                                               PoleZeroConfig &config) {
    if (const auto read = ReadOutputNode(reader, labels, "output_node", config.OutputNode); !read) {
        return read;
    }
    config.InputSource = reader.GetString("input_source").value_or(config.InputSource);
    if (const auto required = RequireOutput(config.OutputNode, "pz"); !required) {
        return required;
    }
    return RequireInput(config.InputSource, "pz");
}

std::expected<void, std::string> ApplySensitivity(ArgumentReader &reader, const NodeLabels &labels,
                                                  SensitivityConfig &config) {
    if (const auto read = ReadOutputNode(reader, labels, "output_node", config.OutputPositiveNode); !read) {
        return read;
    }
    if (const auto read = ReadOutputNode(reader, labels, "reference_node", config.OutputNegativeNode); !read) {
        return read;
    }
    return RequireOutput(config.OutputPositiveNode, "sens");
}

std::expected<void, std::string> ApplyAcSensitivity(ArgumentReader &reader, const NodeLabels &labels,
                                                    AcSensitivityConfig &config) {
    if (const auto read = ReadOutputNode(reader, labels, "output_node", config.OutputPositiveNode); !read) {
        return read;
    }
    if (const auto read = ReadOutputNode(reader, labels, "reference_node", config.OutputNegativeNode); !read) {
        return read;
    }
    config.InputSource = reader.GetString("input_source").value_or(config.InputSource);
    if (const auto required = RequireOutput(config.OutputPositiveNode, "sens_ac"); !required) {
        return required;
    }
    if (const auto required = RequireInput(config.InputSource, "sens_ac"); !required) {
        return required;
    }
    SweepSettings sweep{config.SweepType, config.StartFrequency, config.StopFrequency, config.PointsPerDecade};
    if (const auto read = ReadSweep(reader, sweep); !read) {
        return read;
    }
    config.SweepType = sweep.Type;
    config.StartFrequency = sweep.StartFrequency;
    config.StopFrequency = sweep.StopFrequency;
    config.PointsPerDecade = sweep.Points;
    return {};
}

std::expected<void, std::string> ApplyHarmonicBalance(ArgumentReader &reader, const bool is_from_netlist,
                                                      HarmonicBalanceConfig &config) {
    const std::optional<double> fundamental = reader.GetNumber("fundamental_frequency");
    if (!fundamental && !is_from_netlist) {
        return std::unexpected("The hb analysis needs 'fundamental_frequency' in hertz, or a .hb directive in the "
                               "netlist");
    }
    config.FundamentalFrequency = fundamental.value_or(config.FundamentalFrequency);
    config.Harmonics = reader.GetInteger("harmonics").value_or(config.Harmonics);
    if (config.FundamentalFrequency <= 0.0 || config.Harmonics < 0) {
        return std::unexpected("The hb analysis needs 'fundamental_frequency' above 0 and 'harmonics' of 0 or more");
    }
    return {};
}

/// Applies the arguments of the analysis over its starting configuration.
std::expected<void, std::string> ApplyArguments(ArgumentReader &reader, const NodeLabels &labels,
                                                const bool is_from_netlist, ParsedAnalysis &analysis) {
    switch (analysis.Kind) {
    case AnalysisKind::TRANSIENT:
        return ApplyTransient(reader, std::get<TransientConfig>(analysis.Config));
    case AnalysisKind::DC_SWEEP:
        return ApplyDcSweep(reader, std::get<DcSweepConfig>(analysis.Config));
    case AnalysisKind::AC_SWEEP:
        return ApplyAc(reader, std::get<AcSweepConfig>(analysis.Config));
    case AnalysisKind::TRANSFER_FUNCTION:
        return ApplyTransferFunction(reader, labels, std::get<TransferFunctionConfig>(analysis.Config));
    case AnalysisKind::NOISE:
        return ApplyNoise(reader, labels, std::get<NoiseConfig>(analysis.Config));
    case AnalysisKind::S_PARAMETERS:
        return ApplySParameters(reader, std::get<SParameterConfig>(analysis.Config));
    case AnalysisKind::POLE_ZERO:
        return ApplyPoleZero(reader, labels, std::get<PoleZeroConfig>(analysis.Config));
    case AnalysisKind::SENSITIVITY:
        return ApplySensitivity(reader, labels, std::get<SensitivityConfig>(analysis.Config));
    case AnalysisKind::AC_SENSITIVITY:
        return ApplyAcSensitivity(reader, labels, std::get<AcSensitivityConfig>(analysis.Config));
    case AnalysisKind::HARMONIC_BALANCE:
        return ApplyHarmonicBalance(reader, is_from_netlist, std::get<HarmonicBalanceConfig>(analysis.Config));
    case AnalysisKind::OPERATING_POINT:
    default:
        return {};
    }
}

} // namespace

/**
 * @brief   Works out which analysis to run and with what settings.
 * @details Without an 'analysis' argument the first directive of the netlist runs (an operating point when it has
 *          none). With one, the netlist's directive of that kind is the starting point, or a default configuration
 *          when it has none, and the arguments replace its settings.
 * @param[in,out] reader  Arguments of the call.
 * @param[in]     parsed  Parsed netlist, which holds the directives and the circuit.
 * @param[in]     labels  Node names of the netlist, to resolve the node arguments.
 * @return  The analysis to run, or the message about a missing or invalid argument.
 */
std::expected<ParsedAnalysis, std::string> BuildAnalysis(ArgumentReader &reader, const ParsedNetlist &parsed,
                                                         const NodeLabels &labels) {
    const std::optional<std::string> name = reader.GetString("analysis");
    if (reader.HasError()) {
        return std::unexpected(reader.GetError());
    }
    ParsedAnalysis analysis;
    bool is_from_netlist = true;
    if (!name) {
        analysis = parsed.Analyses.empty() ? ParsedAnalysis{AnalysisKind::OPERATING_POINT, std::monostate{}}
                                           : parsed.Analyses.front();
    } else {
        const std::optional<AnalysisKind> kind = FindAnalysisKind(ToLowerCase(*name));
        if (!kind) {
            std::string known;
            for (const std::string_view analysis_name : GetAnalysisNames()) {
                known += known.empty() ? "" : ", ";
                known += analysis_name;
            }
            return std::unexpected(std::format("Unknown analysis '{}'; use one of: {}", *name, known));
        }
        is_from_netlist = std::ranges::contains(parsed.Analyses, *kind, &ParsedAnalysis::Kind);
        analysis = StartFromNetlist(parsed, *kind);
    }

    const std::expected<void, std::string> applied = ApplyArguments(reader, labels, is_from_netlist, analysis);
    // An argument of the wrong type reads as absent, so it is the likelier cause of whatever else came out wrong
    if (reader.HasError()) {
        return std::unexpected(reader.GetError());
    }
    if (!applied) {
        return std::unexpected(applied.error());
    }
    return analysis;
}

} // namespace Nodalys
