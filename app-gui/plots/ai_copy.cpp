/**
 * @file    ai_copy.cpp
 * @brief   Writes the circuit, the settings of the selected analysis and a summary of its result as one text to paste
 *          into a conversation with an AI assistant.
 */

#include "plots/ai_copy.h"

#include "circuit/circuit.h"
#include "circuit/solver_options.h"
#include "plots/ai_result_summary.h"
#include "schematic/probing.h"
#include "spice/spice_value.h"
#include "ui_elements/ui_element.h"
#include "ui_elements/ui_source.h"
#include <format>
#include <optional>

namespace GUI {

namespace {

/// Most parts the list names, so a very large schematic does not bury the netlist.
constexpr std::size_t MAX_LISTED_PARTS = 200;

/**
 * @brief   The node a stored output point lies on, named as the plots name it.
 */
std::string DescribeOutput(const std::optional<GridPoint> &point, const Connectivity &connectivity) {
    if (!point) {
        return "chosen automatically";
    }
    const std::optional<int> node = connectivity.GetNode(*point);
    return node ? GetVoltageLabel(connectivity, *node) : "not on a node of the circuit";
}

std::string DescribeSource(const std::string &stored) {
    return stored.empty() ? "chosen automatically" : stored;
}

std::string DescribeFrequencyRange(const Core::ACSweepSettings &range) {
    return std::format("{}Hz to {}Hz, {} points per decade", Core::FormatValue(range.StartFrequency),
                       Core::FormatValue(range.StopFrequency), range.PointsPerDecade);
}

const char *GetMethodName(const Core::IntegrationMethod method) {
    switch (method) {
    case Core::IntegrationMethod::TRAPEZOIDAL:
        return "trapezoidal";
    case Core::IntegrationMethod::GEAR:
        return "Gear";
    case Core::IntegrationMethod::DEFAULT:
    default:
        return "default";
    }
}

/**
 * @brief   The bullet of the stepped source of a DC sweep, or nothing when it steps none.
 */
std::string DescribeSteppedSource(const SimulationSettings &settings) {
    if (!settings.StepSource) {
        return {};
    }
    return std::format("- Stepped source: {}, from {} to {} in steps of {}\n",
                       DescribeSource(settings.SteppedRange.Source), Core::FormatValue(settings.SteppedRange.Start),
                       Core::FormatValue(settings.SteppedRange.Stop), Core::FormatValue(settings.SteppedRange.Step));
}

/**
 * @brief   The bullets of the kind of a sensitivity analysis, with the input and sweep of the AC kind.
 */
std::string DescribeSensitivityKind(const SimulationSettings &settings) {
    if (settings.SensitivityType != SensitivityKind::AC) {
        return "- Kind: DC, the voltage of the output\n";
    }
    return std::format("- Kind: AC, the response across frequency\n- Input source: {}\n- Sweep: {}\n",
                       DescribeSource(settings.ACSensitivitySource),
                       DescribeFrequencyRange(settings.ACSensitivityRange));
}

std::string DescribeSParameterRange(const SimulationSettings &settings) {
    const std::string points = settings.SParameters.IsLogarithmic
                                   ? std::format("{} points per decade", settings.SParameters.PointsPerDecade)
                                   : std::format("{} linear points", settings.SParameters.LinearPoints);
    return std::format("Range: {}Hz to {}Hz, {}", Core::FormatValue(settings.SParameters.StartFrequency),
                       Core::FormatValue(settings.SParameters.StopFrequency), points);
}

std::string DescribeTruthTableInputs(const SimulationSettings &settings) {
    return settings.TruthTableInputs.empty()
               ? "Inputs: every logic input"
               : std::format("Inputs: {} logic inputs chosen", settings.TruthTableInputs.size());
}

/**
 * @brief   The lines about the settings of the selected analysis, a bullet each.
 */
std::string DescribeAnalysisSettings(const SimulationSettings &settings, const Connectivity &connectivity) {
    std::string text;
    const auto add = [&text](const std::string &line) { text += std::format("- {}\n", line); };
    switch (settings.Selected) {
    case Analysis::TRANSIENT:
        add(std::format("Stop time: {}s, time step: {}s", Core::FormatValue(settings.Transient.StopTime),
                        Core::FormatValue(settings.Transient.TimeStep)));
        break;
    case Analysis::AC_SWEEP:
        add(std::format("Sweep: {}", DescribeFrequencyRange(settings.ACSweep)));
        break;
    case Analysis::DC_SWEEP:
        add(std::format("Swept source: {}, from {} to {} in steps of {}", DescribeSource(settings.SweptRange.Source),
                        Core::FormatValue(settings.SweptRange.Start), Core::FormatValue(settings.SweptRange.Stop),
                        Core::FormatValue(settings.SweptRange.Step)));
        text += DescribeSteppedSource(settings);
        break;
    case Analysis::NOISE:
        add(std::format("Output: {}", DescribeOutput(settings.NoiseOutput, connectivity)));
        add(std::format("Input source: {}", DescribeSource(settings.NoiseSource)));
        add(std::format("Sweep: {}", DescribeFrequencyRange(settings.NoiseRange)));
        break;
    case Analysis::TRANSFER_FUNCTION:
        add(std::format("Output: {}", DescribeOutput(settings.TransferFunctionOutput, connectivity)));
        add(std::format("Input source: {}", DescribeSource(settings.TransferFunctionSource)));
        break;
    case Analysis::SENSITIVITY:
        add(std::format("Output: {}", DescribeOutput(settings.SensitivityOutput, connectivity)));
        text += DescribeSensitivityKind(settings);
        break;
    case Analysis::POLE_ZERO:
        add(std::format("Output: {}", DescribeOutput(settings.PoleZeroOutput, connectivity)));
        add(std::format("Input source: {}", DescribeSource(settings.PoleZeroSource)));
        break;
    case Analysis::S_PARAMETERS:
        add(DescribeSParameterRange(settings));
        break;
    case Analysis::TRUTH_TABLE:
        add(DescribeTruthTableInputs(settings));
        break;
    case Analysis::HARMONIC_BALANCE:
        add(std::format("Fundamental: {}Hz, harmonics: {}",
                        Core::FormatValue(settings.HarmonicBalance.FundamentalFrequency),
                        settings.HarmonicBalance.Harmonics));
        break;
    case Analysis::OPERATING_POINT:
    default:
        add("No settings");
        break;
    }
    return text;
}

/**
 * @brief   The run modes and solver options that change what an analysis does, or nothing when they are all at rest.
 */
std::string DescribeRunOptions(const SimulationSettings &settings) {
    std::string text;
    if (IsRepeatable(settings.Selected)) {
        if (settings.ParameterStep.Enabled) {
            text += std::format("- Stepped parameter: {}\n", GetParameterLabel(settings.ParameterStep));
        }
        if (settings.MonteCarlo.Enabled) {
            text += std::format(
                "- Monte Carlo: {} runs, {} distribution, seed {}\n", settings.MonteCarlo.Runs,
                settings.MonteCarlo.Distribution == Core::ToleranceDistribution::GAUSSIAN ? "Gaussian" : "uniform",
                settings.MonteCarlo.Seed == 0 ? std::string("drawn anew") : std::to_string(settings.MonteCarlo.Seed));
        }
    }
    const Core::SolverOptions &solver = settings.Solver;
    if (!(solver == Core::SolverOptions{})) {
        text +=
            std::format("- Solver: relative tolerance {}, current tolerance {}A, voltage tolerance {}V, "
                        "integration {}, nominal temperature {} C\n",
                        Core::FormatValue(solver.RelativeTolerance), Core::FormatValue(solver.CurrentToleranceAmperes),
                        Core::FormatValue(solver.VoltageToleranceVolts), GetMethodName(solver.Method),
                        Core::FormatValue(solver.NominalTemperatureCelsius));
    }
    return text;
}

/**
 * @brief   The value of a part as its symbol reads it: a source that is not DC by its waveform, since it ignores its
 *          value.
 */
std::string DescribeValue(const Core::Component &component) {
    if (const auto *source = dynamic_cast<const Core::Source *>(&component);
        source != nullptr && source->GetSourceType() != Core::Source::SourceType::DC) {
        return GetWaveformLabel(*source);
    }
    return component.HasValue() ? std::format("{}{}", GetValueLabel(component), component.GetUnit()) : std::string();
}

/**
 * @brief   One row per part with its value, and the named nets with the node numbers the netlist uses for them.
 */
std::string DescribeParts(Schematic &schematic) {
    std::string table = "| Part | Kind | Value |\n| --- | --- | --- |\n";
    std::size_t listed = 0;
    std::size_t omitted = 0;
    for (const std::unique_ptr<UIElement> &element : schematic.GetElements()) {
        const Core::Component &component = element->GetComponent();
        // Ground, net labels and pin markers have no name and no netlist line of their own
        if (component.GetName().empty()) {
            continue;
        }
        if (listed == MAX_LISTED_PARTS) {
            ++omitted;
            continue;
        }
        table +=
            std::format("| {} | {} | {} |\n", component.GetName(), component.GetTypeName(), DescribeValue(component));
        ++listed;
    }
    std::string text = table;
    if (omitted > 0) {
        text += std::format("\n{} more parts are not listed.\n", omitted);
    }
    const auto &nets = schematic.GetConnectivity().GetNets();
    if (!nets.empty()) {
        text += "\nNamed nets, as node numbers in the netlist: ";
        bool is_first = true;
        for (const auto &[name, node] : nets) {
            text += std::format("{}{} = {}", is_first ? "" : ", ", name, node);
            is_first = false;
        }
        text += "\n";
    }
    return text;
}

} // namespace

/**
 * @brief   Writes the circuit and what the selected analysis found as Markdown.
 * @param[in,out] schematic    The circuit, whose displayed netlist, parts, nets and settings are written.
 * @param[in]     results      The results of the last runs, of which the selected analysis' is summarized.
 * @param[in]     engine_name  The engine that runs the analyses, such as "Nodalys".
 * @return  Sections for the netlist, the parts, the settings and the results. An analysis that has no result on the
 *          circuit as it is now says that it has not been run.
 */
std::string BuildAICopyText(Schematic &schematic, const ResultStore &results, const std::string_view engine_name) {
    const SimulationSettings &settings = schematic.GetSimulationSettings();
    std::string text = std::format("# Circuit from imcsim\n\nSimulation engine: {}. Values are in SI units; node 0 is "
                                   "ground.\n\n",
                                   engine_name);
    text += std::format("## Netlist\n\n```spice\n{}```\n\n", schematic.BuildSpiceNetlist());
    text += std::format("## Parts\n\n{}\n", DescribeParts(schematic));
    text += std::format("## Analysis: {}\n\n", GetAnalysisName(settings.Selected));
    text += DescribeAnalysisSettings(settings, schematic.GetConnectivity());
    text += DescribeRunOptions(settings);
    text += "\n## Results\n\n";
    if (const std::optional<std::string> summary = SummarizeLastResult(schematic, results)) {
        text += *summary;
    } else {
        text += "This analysis has not been run on the circuit as it is now.\n";
    }
    return text;
}

} // namespace GUI
