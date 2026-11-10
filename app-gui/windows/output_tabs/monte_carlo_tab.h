/**
 * @file    monte_carlo_tab.h
 * @brief   The Monte Carlo tab of the Output window: the histogram of a measured quantity over the runs, its statistics
 *          and the table of runs.
 */

#ifndef IMCSIM_MONTE_CARLO_TAB_H
#define IMCSIM_MONTE_CARLO_TAB_H

#include "analysis/monte_carlo_results.h"
#include "document/schematic.h"
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace GUI {

/**
 * @class   MonteCarloTab
 * @brief   Shows how a quantity measured on a node or a current spreads over the runs of the last Monte Carlo analysis,
 * with the nominal run marked, and lists every run with the values its parts took.
 * @details The nominal run and the runs whose traces were kept are picked from the table, which shows them in the
 *          other tabs of the analysis.
 */
class MonteCarloTab {
public:
    explicit MonteCarloTab(Schematic &schematic);

    bool Draw();

    // Export
    std::string BuildCsv() const;

private:
    Schematic &m_Schematic;
    /// Node whose quantity is shown; the first measured one while the user has not picked.
    std::optional<int> m_Node;
    /// Current whose quantity is shown, in place of a node, when the user picked one.
    std::optional<std::string> m_Current;
    /// Quantity shown; the first the node offers while the user has not picked.
    std::optional<MeasureKind> m_Kind;
    RunSort m_Sort;

    std::optional<std::size_t> ResolveColumn(const MonteCarloTable &table);
    void PickTrace(const MeasureColumn &column);
    std::string LabelColumn(const MeasureColumn &column) const;
    std::string LabelTrace(const MeasureColumn &column) const;
    void DrawColumnPicker(const MonteCarloTable &table, std::size_t &column);
    void DrawHistogram(const MonteCarloTable &table, std::size_t column);
    void DrawRunTable(const MonteCarloTable &table, std::size_t column);
    void DrawStatistics(const MonteCarloTable &table, std::size_t column);
    std::optional<double> GetShownRun() const;
    void ShowRun(std::size_t run);
};

} // namespace GUI

#endif // IMCSIM_MONTE_CARLO_TAB_H
