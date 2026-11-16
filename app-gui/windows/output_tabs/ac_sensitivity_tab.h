/**
 * @file    ac_sensitivity_tab.h
 * @brief   The Sensitivity tab of the Output window for an AC sensitivity result: how the gain and the phase at the
 *          output change with the value of each part, across frequency, and a table that ranks the parts at one
 *          frequency.
 */

#ifndef IMCSIM_AC_SENSITIVITY_TAB_H
#define IMCSIM_AC_SENSITIVITY_TAB_H

#include "plots/plot_export.h"
#include "plots/plot_types.h"
#include "results/ac_sensitivity_view.h"
#include "results/result_store.h"
#include "schematic/preferences.h"
#include "schematic/schematic.h"
#include "simulation/simulation_results.h"
#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace GUI {

/**
 * @class   ACSensitivityTab
 * @brief   Plots the change of the gain and of the phase for a 1 % higher value of the checked parts on a logarithmic
 *          frequency axis, and ranks every part at the frequency of a marker that can be dragged.
 * @details The plot starts with the most influential parts checked. The marker starts at the -3 dB point of the
 *          response, so the table answers which part sets the corner; the schematic colors its parts by the same
 *          frequency.
 */
class ACSensitivityTab {
public:
    ACSensitivityTab(Schematic &schematic, ResultStore &results, Preferences &preferences);

    void Draw();

    // Export
    bool CanExport() const;
    ExportFigure BuildFigure() const;

private:
    Schematic &m_Schematic;
    ResultStore &m_Results;
    /// The wire coloring that the checkbox at the top sets to Sensitivity, or back to Plain.
    Preferences &m_Preferences;
    /// The result version the checked parts and the curves were made for.
    std::optional<std::size_t> m_LoadedVersion;
    /// Visible range of the plots, which both plots share.
    PlotSpan m_View;
    /// The parts whose curves the plots show.
    std::set<std::string> m_ShownParts;
    /// The figures of every part as curves, in the order of the parts of the result.
    std::vector<ACSensitivityCurve> m_Curves;

    // Result
    void LoadResult(const Core::ACSensitivity &sensitivity);

    // Plots
    void DrawPlots(const Core::ACSensitivity &sensitivity);
    void DrawPlot(const Core::ACSensitivity &sensitivity, bool is_phase, float height);

    // Table
    void DrawTable(const Core::ACSensitivity &sensitivity);
    void DrawFrequencyChoice(const Core::ACSensitivity &sensitivity);
};

} // namespace GUI

#endif // IMCSIM_AC_SENSITIVITY_TAB_H
