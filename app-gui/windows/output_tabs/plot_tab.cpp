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

} // namespace GUI
