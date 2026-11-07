/**
 * @file    plot_tab_tests.cpp
 * @brief   Tests for which tabs the Output window shows.
 */

#include "windows/output_tabs/plot_tab.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <string_view>
#include <vector>

TEST_CASE("Only the tabs with a result are shown, the others wait in the menu", "[plot_tab]") {
    GUI::TabResults has_result{};
    has_result[GUI::ToIndex(GUI::PlotTab::TRANSIENT)] = true;
    has_result[GUI::ToIndex(GUI::PlotTab::SPECTRUM)] = true;
    const GUI::TabListing listing = GUI::ListTabs(has_result, std::nullopt);
    CHECK(listing.Shown == std::vector{GUI::PlotTab::TRANSIENT, GUI::PlotTab::SPECTRUM});
    CHECK(listing.Others.size() == GUI::PLOT_TABS.size() - 2);
}

TEST_CASE("The tab the user is on stays when an edit drops its result", "[plot_tab]") {
    GUI::TabResults has_result{};
    has_result[GUI::ToIndex(GUI::PlotTab::OPERATING_POINT)] = true;
    const GUI::TabListing listing = GUI::ListTabs(has_result, GUI::PlotTab::AC_SWEEP);
    // In the order of the bar, not the order they were kept
    CHECK(listing.Shown == std::vector{GUI::PlotTab::OPERATING_POINT, GUI::PlotTab::AC_SWEEP});
}

TEST_CASE("Without results or a kept tab the bar shows nothing", "[plot_tab]") {
    const GUI::TabListing listing = GUI::ListTabs(GUI::TabResults{}, std::nullopt);
    CHECK(listing.Shown.empty());
    CHECK(listing.Others.size() == GUI::PLOT_TABS.size());
}

TEST_CASE("Every tab has a label", "[plot_tab]") {
    for (const GUI::PlotTab tab : GUI::PLOT_TABS) {
        CHECK(std::string_view(GUI::GetTabLabel(tab)).size() > 0);
    }
}

TEST_CASE("Every analysis has a tab that reports it back", "[plot_tab]") {
    for (std::size_t index = 0; index < GUI::ANALYSIS_COUNT; ++index) {
        const auto analysis = static_cast<GUI::Analysis>(index);
        CHECK(GUI::GetTabAnalysis(GUI::GetAnalysisTab(analysis)) == analysis);
    }
}

TEST_CASE("The spectrum and Monte Carlo tabs report no analysis of their own", "[plot_tab]") {
    CHECK_FALSE(GUI::GetTabAnalysis(GUI::PlotTab::SPECTRUM).has_value());
    CHECK_FALSE(GUI::GetTabAnalysis(GUI::PlotTab::MONTE_CARLO).has_value());
}
