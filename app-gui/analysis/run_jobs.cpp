/**
 * @file    run_jobs.cpp
 * @brief   Building blocks that put an analysis on the worker thread: the job that simulates a copy of the circuit,
 *          the runs once per step or per Monte Carlo draw, and the hand-over of what they make to the results.
 */

#include "analysis/run_jobs.h"

namespace GUI {

/**
 * @brief   The table of a Monte Carlo analysis, with the varied parts of the circuit and no runs yet.
 */
MonteCarloTable MakeMonteCarloTable(const Core::Circuit &circuit, const Core::MonteCarloSettings &settings) {
    MonteCarloTable table{.RequestedRuns = static_cast<std::size_t>(settings.Runs)};
    for (const Core::Component *part : Core::FindVariedParts(circuit)) {
        table.Parts.push_back(part->GetName());
        table.Units.emplace_back(part->GetUnit());
    }
    return table;
}

} // namespace GUI
