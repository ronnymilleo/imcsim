/**
 * @file    ai_result_summary.cpp
 * @brief   Summaries of the results of each analysis as short Markdown, for pasting into a conversation with an AI.
 */

#include "plots/ai_result_summary.h"

#include "circuit/circuit.h"
#include "plots/harmonic_balance_view.h"
#include "plots/statistics_table.h"
#include "results/ac_sensitivity_view.h"
#include "results/logic_trace.h"
#include "results/monte_carlo_results.h"
#include "results/parameter_step.h"
#include "results/pole_zero_view.h"
#include "results/sensitivity_view.h"
#include "results/stability_summary.h"
#include "results/truth_table.h"
#include "results/two_port_metrics.h"
#include "schematic/probing.h"
#include "spice/spice_value.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <format>
#include <numbers>
#include <span>
#include <string_view>

namespace GUI {

namespace {

/// Decibels of a magnitude at or below zero, so an absent signal reads as a floor and not as minus infinity.
constexpr double DECIBEL_FLOOR = -300.0;
/// Decibels per decade of amplitude.
constexpr double DECIBELS_PER_DECADE = 20.0;
/// Most ports whose full matrix is listed; a larger circuit lists the reflections only.
constexpr int MAX_FULL_MATRIX_PORTS = 4;
/// Most combinations of a truth table written out.
constexpr std::size_t MAX_TRUTH_TABLE_ROWS = 32;
/// Degrees in a radian.
constexpr double RADIANS_TO_DEGREES = 180.0 / std::numbers::pi;
/// Index of the fundamental in the rows of a harmonic summary, which start at the DC component.
constexpr std::size_t FUNDAMENTAL_ROW = 1;

/**
 * @brief   Writes a Markdown table.
 * @param[in] headers  Names of the columns.
 * @param[in] rows     The cells of each row, as many as the headers.
 */
std::string WriteTable(const std::vector<std::string> &headers, const std::vector<std::vector<std::string>> &rows) {
    const auto write_row = [](const std::vector<std::string> &cells) {
        std::string line = "|";
        for (const std::string &cell : cells) {
            line += std::format(" {} |", cell);
        }
        return line + "\n";
    };
    std::string text = write_row(headers);
    text += write_row(std::vector<std::string>(headers.size(), "---"));
    for (const std::vector<std::string> &row : rows) {
        text += write_row(row);
    }
    return text;
}

std::string Quantity(const double value, const std::string_view unit) {
    return std::format("{}{}", Core::FormatMeasurement(value), unit);
}

std::string FrequencyRange(const std::span<const double> frequencies) {
    if (frequencies.empty()) {
        return "no frequencies";
    }
    return std::format("{}Hz to {}Hz, {} points", Core::FormatValue(frequencies.front()),
                       Core::FormatValue(frequencies.back()), frequencies.size());
}

double ToDecibels(const double magnitude) {
    return magnitude > 0.0 ? std::max(DECIBEL_FLOOR, DECIBELS_PER_DECADE * std::log10(magnitude)) : DECIBEL_FLOOR;
}

/**
 * @brief   Says how many nodes the summary left out, or nothing when it listed them all.
 */
std::string OmittedNote(const SummaryContext &context) {
    if (context.OmittedNodes == 0) {
        return {};
    }
    return std::format("\n{} more nodes are not listed; probe them to include them.\n", context.OmittedNodes);
}

/**
 * @brief   Writes a table with a row per trace from the quantity rows of a statistics table, which have a column per
 *          trace.
 * @param[in] statistics  Rows of the statistics, one per quantity.
 * @param[in] labels      Name of each trace, in the order of the columns of the statistics.
 * @param[in] quantities  Names of the rows to keep, in the order of the columns of the table written.
 */
std::string WriteStatisticsTable(const std::vector<StatisticsRow> &statistics, const std::vector<std::string> &labels,
                                 const std::span<const std::string_view> quantities) {
    std::vector<std::string> headers = {"Signal"};
    std::vector<const StatisticsRow *> kept;
    for (const std::string_view quantity : quantities) {
        const auto found = std::ranges::find(statistics, quantity, &StatisticsRow::Name);
        if (found != statistics.end()) {
            headers.emplace_back(quantity);
            kept.push_back(&*found);
        }
    }
    std::vector<std::vector<std::string>> rows;
    for (std::size_t trace = 0; trace < labels.size(); ++trace) {
        std::vector<std::string> row = {labels[trace]};
        for (const StatisticsRow *quantity : kept) {
            row.push_back(trace < quantity->Cells.size() ? quantity->Cells[trace].Text : std::string(NO_VALUE));
        }
        rows.push_back(std::move(row));
    }
    return WriteTable(headers, rows);
}

std::vector<std::string> LabelNodes(const SummaryContext &context) {
    std::vector<std::string> labels;
    for (const int node : context.Nodes) {
        labels.push_back(GetVoltageLabel(context.Names, node));
    }
    return labels;
}

/**
 * @brief   The traces of the nodes in the context that a result has, as the statistics tables read them.
 * @param[in] values  The trace of every node of the result, by node number.
 */
std::vector<MeasuredTrace> CollectNodeTraces(const std::vector<std::vector<double>> &values,
                                             const SummaryContext &context) {
    std::vector<MeasuredTrace> traces;
    for (const int node : context.Nodes) {
        const std::size_t index = static_cast<std::size_t>(node);
        traces.push_back(
            {.Values = index < values.size() ? std::span<const double>(values[index]) : std::span<const double>(),
             .Unit = "V"});
    }
    return traces;
}

/**
 * @brief   A row of the first or last value of each trace, which the tables of statistics do not hold.
 */
StatisticsRow EndValueRow(const std::string_view name, const std::vector<MeasuredTrace> &traces, const bool is_last) {
    StatisticsRow row{.Name = std::string(name), .Cells = {}};
    for (const MeasuredTrace &trace : traces) {
        if (trace.Values.empty()) {
            row.Cells.push_back({.Text = std::string(NO_VALUE), .Tooltip = {}});
        } else {
            row.Cells.push_back(
                {.Text = Quantity(is_last ? trace.Values.back() : trace.Values.front(), trace.Unit), .Tooltip = {}});
        }
    }
    return row;
}

/**
 * @brief   Writes the dB and degrees of the entries of the matrix at one frequency.
 */
std::string WriteSMatrix(const Core::SParameters &result, const std::size_t frequency) {
    const int ports = result.GetPortCount();
    std::vector<std::vector<std::string>> rows;
    for (int row = 1; row <= ports; ++row) {
        for (int column = 1; column <= ports; ++column) {
            if (ports > MAX_FULL_MATRIX_PORTS && row != column) {
                continue;
            }
            const std::complex<double> value = result.Get(frequency, row, column);
            rows.push_back({std::format("S{}{}", row, column),
                            std::format("{} dB", Core::FormatFixed(ToDecibels(std::abs(value)), 2)),
                            std::format("{} deg", Core::FormatFixed(std::arg(value) * RADIANS_TO_DEGREES, 1))});
        }
    }
    return WriteTable({"Entry", "Magnitude", "Phase"}, rows);
}

/**
 * @brief   The line that says a stepped run is summarized by one of its steps.
 */
template <typename Result> std::string DescribeSteps(const StepFamily<Result> &family) {
    if (family.Results.size() < 2) {
        return {};
    }
    const double selected = family.Selected < family.Values.size() ? family.Values[family.Selected] : 0.0;
    return std::format("Stepped run: {} steps of {}; this summary is of {}.\n\n", family.Results.size(),
                       family.Parameter, FormatStepValue(family.Parameter, selected, family.Unit));
}

/**
 * @brief   Joins the warnings of a result as a list, or nothing when there are none.
 */
std::string WriteWarnings(const std::vector<std::string> &warnings) {
    if (warnings.empty()) {
        return {};
    }
    std::string text = "\nWarnings:\n\n";
    for (const std::string &warning : warnings) {
        text += std::format("- {}\n", warning);
    }
    return text;
}

} // namespace

/**
 * @brief   Chooses the nodes a summary reads.
 * @param[in,out] schematic   Schematic that knows which voltages are measured and names its nets.
 * @param[in]     node_count  Nodes of the result, ground included.
 * @return  The measured nodes, or the first ones when none is measured, at most MAX_SUMMARY_TRACES.
 */
SummaryContext MakeSummaryContext(Schematic &schematic, const std::size_t node_count) {
    SummaryContext context{.Names = schematic.GetConnectivity(), .Nodes = {}, .OmittedNodes = 0};
    std::vector<int> measured;
    std::vector<int> all;
    for (std::size_t node = 1; node < node_count; ++node) {
        all.push_back(static_cast<int>(node));
        if (schematic.IsVoltageMeasured(static_cast<int>(node))) {
            measured.push_back(static_cast<int>(node));
        }
    }
    const std::vector<int> &wanted = measured.empty() ? all : measured;
    const std::size_t kept = std::min(wanted.size(), MAX_SUMMARY_TRACES);
    context.Nodes.assign(wanted.begin(), wanted.begin() + static_cast<std::ptrdiff_t>(kept));
    context.OmittedNodes = all.size() - kept;
    return context;
}

/**
 * @brief   Lists the DC voltage of every node and the current of every part, which is the whole of an operating point.
 * @param[in] result   The operating point.
 * @param[in] context  Names of the nodes.
 * @return  Two tables, and the warnings; each is cut at MAX_SUMMARY_LISTED rows.
 */
std::string SummarizeOperatingPoint(const Core::OperatingPoint &result, const SummaryContext &context) {
    std::vector<std::vector<std::string>> voltages;
    for (std::size_t node = 0; node < result.NodeVoltages.size() && voltages.size() < MAX_SUMMARY_LISTED; ++node) {
        const std::string label = node == 0 ? "0 (ground)" : GetVoltageLabel(context.Names, static_cast<int>(node));
        voltages.push_back({label, Quantity(result.NodeVoltages[node], "V")});
    }
    std::string text = WriteTable({"Node", "Voltage"}, voltages);
    if (!result.Currents.empty()) {
        std::vector<std::vector<std::string>> currents;
        for (const Core::ComponentCurrent &current : result.Currents) {
            if (currents.size() == MAX_SUMMARY_LISTED) {
                break;
            }
            currents.push_back({current.Name, Quantity(current.Current, "A")});
        }
        text += "\n" + WriteTable({"Part", "Current"}, currents);
        text += "\nCurrents are positive through a two-terminal part from its first terminal to its second, and into "
                "each terminal of a transistor.\n";
    }
    return text + WriteWarnings(result.Warnings);
}

/**
 * @brief   Reads each node over the whole run: the statistics of an oscilloscope, and where the trace starts and ends.
 * @param[in] result   The transient.
 * @param[in] context  The nodes to read.
 * @return  The span and number of points, then a table with a row per node; a run that was stopped says so.
 */
std::string SummarizeTransient(const Core::Transient &result, const SummaryContext &context) {
    if (result.Times.empty()) {
        return "The transient holds no points.\n";
    }
    std::string text = std::format("{}s to {}s, {} points{}.\n\n", Core::FormatValue(result.Times.front()),
                                   Core::FormatValue(result.Times.back()), result.Times.size(),
                                   result.IsComplete ? "" : " (the run was stopped before its end)");
    const std::vector<MeasuredTrace> traces = CollectNodeTraces(result.NodeVoltages, context);
    std::vector<StatisticsRow> rows = BuildTransientStatistics(
        result.Times, traces, {.From = result.Times.front(), .To = result.Times.back()}, std::nullopt);
    rows.push_back(EndValueRow("First", traces, false));
    rows.push_back(EndValueRow("Last", traces, true));
    constexpr auto QUANTITIES =
        std::to_array<std::string_view>({"Min", "Max", "Mean", "RMS", "Pk-Pk", "Freq", "First", "Last"});
    text += WriteStatisticsTable(rows, LabelNodes(context), QUANTITIES);
    return text + OmittedNote(context) + WriteWarnings(result.Warnings);
}

/**
 * @brief   Reads the Bode figures of each node over the whole sweep.
 * @param[in] result   The AC sweep, with gains in decibels and phases in degrees.
 * @param[in] context  The nodes to read.
 * @return  The frequency range, then a table with a row per node: peak gain, -3 dB cutoffs, unity-gain frequency and
 *          phase margin.
 */
std::string SummarizeACSweep(const Core::ACSweep &result, const SummaryContext &context) {
    std::string text = std::format("{}.\n\n", FrequencyRange(result.Frequencies));
    std::vector<BodeTrace> traces;
    for (const int node : context.Nodes) {
        const std::size_t index = static_cast<std::size_t>(node);
        if (index < result.NodeMagnitudesDecibels.size() && index < result.NodePhasesDegrees.size()) {
            traces.push_back({.MagnitudesDecibels = result.NodeMagnitudesDecibels[index],
                              .PhasesDegrees = result.NodePhasesDegrees[index]});
        }
    }
    const std::vector<StatisticsRow> rows = BuildBodeStatistics(result.Frequencies, traces, std::nullopt);
    constexpr auto QUANTITIES =
        std::to_array<std::string_view>({"Peak gain", "Peak at", "-3 dB low", "-3 dB high", "0 dB at", "Phase margin"});
    text += WriteStatisticsTable(rows, LabelNodes(context), QUANTITIES);
    return text + "\nGains are the voltage of the node in decibels, as plotted, in volts for a 1 V input.\n" +
           OmittedNote(context);
}

/**
 * @brief   Reads each node over the swept range: its extremes, mean and the values at both ends of the sweep.
 * @param[in] result   The DC sweep; the first curve of a stepped sweep is read.
 * @param[in] context  The nodes to read.
 * @return  What was swept and over which range, then a table with a row per node.
 */
std::string SummarizeDCSweep(const Core::DCSweep &result, const SummaryContext &context) {
    if (result.SweptValues.empty() || result.Curves.empty()) {
        return "The sweep holds no points.\n";
    }
    std::string text = std::format(
        "Swept {} from {}{} to {}{}, {} points.\n\n", result.SweptSource, Core::FormatValue(result.SweptValues.front()),
        result.SweptUnit, Core::FormatValue(result.SweptValues.back()), result.SweptUnit, result.SweptValues.size());
    if (result.Curves.size() > 1) {
        text += std::format("The sweep is stepped over {}: {} curves; this table reads the first, at {}{}.\n\n",
                            result.SteppedSource, result.Curves.size(),
                            Core::FormatValue(result.Curves.front().StepValue), result.SteppedUnit);
    }
    const std::vector<MeasuredTrace> traces = CollectNodeTraces(result.Curves.front().NodeVoltages, context);
    std::vector<SweptTrace> swept;
    for (const MeasuredTrace &trace : traces) {
        swept.push_back({.Values = trace.Values, .Unit = trace.Unit});
    }
    std::vector<StatisticsRow> rows =
        BuildSweepStatistics(result.SweptValues, result.SweptUnit, swept,
                             {.From = result.SweptValues.front(), .To = result.SweptValues.back()}, std::nullopt);
    rows.push_back(EndValueRow("At start", traces, false));
    rows.push_back(EndValueRow("At end", traces, true));
    constexpr auto QUANTITIES = std::to_array<std::string_view>({"Min", "Max", "Mean", "At start", "At end"});
    text += WriteStatisticsTable(rows, LabelNodes(context), QUANTITIES);
    return text + OmittedNote(context);
}

/**
 * @brief   Gives the RMS noise at the output and at the input, and the parts that add most to the output.
 * @param[in] result   The noise analysis.
 * @param[in] context  Names of the nodes.
 */
std::string SummarizeNoise(const Core::Noise &result, const SummaryContext &context) {
    std::string text =
        std::format("Output {}, input source {}, {}.\n\n", GetVoltageLabel(context.Names, result.OutputNode),
                    result.InputSource, FrequencyRange(result.Frequencies));
    text += std::format("- RMS noise at the output: {}\n", Quantity(result.OutputTotal, "V"));
    if (!result.InputUnit.empty()) {
        text += std::format("- RMS noise referred to the input: {}\n", Quantity(result.InputTotal, result.InputUnit));
    }
    if (result.Contributions.empty()) {
        return text + "\nNo part adds noise.\n";
    }
    std::vector<const Core::NoiseContribution *> ranked;
    for (const Core::NoiseContribution &contribution : result.Contributions) {
        ranked.push_back(&contribution);
    }
    std::ranges::stable_sort(
        ranked, [](const auto *first, const auto *second) { return first->OutputTotal > second->OutputTotal; });
    // Powers add up, so the shares of all parts make the whole
    const double output_power = result.OutputTotal * result.OutputTotal;
    std::vector<std::vector<std::string>> rows;
    for (const Core::NoiseContribution *contribution : ranked) {
        if (rows.size() == MAX_SUMMARY_ROWS) {
            break;
        }
        const double share =
            output_power > 0.0 ? contribution->OutputTotal * contribution->OutputTotal / output_power : 0.0;
        rows.push_back({contribution->Name, Quantity(contribution->OutputTotal, "V"),
                        std::format("{} %", Core::FormatFixed(share * 100.0, 1))});
    }
    text += "\nShare of each part in the output noise, largest first:\n\n";
    return text + WriteTable({"Part", "RMS at the output", "Share"}, rows);
}

/**
 * @brief   Gives the DC gain and the input and output impedances.
 */
std::string SummarizeTransferFunction(const Core::TransferFunction &result, const SummaryContext &context) {
    const std::string gain_unit =
        result.InputUnit.empty() ? "" : std::format(" (output volts per {})", result.InputUnit);
    return std::format("From {} to {}.\n\n- DC gain: {}{}\n- Input impedance: {}\n- Output impedance: {}\n",
                       result.InputSource, GetVoltageLabel(context.Names, result.OutputNode),
                       Core::FormatMeasurement(result.Gain), gain_unit, Quantity(result.InputImpedance, "Ohm"),
                       Quantity(result.OutputImpedance, "Ohm"));
}

/**
 * @brief   Ranks the parts by how much the DC voltage of the output moves for a 1 % change of their value.
 */
std::string SummarizeSensitivity(const Core::Sensitivity &result, const SummaryContext &context) {
    std::string text = std::format("DC sensitivity of {}, the parts that move it most first.\n\n",
                                   GetVoltageLabel(context.Names, result.OutputNode));
    std::vector<std::vector<std::string>> rows;
    for (const SensitivityRow &row : MakeSensitivityRows(result)) {
        if (rows.size() == MAX_SUMMARY_ROWS) {
            break;
        }
        rows.push_back({row.Part, row.Value, row.Absolute, row.Normalized});
    }
    return text + WriteTable({"Part", "Value", "Per unit of the value", "Per 1 % of the value"}, rows);
}

/**
 * @brief   Ranks the parts by how much they move the response at the -3 dB frequency, the one the schematic colors by.
 */
std::string SummarizeACSensitivity(const Core::ACSensitivity &result, const SummaryContext &context) {
    if (result.Frequencies.empty()) {
        return "The analysis holds no frequencies.\n";
    }
    const std::size_t corner = FindCornerFrequencyIndex(result);
    std::string text = std::format("AC sensitivity of {} to {}, {}; the table is read at {}Hz, the -3 dB point.\n\n",
                                   GetVoltageLabel(context.Names, result.OutputNode), result.InputSource,
                                   FrequencyRange(result.Frequencies), Core::FormatValue(result.Frequencies[corner]));
    std::vector<std::vector<std::string>> rows;
    for (const ACSensitivityRow &row : MakeACSensitivityRows(result, corner)) {
        if (rows.size() == MAX_SUMMARY_ROWS) {
            break;
        }
        rows.push_back({row.Part, row.Value, row.Magnitude, row.Phase});
    }
    text += WriteTable({"Part", "Value", "Gain per 1 %", "Phase per 1 %"}, rows);
    return text + WriteWarnings(result.Warnings);
}

/**
 * @brief   Lists the poles and zeros of the transfer function.
 */
std::string SummarizePoleZero(const Core::PoleZero &result, const SummaryContext &context) {
    std::string text = std::format("Transfer from {} to {}; roots in rad/s.\n\n", result.InputSource,
                                   GetVoltageLabel(context.Names, result.OutputNode));
    std::vector<std::vector<std::string>> rows;
    for (const PoleZeroRow &row : MakePoleZeroRows(result)) {
        if (rows.size() == MAX_SUMMARY_LISTED) {
            break;
        }
        rows.push_back({row.Kind, row.Real, row.Imaginary, row.Frequency, row.Quality, row.Damping,
                        row.IsRightHalfPlane ? "yes" : "no"});
    }
    if (rows.empty()) {
        text += "The transfer function has no poles or zeros.\n";
    } else {
        text +=
            WriteTable({"Kind", "Real", "Imaginary", "Natural frequency", "Q", "Damping", "Right half-plane"}, rows);
    }
    const std::string cancelled = DescribeCancelledPairs(result.CancelledPairs);
    if (!cancelled.empty()) {
        text += std::format("\n{}\n", cancelled);
    }
    return text + WriteWarnings(result.Warnings);
}

/**
 * @brief   Lists the S-parameters at the first, middle and last frequency, and the stability of a two-port.
 */
std::string SummarizeSParameters(const Core::SParameters &result) {
    if (result.Frequencies.empty() || result.GetPortCount() == 0) {
        return "The analysis holds no frequencies.\n";
    }
    std::string text = std::format("{} ports, {}.\n\n", result.GetPortCount(), FrequencyRange(result.Frequencies));
    std::vector<std::size_t> shown = {0, result.Frequencies.size() / 2, result.Frequencies.size() - 1};
    shown.erase(std::ranges::unique(shown).begin(), shown.end());
    for (const std::size_t frequency : shown) {
        text += std::format("At {}Hz:\n\n{}\n", Core::FormatValue(result.Frequencies[frequency]),
                            WriteSMatrix(result, frequency));
    }
    if (IsTwoPort(result)) {
        text += std::format("{}\n", DescribeStability(ComputeTwoPortSweep(result)));
    }
    return text + WriteWarnings(result.Warnings);
}

/**
 * @brief   Writes the truth table, cut at MAX_TRUTH_TABLE_ROWS combinations.
 */
std::string SummarizeTruthTable(const Core::TruthTable &result, const Core::Circuit &circuit,
                                const SummaryContext &context) {
    std::vector<std::string> output_labels;
    for (const int node : result.OutputNodes) {
        output_labels.push_back(GetVoltageLabel(context.Names, node));
    }
    Core::TruthTable shown = result;
    std::string note;
    if (shown.Rows.size() > MAX_TRUTH_TABLE_ROWS) {
        note = std::format("\nOnly the first {} of {} combinations are listed.\n", MAX_TRUTH_TABLE_ROWS,
                           shown.Rows.size());
        shown.Rows.resize(MAX_TRUTH_TABLE_ROWS);
    }
    return FormatTruthTableMarkdown(shown, output_labels, FindLogicNodes(circuit)) + note +
           WriteWarnings(result.Warnings);
}

/**
 * @brief   Gives the DC value, the fundamental and the THD of each node.
 */
std::string SummarizeHarmonicBalance(const Core::HarmonicBalance &result, const SummaryContext &context) {
    std::string text = std::format("Fundamental {}Hz, {} harmonics, {} iterations{}.\n\n",
                                   Core::FormatValue(result.FundamentalFrequency),
                                   result.Frequencies.empty() ? 0 : result.Frequencies.size() - 1, result.Iterations,
                                   result.Strategy.empty() ? "" : std::format(" ({})", result.Strategy));
    std::vector<std::vector<std::string>> rows;
    for (const int node : context.Nodes) {
        const std::size_t index = static_cast<std::size_t>(node);
        if (index >= result.NodePhasors.size()) {
            continue;
        }
        const HarmonicTraceSummary summary =
            BuildHarmonicTraceSummary(GetVoltageLabel(context.Names, node), "V", false, result.NodePhasors[index],
                                      result.Frequencies, HarmonicAmplitudeUnit::PEAK);
        const std::string fundamental = summary.Rows.size() > FUNDAMENTAL_ROW
                                            ? Quantity(summary.Rows[FUNDAMENTAL_ROW].MagnitudeLinear, "V")
                                            : std::string(NO_VALUE);
        rows.push_back({summary.Label, Quantity(summary.DcValue, "V"), fundamental,
                        std::format("{} %", Core::FormatFixed(summary.ThdPercent, 2))});
    }
    text += WriteTable({"Signal", "DC", "Fundamental (peak)", "THD"}, rows);
    return text + OmittedNote(context) + WriteWarnings(result.Warnings);
}

/**
 * @brief   Gives the nominal value and the spread over the runs of each measured quantity.
 */
std::string SummarizeMonteCarlo(const MonteCarloTable &result, const SummaryContext &context) {
    const std::size_t failed =
        static_cast<std::size_t>(std::ranges::count(result.Runs, true, &MonteCarloRun::IsFailed));
    std::string text = std::format("{} runs of {} asked for after the nominal one, {} failed; varied parts: ",
                                   result.Runs.empty() ? 0 : result.Runs.size() - 1, result.RequestedRuns, failed);
    for (std::size_t part = 0; part < result.Parts.size(); ++part) {
        text += std::format("{}{}", part == 0 ? "" : ", ", result.Parts[part]);
    }
    text += ".\n\n";
    std::vector<std::vector<std::string>> rows;
    for (std::size_t column = 0; column < result.Columns.size() && rows.size() < MAX_SUMMARY_LISTED; ++column) {
        const MeasureColumn &measured = result.Columns[column];
        const std::string unit = GetMeasureUnit(measured.Kind);
        const std::string subject = measured.Current.empty() ? GetVoltageLabel(context.Names, measured.Node)
                                                             : std::format("I({})", measured.Current);
        const std::optional<SampleStatistics> statistics = ComputeSampleStatistics(CollectMeasure(result, column));
        const std::optional<double> nominal = result.Runs.empty() || column >= result.Runs.front().Measures.size()
                                                  ? std::nullopt
                                                  : result.Runs.front().Measures[column];
        if (!statistics) {
            continue;
        }
        rows.push_back({std::format("{} {}", subject, GetMeasureName(measured.Kind)),
                        nominal ? Quantity(*nominal, unit) : std::string(NO_VALUE), Quantity(statistics->Mean, unit),
                        Quantity(statistics->StandardDeviation, unit), Quantity(statistics->Minimum, unit),
                        Quantity(statistics->Maximum, unit)});
    }
    text += WriteTable({"Quantity", "Nominal", "Mean", "Std deviation", "Min", "Max"}, rows);
    return text + WriteWarnings(ListMonteCarloWarnings(result));
}

/**
 * @brief   Summarizes the last result of the analysis the settings select.
 * @param[in,out] schematic  Schematic whose settings select the analysis and whose nets and measured nodes name and
 *                           choose what is read.
 * @param[in]     results    The results of the last runs.
 * @return  Markdown, or no value when the selected analysis has no result. A Monte Carlo run is summarized as such, and
 *          a stepped run says which step the figures are of.
 */
std::optional<std::string> SummarizeLastResult(Schematic &schematic, const ResultStore &results) {
    const SimulationSettings &settings = schematic.GetSimulationSettings();
    if (IsMonteCarloRun(settings) && results.GetMonteCarlo()) {
        return SummarizeMonteCarlo(*results.GetMonteCarlo(), MakeSummaryContext(schematic, 0));
    }
    switch (settings.Selected) {
    case Analysis::OPERATING_POINT:
        if (const auto &result = results.GetOperatingPoint()) {
            const std::string steps =
                results.GetOperatingPointSteps() ? DescribeSteps(*results.GetOperatingPointSteps()) : "";
            return steps + SummarizeOperatingPoint(*result, MakeSummaryContext(schematic, 0));
        }
        break;
    case Analysis::TRANSIENT:
        if (const auto &result = results.GetTransient()) {
            const std::string steps = results.GetTransientSteps() ? DescribeSteps(*results.GetTransientSteps()) : "";
            return steps + SummarizeTransient(*result, MakeSummaryContext(schematic, result->NodeVoltages.size()));
        }
        break;
    case Analysis::AC_SWEEP:
        if (const auto &result = results.GetACSweep()) {
            const std::string steps = results.GetACSweepSteps() ? DescribeSteps(*results.GetACSweepSteps()) : "";
            return steps +
                   SummarizeACSweep(*result, MakeSummaryContext(schematic, result->NodeMagnitudesDecibels.size()));
        }
        break;
    case Analysis::DC_SWEEP:
        if (const auto &result = results.GetDCSweep()) {
            const std::size_t nodes = result->Curves.empty() ? 0 : result->Curves.front().NodeVoltages.size();
            return SummarizeDCSweep(*result, MakeSummaryContext(schematic, nodes));
        }
        break;
    case Analysis::NOISE:
        if (const auto &result = results.GetNoise()) {
            return SummarizeNoise(*result, MakeSummaryContext(schematic, 0));
        }
        break;
    case Analysis::TRANSFER_FUNCTION:
        if (const auto &result = results.GetTransferFunction()) {
            return SummarizeTransferFunction(*result, MakeSummaryContext(schematic, 0));
        }
        break;
    case Analysis::SENSITIVITY:
        if (settings.SensitivityType == SensitivityKind::AC && results.GetACSensitivity()) {
            return SummarizeACSensitivity(*results.GetACSensitivity(), MakeSummaryContext(schematic, 0));
        }
        if (settings.SensitivityType == SensitivityKind::DC && results.GetSensitivity()) {
            return SummarizeSensitivity(*results.GetSensitivity(), MakeSummaryContext(schematic, 0));
        }
        break;
    case Analysis::POLE_ZERO:
        if (const auto &result = results.GetPoleZero()) {
            return SummarizePoleZero(*result, MakeSummaryContext(schematic, 0));
        }
        break;
    case Analysis::S_PARAMETERS:
        if (const auto &result = results.GetSParameters()) {
            return SummarizeSParameters(*result);
        }
        break;
    case Analysis::TRUTH_TABLE:
        if (const auto &result = results.GetTruthTable()) {
            return SummarizeTruthTable(*result, schematic.BuildCircuit(), MakeSummaryContext(schematic, 0));
        }
        break;
    case Analysis::HARMONIC_BALANCE:
        if (const auto &result = results.GetHarmonicBalance()) {
            return SummarizeHarmonicBalance(*result, MakeSummaryContext(schematic, result->NodePhasors.size()));
        }
        break;
    default:
        break;
    }
    return std::nullopt;
}

} // namespace GUI
