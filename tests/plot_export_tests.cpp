/**
 * @file    plot_export_tests.cpp
 * @brief   Tests for the SVG and CSV export of plots.
 */

#include "plot_export.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <vector>

namespace {

using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;

// A transient-like figure: one voltage and one dashed current over 0 to 1 ms
GUI::ExportFigure MakeFigure(const std::size_t sample_count = 101) {
    GUI::ExportFigure figure;
    figure.XAxis = {.Name = "Time", .Unit = "s"};
    GUI::ExportPanel panel;
    panel.YAxes = {{.Name = "Voltage", .Unit = "V"}, {.Name = "Current", .Unit = "A"}};
    GUI::ExportSeries voltage{.Name = "V(1)", .LegendLabel = "V(1)", .Color = {230, 120, 100}};
    GUI::ExportSeries current{.Name = "I(R1)", .LegendLabel = "I(R1)", .Axis = 1, .Color = {90, 140, 230}};
    current.Dashed = true;
    for (std::size_t index = 0; index < sample_count; ++index) {
        const double time = 1e-3 * static_cast<double>(index) / static_cast<double>(sample_count - 1);
        figure.Xs.push_back(time);
        voltage.Values.push_back(5.0 * time / 1e-3);
        current.Values.push_back(2e-3 * time / 1e-3);
    }
    panel.Series = {voltage, current};
    figure.Panels.push_back(panel);
    return figure;
}

std::size_t CountOccurrences(const std::string &text, const std::string &pattern) {
    std::size_t count = 0;
    for (std::size_t position = text.find(pattern); position != std::string::npos;
         position = text.find(pattern, position + pattern.size())) {
        ++count;
    }
    return count;
}

// Points of the first polyline drawn in the given color
std::size_t CountPolylinePoints(const std::string &svg, const std::string &color) {
    const std::size_t line = svg.find(std::string("<polyline fill=\"none\" stroke=\"") + color);
    if (line == std::string::npos) {
        return 0;
    }
    const std::size_t start = svg.find("points=\"", line) + 8;
    const std::size_t end = svg.find('"', start);
    return CountOccurrences(svg.substr(start, end - start), ",");
}

} // namespace

TEST_CASE("Nice ticks are round numbers across the range", "[plot_export]") {
    const std::vector<double> ticks = GUI::FindNiceTicks(0.0, 1.0, 5);
    REQUIRE(ticks.size() == 6);
    CHECK_THAT(ticks[1], WithinAbs(0.2, 1e-12));
    CHECK_THAT(ticks.back(), WithinAbs(1.0, 1e-12));

    const std::vector<double> around_zero = GUI::FindNiceTicks(-0.3, 0.3, 6);
    CHECK(std::ranges::find(around_zero, 0.0) != around_zero.end());
    CHECK(GUI::FindNiceTicks(1.0, 1.0, 5).empty());
}

TEST_CASE("Decade ticks skip decades when there are too many", "[plot_export]") {
    CHECK(GUI::FindDecadeTicks(1.0, 1e6, 10).size() == 7);
    const std::vector<double> thinned = GUI::FindDecadeTicks(1.0, 1e6, 3);
    CHECK(thinned.size() <= 3);
    CHECK_THAT(thinned.front(), WithinAbs(1.0, 1e-12));
    CHECK(GUI::FindDecadeTicks(-1.0, 10.0, 5).empty());
}

TEST_CASE("Tick labels show the step with the fewest decimals", "[plot_export]") {
    CHECK(GUI::FormatTick(0.5, 0.1) == "0.5");
    CHECK(GUI::FormatTick(20.0, 10.0) == "20");
    CHECK(GUI::FormatTick(-1e-17, 0.2) == "0");
    CHECK(GUI::FormatTick(-0.0, 1.0) == "0");
    CHECK(GUI::FormatTick(2e-5, 1e-5) == "2e-5");
}

TEST_CASE("SVG export draws the traces, the legend and prefixed ticks", "[plot_export]") {
    const auto svg = GUI::RenderSvg(MakeFigure(), {});
    REQUIRE(svg);
    CHECK_THAT(*svg, ContainsSubstring("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"1600\" height=\"900\""));
    CHECK_THAT(*svg, ContainsSubstring(">V(1)</text>"));
    CHECK_THAT(*svg, ContainsSubstring(">I(R1)</text>"));
    // Currents are dashed, voltages are not
    CHECK(CountOccurrences(*svg, "stroke-dasharray") == 2);
    CHECK_THAT(*svg, ContainsSubstring("ms</text>"));
    CHECK_THAT(*svg, ContainsSubstring("mA</text>"));
    CHECK_THAT(*svg, ContainsSubstring(">Time</text>"));
}

TEST_CASE("SVG export rejects bad sizes and empty figures", "[plot_export]") {
    CHECK_FALSE(GUI::RenderSvg(MakeFigure(), {.Width = 100, .Height = 900}));
    CHECK_FALSE(GUI::RenderSvg(MakeFigure(), {.Width = 1600, .Height = GUI::MaxExportSize + 1}));
    CHECK_FALSE(GUI::RenderSvg(GUI::ExportFigure{}, {}));
}

TEST_CASE("SVG export escapes text", "[plot_export]") {
    GUI::ExportFigure figure = MakeFigure();
    figure.Panels.front().Series.front().LegendLabel = "a<b & \"c\"";
    const auto svg = GUI::RenderSvg(figure, {});
    REQUIRE(svg);
    CHECK_THAT(*svg, ContainsSubstring(">a&lt;b &amp; &quot;c&quot;</text>"));
}

TEST_CASE("Light trace colors are darkened for print only", "[plot_export]") {
    GUI::ExportFigure figure = MakeFigure();
    figure.Panels.front().Series.front().Color = {240, 200, 90};
    const auto light = GUI::RenderSvg(figure, {.Dark = false});
    const auto dark = GUI::RenderSvg(figure, {.Dark = true});
    REQUIRE(light);
    REQUIRE(dark);
    CHECK_THAT(*dark, ContainsSubstring("stroke=\"#f0c85a\""));
    CHECK_THAT(*light, !ContainsSubstring("#f0c85a"));
}

TEST_CASE("SVG export draws only the samples in the X range", "[plot_export]") {
    // Dark keeps the trace colors as given, so the line is found by its color
    GUI::ExportFigure figure = MakeFigure(1001);
    const auto whole = GUI::RenderSvg(figure, {.Dark = true});
    figure.XRange = GUI::ExportRange{.From = 0.0, .To = 1e-4};
    const auto cropped = GUI::RenderSvg(figure, {.Dark = true});
    REQUIRE(whole);
    REQUIRE(cropped);
    const std::size_t whole_points = CountPolylinePoints(*whole, "#e67864");
    const std::size_t cropped_points = CountPolylinePoints(*cropped, "#e67864");
    CHECK(whole_points > 500);
    // The samples from 0 to 0.1 ms, plus the one just past the range
    CHECK(cropped_points == 102);
}

TEST_CASE("Missing samples break the line", "[plot_export]") {
    GUI::ExportFigure figure = MakeFigure();
    figure.Panels.front().Series.resize(1);
    figure.Panels.front().Series.front().Values[50] = std::numeric_limits<double>::quiet_NaN();
    const auto svg = GUI::RenderSvg(figure, {.Dark = true});
    REQUIRE(svg);
    CHECK(CountOccurrences(*svg, "<polyline fill=\"none\" stroke=\"#e67864\"") == 3);
}

TEST_CASE("Logarithmic X axes label each decade with its unit", "[plot_export]") {
    GUI::ExportFigure figure;
    figure.XAxis = {.Name = "Frequency", .Unit = "Hz"};
    figure.XLogarithmic = true;
    GUI::ExportPanel panel;
    panel.YAxes = {{.Name = "Voltage (dB)", .Unit = "dB", .UsesPrefixes = false}};
    GUI::ExportSeries gain{.Name = "V(2)", .LegendLabel = "V(2)"};
    for (int exponent = 0; exponent <= 5; ++exponent) {
        figure.Xs.push_back(std::pow(10.0, exponent));
        gain.Values.push_back(-20.0 * exponent);
    }
    panel.Series = {gain};
    figure.Panels = {panel, panel};
    const auto svg = GUI::RenderSvg(figure, {});
    REQUIRE(svg);
    CHECK_THAT(*svg, ContainsSubstring(">1kHz</text>"));
    CHECK_THAT(*svg, ContainsSubstring(">100kHz</text>"));
    // Decibels are plain numbers, without prefixes
    CHECK_THAT(*svg, ContainsSubstring(">-40</text>"));
    CHECK_THAT(*svg, !ContainsSubstring("mdB"));
}

TEST_CASE("Notes on a panel are drawn in the X range", "[plot_export]") {
    GUI::ExportFigure figure = MakeFigure();
    figure.Panels.front().Notes = {{.X = 5e-4, .Y = 2.5, .Axis = 0, .Text = "VB=1V"},
                                   {.X = 2e-3, .Y = 2.5, .Axis = 0, .Text = "outside"}};
    const auto svg = GUI::RenderSvg(figure, {});
    REQUIRE(svg);
    CHECK_THAT(*svg, ContainsSubstring(">VB=1V</text>"));
    CHECK_THAT(*svg, !ContainsSubstring(">outside</text>"));
}

TEST_CASE("CSV export lists every sample of every series", "[plot_export]") {
    GUI::ExportFigure figure = MakeFigure(3);
    figure.XRange = GUI::ExportRange{.From = 0.0, .To = 1e-4};
    figure.Panels.front().Series.front().Values[1] = std::numeric_limits<double>::quiet_NaN();
    const std::string csv = GUI::RenderCsv(figure);
    CHECK(csv == "Time (s),V(1) (V),I(R1) (A)\n"
                 "0,0,0\n"
                 "5e-04,,0.001\n"
                 "0.001,5,0.002\n");
}

TEST_CASE("CSV headers name the panel when there are several and quote separators", "[plot_export]") {
    GUI::ExportFigure figure;
    figure.XAxis = {.Name = "Frequency", .Unit = "Hz"};
    figure.Xs = {1.0};
    GUI::ExportPanel magnitude{.Title = "Magnitude", .YAxes = {{.Name = "dB", .Unit = "dB"}}};
    magnitude.Series = {{.Name = "V(2)", .Values = {-3.0}}};
    GUI::ExportPanel phase{.Title = "Phase", .YAxes = {{.Name = "deg", .Unit = "deg"}}};
    phase.Series = {{.Name = "M1: V(1,2)", .Values = {-45.0}}};
    figure.Panels = {magnitude, phase};
    CHECK(GUI::RenderCsv(figure) == "Frequency (Hz),Magnitude V(2) (dB),\"Phase M1: V(1,2) (deg)\"\n1,-3,-45\n");
}
