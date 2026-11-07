/**
 * @file    plot_tab.h
 * @brief   The tabs of the Output window and which of them the tab bar shows.
 */

#ifndef IMCSIM_PLOT_TAB_H
#define IMCSIM_PLOT_TAB_H

#include <array>
#include <cstddef>
#include <optional>
#include <vector>

namespace GUI {

/**
 * @enum    PlotTab
 * @brief   The tabs of the Output window, one per analysis, and the spectrum of the transient.
 */
enum class PlotTab {
    OPERATING_POINT,
    TRANSIENT,
    SPECTRUM,
    AC_SWEEP,
    DC_SWEEP,
    NOISE,
    TRANSFER_FUNCTION,
    SENSITIVITY,
    POLE_ZERO,
    S_PARAMETERS,
    TRUTH_TABLE,
    MONTE_CARLO
};

/// Every tab, in the order of the tab bar.
inline constexpr std::array<PlotTab, 12> PLOT_TABS = {
    PlotTab::OPERATING_POINT, PlotTab::TRANSIENT,    PlotTab::SPECTRUM,          PlotTab::AC_SWEEP,
    PlotTab::DC_SWEEP,        PlotTab::NOISE,        PlotTab::TRANSFER_FUNCTION, PlotTab::SENSITIVITY,
    PlotTab::POLE_ZERO,       PlotTab::S_PARAMETERS, PlotTab::TRUTH_TABLE,       PlotTab::MONTE_CARLO};

/// Whether each tab has a result to show, indexed by the tab.
using TabResults = std::array<bool, PLOT_TABS.size()>;

/**
 * @struct  TabListing
 * @brief   The tabs the bar shows, and the others, which a menu beside them offers.
 */
struct TabListing {
    std::vector<PlotTab> Shown;
    std::vector<PlotTab> Others;
};

// Tabs
std::size_t ToIndex(PlotTab tab);
const char *GetTabLabel(PlotTab tab);
TabListing ListTabs(const TabResults &has_result, std::optional<PlotTab> kept);

} // namespace GUI

#endif // IMCSIM_PLOT_TAB_H
