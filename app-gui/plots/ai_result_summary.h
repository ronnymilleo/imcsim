/**
 * @file    ai_result_summary.h
 * @brief   Summaries of the results of each analysis as short Markdown, for pasting into a conversation with an AI.
 * @details Pure functions over the results and the figures the Output tabs already compute. A summary holds a handful
 *          of figures per trace and never a vector of points, so it stays short however long the run was.
 */

#ifndef IMCSIM_AI_RESULT_SUMMARY_H
#define IMCSIM_AI_RESULT_SUMMARY_H

#include "results/result_store.h"
#include "schematic/connectivity.h"
#include "schematic/schematic.h"
#include "schematic/simulation_settings.h"
#include "simulation/simulation_results.h"
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace GUI {

/// Most traces a summary reads: the voltages of this many nodes, so a large circuit does not bury the figures.
constexpr std::size_t MAX_SUMMARY_TRACES = 12;
/// Most rows of a ranked list, such as the sensitivities or the noise contributors.
constexpr std::size_t MAX_SUMMARY_ROWS = 10;
/// Most rows of the tables that list everything the circuit has, such as the operating point.
constexpr std::size_t MAX_SUMMARY_LISTED = 40;

/**
 * @struct  SummaryContext
 * @brief   The names of the nodes and the nodes a summary reads.
 */
struct SummaryContext {
    const Connectivity &Names;
    /// Nodes to read, in node order and without ground: the measured ones, or the first nodes when none is measured.
    std::vector<int> Nodes;
    /// How many nodes the circuit has beyond those listed, which the summary says it left out.
    std::size_t OmittedNodes = 0;
};

// Context
SummaryContext MakeSummaryContext(Schematic &schematic, std::size_t node_count);

// Summaries, one per analysis; each is Markdown that starts with a line or a table, with no heading
std::string SummarizeOperatingPoint(const Core::OperatingPoint &result, const SummaryContext &context);
std::string SummarizeTransient(const Core::Transient &result, const SummaryContext &context);
std::string SummarizeACSweep(const Core::ACSweep &result, const SummaryContext &context);
std::string SummarizeDCSweep(const Core::DCSweep &result, const SummaryContext &context);
std::string SummarizeNoise(const Core::Noise &result, const SummaryContext &context);
std::string SummarizeTransferFunction(const Core::TransferFunction &result, const SummaryContext &context);
std::string SummarizeSensitivity(const Core::Sensitivity &result, const SummaryContext &context);
std::string SummarizeACSensitivity(const Core::ACSensitivity &result, const SummaryContext &context);
std::string SummarizePoleZero(const Core::PoleZero &result, const SummaryContext &context);
std::string SummarizeSParameters(const Core::SParameters &result);
std::string SummarizeTruthTable(const Core::TruthTable &result, const Core::Circuit &circuit,
                                const SummaryContext &context);
std::string SummarizeHarmonicBalance(const Core::HarmonicBalance &result, const SummaryContext &context);
std::string SummarizeMonteCarlo(const MonteCarloTable &result, const SummaryContext &context);

// Selected analysis
std::optional<std::string> SummarizeLastResult(Schematic &schematic, const ResultStore &results);

} // namespace GUI

#endif // IMCSIM_AI_RESULT_SUMMARY_H
