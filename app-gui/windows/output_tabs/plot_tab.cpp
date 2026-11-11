/**
 * @file    plot_tab.cpp
 * @brief   The tabs of the Output window and which of them the tab bar shows.
 */

#include "windows/output_tabs/plot_tab.h"

namespace GUI {

/**
 * @brief   The position of a tab in PLOT_TABS and in a TabResults.
 * @param[in] tab  The tab.
 * @return  Its index, from 0.
 */
std::size_t ToIndex(const PlotTab tab) {
    return static_cast<std::size_t>(tab);
}

/**
 * @brief   The name of a tab on the tab bar.
 * @param[in] tab  The tab.
 * @return  Its name, such as "AC sweep".
 */
const char *GetTabLabel(const PlotTab tab) {
    switch (tab) {
    case PlotTab::OPERATING_POINT:
        return "Operating point";
    case PlotTab::TRANSIENT:
        return "Transient";
    case PlotTab::SPECTRUM:
        return "Spectrum";
    case PlotTab::AC_SWEEP:
        return "AC sweep";
    case PlotTab::DC_SWEEP:
        return "DC sweep";
    case PlotTab::NOISE:
        return "Noise";
    case PlotTab::TRANSFER_FUNCTION:
        return "Transfer function";
    case PlotTab::SENSITIVITY:
        return "Sensitivity";
    case PlotTab::POLE_ZERO:
        return "Poles and zeros";
    case PlotTab::S_PARAMETERS:
        return "S-parameters";
    case PlotTab::TRUTH_TABLE:
        return "Truth table";
    case PlotTab::HARMONIC_BALANCE:
        return "Harmonic balance";
    case PlotTab::MONTE_CARLO:
        return "Monte Carlo";
    default:
        return "";
    }
}

/**
 * @brief   Picks the tabs the bar shows: those with a result, so twelve labels never crowd and clip each other, and the
 *          one the user is on, so an edit that drops its result does not pull it away before the run that follows.
 * @param[in] has_result  Whether each tab has a result.
 * @param[in] kept        The tab the user is on, shown with or without a result, or none.
 * @return  The shown tabs and the others, each in the order of PLOT_TABS.
 */
TabListing ListTabs(const TabResults &has_result, const std::optional<PlotTab> kept) {
    TabListing listing;
    for (const PlotTab tab : PLOT_TABS) {
        if (has_result[ToIndex(tab)] || tab == kept) {
            listing.Shown.push_back(tab);
        } else {
            listing.Others.push_back(tab);
        }
    }
    return listing;
}

/**
 * @brief   The analysis whose last run a tab reports.
 * @param[in] tab  The tab.
 * @return  The analysis, or none for the spectrum, which transforms the transient, and for Monte Carlo, a run mode of
 *          the analysis it repeats.
 */
std::optional<Analysis> GetTabAnalysis(const PlotTab tab) {
    switch (tab) {
    case PlotTab::OPERATING_POINT:
        return Analysis::OPERATING_POINT;
    case PlotTab::TRANSIENT:
        return Analysis::TRANSIENT;
    case PlotTab::AC_SWEEP:
        return Analysis::AC_SWEEP;
    case PlotTab::DC_SWEEP:
        return Analysis::DC_SWEEP;
    case PlotTab::NOISE:
        return Analysis::NOISE;
    case PlotTab::TRANSFER_FUNCTION:
        return Analysis::TRANSFER_FUNCTION;
    case PlotTab::SENSITIVITY:
        return Analysis::SENSITIVITY;
    case PlotTab::POLE_ZERO:
        return Analysis::POLE_ZERO;
    case PlotTab::S_PARAMETERS:
        return Analysis::S_PARAMETERS;
    case PlotTab::TRUTH_TABLE:
        return Analysis::TRUTH_TABLE;
    case PlotTab::HARMONIC_BALANCE:
        return Analysis::HARMONIC_BALANCE;
    case PlotTab::SPECTRUM:
    case PlotTab::MONTE_CARLO:
    default:
        return std::nullopt;
    }
}

/**
 * @brief   The tab that shows the result of an analysis, or why its last run failed.
 * @param[in] analysis  The analysis.
 * @return  Its tab.
 */
PlotTab GetAnalysisTab(const Analysis analysis) {
    switch (analysis) {
    case Analysis::OPERATING_POINT:
        return PlotTab::OPERATING_POINT;
    case Analysis::TRANSIENT:
        return PlotTab::TRANSIENT;
    case Analysis::AC_SWEEP:
        return PlotTab::AC_SWEEP;
    case Analysis::DC_SWEEP:
        return PlotTab::DC_SWEEP;
    case Analysis::NOISE:
        return PlotTab::NOISE;
    case Analysis::TRANSFER_FUNCTION:
        return PlotTab::TRANSFER_FUNCTION;
    case Analysis::SENSITIVITY:
        return PlotTab::SENSITIVITY;
    case Analysis::POLE_ZERO:
        return PlotTab::POLE_ZERO;
    case Analysis::S_PARAMETERS:
        return PlotTab::S_PARAMETERS;
    case Analysis::TRUTH_TABLE:
        return PlotTab::TRUTH_TABLE;
    case Analysis::HARMONIC_BALANCE:
        return PlotTab::HARMONIC_BALANCE;
    default:
        return PlotTab::OPERATING_POINT;
    }
}

} // namespace GUI
