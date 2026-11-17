/**
 * @file    simulation_summary.cpp
 * @brief   Turns the result of an analysis into the compact figures of an MCP answer, or into its full data.
 */

#include "mcp/simulation_summary.h"

#include "mcp/signal_measures.h"
#include "nodalys/exporters.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <format>
#include <numbers>
#include <string>
#include <utility>
#include <variant>

namespace Nodalys {

namespace {

/// Significant digits of the figures in an answer: enough for a design decision, short for the AI's context.
constexpr int SIGNIFICANT_DIGITS = 6;
/// Currents of an operating point an answer lists; the rest are counted.
constexpr std::size_t MAX_LISTED_CURRENTS = 40;
/// Elements of a sensitivity an answer lists, the largest first.
constexpr std::size_t MAX_LISTED_ELEMENTS = 10;
/// Poles, and zeros, an answer lists.
constexpr std::size_t MAX_LISTED_ROOTS = 24;
/// Harmonics of a node an answer lists, from the fundamental up.
constexpr std::size_t MAX_LISTED_HARMONICS = 5;
/// Noise contributors an answer lists, the largest first.
constexpr std::size_t MAX_LISTED_CONTRIBUTORS = 5;

/// Rounds a figure to the significant digits of the answers; a value that is not finite becomes null in the JSON.
nlohmann::json Round(const double value) {
    if (!std::isfinite(value)) {
        return nullptr;
    }
    return std::stod(std::format("{:.{}g}", value, SIGNIFICANT_DIGITS));
}

nlohmann::json Round(const std::optional<double> value) {
    return value ? Round(*value) : nlohmann::json(nullptr);
}

nlohmann::json MakeRange(const std::vector<double> &frequencies) {
    if (frequencies.empty()) {
        return nullptr;
    }
    return {{"start_hz", Round(frequencies.front())},
            {"stop_hz", Round(frequencies.back())},
            {"points", frequencies.size()}};
}

/// The column of per-node data for a node, or none when the result has no such node.
template <typename Value>
const std::vector<Value> *FindNodeColumn(const std::vector<std::vector<Value>> &columns, const NodeId node) {
    if (node < 0 || static_cast<std::size_t>(node) >= columns.size()) {
        return nullptr;
    }
    return &columns[static_cast<std::size_t>(node)];
}

/// Describes the items of a list with the largest keys, as many as the limit allows, largest first.
template <typename Item, typename Key>
nlohmann::json ListLargest(std::vector<Item> items, const std::size_t limit, Key key, auto describe) {
    std::ranges::sort(items, [&](const Item &a, const Item &b) { return key(a) > key(b); });
    nlohmann::json list = nlohmann::json::array();
    for (std::size_t index = 0; index < items.size() && index < limit; ++index) {
        list.push_back(describe(items[index]));
    }
    return list;
}

nlohmann::json DescribeRoots(const std::vector<std::complex<double>> &roots) {
    constexpr double RADIANS_PER_CYCLE = 2.0 * std::numbers::pi;
    nlohmann::json list = nlohmann::json::array();
    for (std::size_t index = 0; index < roots.size() && index < MAX_LISTED_ROOTS; ++index) {
        list.push_back({{"real", Round(roots[index].real())},
                        {"imag", Round(roots[index].imag())},
                        {"frequency_hz", Round(std::abs(roots[index]) / RADIANS_PER_CYCLE)}});
    }
    return list;
}

/// Gives the figures of the result of each analysis, for the nodes the caller asked about.
class Summarizer {
public:
    Summarizer(const NodeLabels &labels, const std::vector<NodeId> &nodes) : m_Labels(labels), m_Nodes(nodes) {}

    nlohmann::json operator()(const OperatingPointResult &result) const {
        nlohmann::json voltages = nlohmann::json::object();
        for (const NodeId node : m_Nodes) {
            if (node >= 0 && static_cast<std::size_t>(node) < result.NodeVoltages.size()) {
                voltages[m_Labels.GetName(node)] = Round(result.NodeVoltages[static_cast<std::size_t>(node)]);
            }
        }
        nlohmann::json currents = nlohmann::json::object();
        for (std::size_t index = 0; index < result.Currents.size() && index < MAX_LISTED_CURRENTS; ++index) {
            currents[result.Currents[index].Name] = Round(result.Currents[index].Current);
        }
        return {{"analysis", "op"},
                {"node_voltages_v", std::move(voltages)},
                {"currents_a", std::move(currents)},
                {"currents_omitted",
                 result.Currents.size() > MAX_LISTED_CURRENTS ? result.Currents.size() - MAX_LISTED_CURRENTS : 0},
                {"warnings", nlohmann::json(result.Warnings)}};
    }

    nlohmann::json operator()(const TransientResult &result) const {
        nlohmann::json figures = nlohmann::json::object();
        for (const NodeId node : m_Nodes) {
            const auto *column = FindNodeColumn(result.NodeVoltages, node);
            const auto measured = column ? MeasureWaveform(result.Times, *column) : std::nullopt;
            if (measured) {
                figures[m_Labels.GetName(node)] = {{"min_v", Round(measured->Minimum)},
                                                   {"max_v", Round(measured->Maximum)},
                                                   {"peak_to_peak_v", Round(measured->Maximum - measured->Minimum)},
                                                   {"final_v", Round(measured->Final)},
                                                   {"mean_v", Round(measured->Mean)},
                                                   {"rms_v", Round(measured->Rms)}};
            }
        }
        return {{"analysis", "tran"},
                {"points", result.Times.size()},
                {"start_time_s", Round(result.Times.empty() ? 0.0 : result.Times.front())},
                {"stop_time_s", Round(result.Times.empty() ? 0.0 : result.Times.back())},
                {"is_complete", result.IsComplete},
                {"nodes", std::move(figures)},
                {"warnings", nlohmann::json(result.Warnings)}};
    }

    nlohmann::json operator()(const DcSweepResult &result) const {
        nlohmann::json figures = nlohmann::json::object();
        for (const NodeId node : m_Nodes) {
            const auto described = DescribeSweepNode(result, node);
            if (!described.is_null()) {
                figures[m_Labels.GetName(node)] = described;
            }
        }
        nlohmann::json summary = {{"analysis", "dc"},
                                  {"swept_source", result.SweptSource},
                                  {"points", result.SweptValues.size()},
                                  {"nodes", std::move(figures)}};
        if (!result.SweptValues.empty()) {
            summary["sweep_start"] = Round(result.SweptValues.front());
            summary["sweep_stop"] = Round(result.SweptValues.back());
        }
        if (!result.SteppedSource.empty()) {
            summary["stepped_source"] = result.SteppedSource;
            summary["curves"] = result.Curves.size();
            summary["note"] = "min and max cover all the curves of the stepped source";
        }
        return summary;
    }

    nlohmann::json operator()(const AcSweepResult &result) const {
        nlohmann::json figures = nlohmann::json::object();
        for (const NodeId node : m_Nodes) {
            const auto *gains = FindNodeColumn(result.NodeMagnitudesDecibels, node);
            const auto *phases = FindNodeColumn(result.NodePhasesDegrees, node);
            const auto measured =
                (gains && phases) ? MeasureResponse(result.Frequencies, *gains, *phases) : std::nullopt;
            if (!measured) {
                continue;
            }
            std::optional<double> phase_margin;
            if (measured->PhaseAtUnityGainDegrees) {
                phase_margin = 180.0 + *measured->PhaseAtUnityGainDegrees;
            }
            figures[m_Labels.GetName(node)] = {{"gain_at_start_db", Round(gains->front())},
                                               {"gain_at_stop_db", Round(gains->back())},
                                               {"phase_at_start_deg", Round(phases->front())},
                                               {"phase_at_stop_deg", Round(phases->back())},
                                               {"peak_gain_db", Round(measured->PeakGainDecibels)},
                                               {"peak_frequency_hz", Round(measured->PeakFrequency)},
                                               {"lower_cutoff_3db_hz", Round(measured->LowerCutoffFrequency)},
                                               {"upper_cutoff_3db_hz", Round(measured->UpperCutoffFrequency)},
                                               {"unity_gain_hz", Round(measured->UnityGainFrequency)},
                                               {"phase_at_unity_gain_deg", Round(measured->PhaseAtUnityGainDegrees)},
                                               {"phase_margin_deg", Round(phase_margin)}};
        }
        return {{"analysis", "ac"},
                {"frequencies", MakeRange(result.Frequencies)},
                {"nodes", std::move(figures)},
                {"note",
                 "gain is in dB relative to the AC magnitude of the source; phase_margin_deg is 180 + the phase "
                 "at unity gain and means something only when the node is a loop gain"}};
    }

    nlohmann::json operator()(const TransferFunctionResult &result) const {
        return {{"analysis", "tf"},
                {"input_source", result.InputSource},
                {"output_positive", m_Labels.GetName(result.OutputPositiveNode)},
                {"output_negative", m_Labels.GetName(result.OutputNegativeNode)},
                {"gain", Round(result.Gain)},
                {"gain_unit", result.GainUnit},
                {"input_impedance_ohm", Round(result.InputImpedance)},
                {"output_impedance_ohm", Round(result.OutputImpedance)}};
    }

    nlohmann::json operator()(const NoiseResult &result) const {
        const auto contributors = ListLargest(
            result.Contributions, MAX_LISTED_CONTRIBUTORS, [](const NoiseContribution &c) { return c.OutputTotal; },
            [](const NoiseContribution &c) {
                return nlohmann::json{{"element", c.Name}, {"output_total", Round(c.OutputTotal)}};
            });
        return {
            {"analysis", "noise"},
            {"output_node", m_Labels.GetName(result.OutputNode)},
            {"input_source", result.InputSource},
            {"frequencies", MakeRange(result.Frequencies)},
            {"output_total", Round(result.OutputTotal)},
            {"input_total", Round(result.InputTotal)},
            {"input_unit", result.InputUnit},
            {"output_density_at_start", Round(result.OutputDensities.empty() ? 0.0 : result.OutputDensities.front())},
            {"output_density_at_stop", Round(result.OutputDensities.empty() ? 0.0 : result.OutputDensities.back())},
            {"largest_contributors", contributors}};
    }

    nlohmann::json operator()(const SParameterResult &result) const {
        nlohmann::json entries = nlohmann::json::object();
        const int ports = result.GetPortCount();
        for (int row = 1; row <= ports; ++row) {
            for (int column = 1; column <= ports; ++column) {
                entries[std::format("S{}{}", row, column)] = DescribeSEntry(result, row, column);
            }
        }
        return {{"analysis", "sp"},
                {"ports", ports},
                {"reference_ohms", result.ReferenceOhms},
                {"frequencies", MakeRange(result.Frequencies)},
                {"magnitudes", std::move(entries)},
                {"warnings", nlohmann::json(result.Warnings)}};
    }

    nlohmann::json operator()(const PoleZeroResult &result) const {
        return {{"analysis", "pz"},
                {"pole_count", result.Poles.size()},
                {"zero_count", result.Zeros.size()},
                {"poles", DescribeRoots(result.Poles)},
                {"zeros", DescribeRoots(result.Zeros)},
                {"unit", "real and imag in rad/s"},
                {"warnings", nlohmann::json(result.Warnings)}};
    }

    nlohmann::json operator()(const SensitivityResult &result) const {
        const std::vector<std::pair<std::string, double>> vectors(result.Vectors.begin(), result.Vectors.end());
        const auto largest = ListLargest(
            vectors, MAX_LISTED_ELEMENTS, [](const auto &entry) { return std::abs(entry.second); },
            [](const auto &entry) {
                return nlohmann::json{{"element", entry.first}, {"sensitivity", Round(entry.second)}};
            });
        return {{"analysis", "sens"},
                {"output_positive", m_Labels.GetName(result.OutputPositiveNode)},
                {"output_negative", m_Labels.GetName(result.OutputNegativeNode)},
                {"element_count", vectors.size()},
                {"largest_sensitivities", largest}};
    }

    nlohmann::json operator()(const AcSensitivityResult &result) const {
        struct Peak {
            const AcElementSensitivity *Element = nullptr;
            double PeakDecibelsPerPercent = 0.0;
            double Frequency = 0.0;
        };
        std::vector<Peak> peaks;
        for (const AcElementSensitivity &element : result.Elements) {
            Peak peak{.Element = &element};
            for (std::size_t index = 0; index < element.MagnitudeDecibelsPerPercent.size(); ++index) {
                const auto &value = element.MagnitudeDecibelsPerPercent[index];
                if (value && std::abs(*value) > std::abs(peak.PeakDecibelsPerPercent)) {
                    peak.PeakDecibelsPerPercent = *value;
                    peak.Frequency = result.Frequencies[index];
                }
            }
            peaks.push_back(peak);
        }
        const auto largest = ListLargest(
            peaks, MAX_LISTED_ELEMENTS, [](const Peak &p) { return std::abs(p.PeakDecibelsPerPercent); },
            [](const Peak &p) {
                return nlohmann::json{{"element", p.Element->Name},
                                      {"value", Round(p.Element->Value)},
                                      {"peak_gain_change_db_per_percent", Round(p.PeakDecibelsPerPercent)},
                                      {"at_frequency_hz", Round(p.Frequency)}};
            });
        return {{"analysis", "sens_ac"},
                {"output_positive", m_Labels.GetName(result.OutputPositiveNode)},
                {"output_negative", m_Labels.GetName(result.OutputNegativeNode)},
                {"input_source", result.InputSource},
                {"frequencies", MakeRange(result.Frequencies)},
                {"element_count", result.Elements.size()},
                {"largest_sensitivities", largest},
                {"warnings", nlohmann::json(result.Warnings)}};
    }

    nlohmann::json operator()(const HarmonicBalanceResult &result) const {
        nlohmann::json figures = nlohmann::json::object();
        for (const NodeId node : m_Nodes) {
            const auto *phasors = FindNodeColumn(result.NodePhasors, node);
            if (phasors == nullptr || phasors->empty()) {
                continue;
            }
            nlohmann::json harmonics = nlohmann::json::array();
            for (std::size_t order = 1; order < phasors->size() && order <= MAX_LISTED_HARMONICS; ++order) {
                harmonics.push_back(Round(std::abs((*phasors)[order])));
            }
            figures[m_Labels.GetName(node)] = {{"dc_v", Round((*phasors)[0].real())},
                                               {"harmonic_amplitudes_v", std::move(harmonics)},
                                               {"thd_percent", Round(ComputeThd(*phasors) * 100.0)}};
        }
        return {{"analysis", "hb"},
                {"fundamental_hz", Round(result.Frequencies.size() > 1 ? result.Frequencies[1] : 0.0)},
                {"harmonics", result.Frequencies.empty() ? 0 : result.Frequencies.size() - 1},
                {"iterations", result.Iterations},
                {"strategy", result.Strategy},
                {"nodes", std::move(figures)},
                {"note", "harmonic_amplitudes_v lists the peak amplitudes of harmonics 1 to 5"},
                {"warnings", nlohmann::json(result.Warnings)}};
    }

private:
    nlohmann::json DescribeSweepNode(const DcSweepResult &result, const NodeId node) const {
        double minimum = 0.0;
        double maximum = 0.0;
        double at_minimum = 0.0;
        double at_maximum = 0.0;
        bool is_first = true;
        for (const DcSweepCurve &curve : result.Curves) {
            const auto *column = FindNodeColumn(curve.NodeVoltages, node);
            if (column == nullptr) {
                continue;
            }
            for (std::size_t index = 0; index < column->size() && index < result.SweptValues.size(); ++index) {
                const double value = (*column)[index];
                if (is_first || value < minimum) {
                    minimum = value;
                    at_minimum = result.SweptValues[index];
                }
                if (is_first || value > maximum) {
                    maximum = value;
                    at_maximum = result.SweptValues[index];
                }
                is_first = false;
            }
        }
        if (is_first) {
            return nullptr;
        }
        const auto &first_column = result.Curves.front().NodeVoltages[static_cast<std::size_t>(node)];
        return {{"min_v", Round(minimum)},
                {"at_sweep_value_of_min", Round(at_minimum)},
                {"max_v", Round(maximum)},
                {"at_sweep_value_of_max", Round(at_maximum)},
                {"first_v", Round(first_column.front())},
                {"last_v", Round(first_column.back())}};
    }

    static nlohmann::json DescribeSEntry(const SParameterResult &result, const int row, const int column) {
        constexpr double MIN_MAGNITUDE = 1e-15;
        double minimum = 0.0;
        double maximum = 0.0;
        double at_maximum = 0.0;
        bool is_first = true;
        for (std::size_t index = 0; index < result.Frequencies.size(); ++index) {
            const double magnitude = std::max(std::abs(result.Get(index, row, column)), MIN_MAGNITUDE);
            const double decibels = 20.0 * std::log10(magnitude);
            if (is_first || decibels < minimum) {
                minimum = decibels;
            }
            if (is_first || decibels > maximum) {
                maximum = decibels;
                at_maximum = result.Frequencies[index];
            }
            is_first = false;
        }
        return {{"min_db", Round(minimum)}, {"max_db", Round(maximum)}, {"max_at_hz", Round(at_maximum)}};
    }

    const NodeLabels &m_Labels;
    const std::vector<NodeId> &m_Nodes;
};

} // namespace

/**
 * @brief   Gives the compact figures of a result.
 * @details Per requested node: the extremes, final value, mean and RMS of a transient; the gain, the -3 dB points and
 *          the unity-gain crossing of an AC sweep; the extremes of a DC sweep. The other analyses give their own few
 *          figures, such as the gain and impedances of a transfer function or the largest sensitivities. Never a vector
 *          of points, so a transient of a million points answers in the same few lines as one of a hundred.
 * @param[in] result  Result of the analysis.
 * @param[in] labels  Names of the nodes.
 * @param[in] nodes   Nodes the figures are given for.
 * @return  A JSON object with an "analysis" field naming the kind.
 */
nlohmann::json SummarizeResult(const SimulationResult &result, const NodeLabels &labels,
                               const std::vector<NodeId> &nodes) {
    return std::visit(Summarizer(labels, nodes), result);
}

/**
 * @brief   Gives the whole data of a result in the JSON export format of the command line, with the node names.
 * @param[in] result  Result of the analysis.
 * @param[in] labels  Names of the nodes.
 * @return  An object with "node_names", the name of each node number, and "data", the export; if the export is not
 *          valid JSON (it may hold "nan"), "data_text" holds it as text instead.
 */
nlohmann::json MakeFullData(const SimulationResult &result, const NodeLabels &labels) {
    const std::string exported = ExportJson(result);
    nlohmann::json data = nlohmann::json::parse(exported, nullptr, false);

    nlohmann::json names = nlohmann::json::object();
    for (const NodeId node : labels.GetNamedNodes()) {
        names[std::to_string(node)] = labels.GetName(node);
    }
    nlohmann::json full = {{"node_names", std::move(names)}};
    if (data.is_discarded()) {
        full["data_text"] = exported;
    } else {
        full["data"] = std::move(data);
    }
    return full;
}

} // namespace Nodalys
