/**
 * @file    stability_view_tests.cpp
 * @brief   Tests of the curves and figures of the Stability view and of the Smith chart with the stability circles,
 *          including their SVG and CSV.
 */

#include "plots/stability_view.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <string>
#include <vector>

namespace {

using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Complex = std::complex<double>;

Complex Polar(const double magnitude, const double degrees) {
    return std::polar(magnitude, degrees * std::numbers::pi / 180.0);
}

/**
 * @brief   Three frequencies of a transistor: unstable at the first two (K below 1) and stable at the last.
 */
Core::SParameters MakeResult() {
    Core::SParameters result;
    result.Frequencies = {1e8, 2e8, 4e8};
    result.ReferenceOhms = {50.0, 50.0};
    const GUI::TwoPortMatrix unstable{.S11 = Polar(0.75, -115.0),
                                      .S12 = Polar(0.06, 60.0),
                                      .S21 = Polar(3.2, 70.0),
                                      .S22 = Polar(0.45, -40.0)};
    const GUI::TwoPortMatrix stable{.S11 = Polar(0.3, -70.0),
                                    .S12 = Polar(0.08, 60.0),
                                    .S21 = Polar(2.1, 80.0),
                                    .S22 = Polar(0.4, -40.0)};
    for (const GUI::TwoPortMatrix &matrix : {unstable, unstable, stable}) {
        result.Values.insert(result.Values.end(), {matrix.S11, matrix.S12, matrix.S21, matrix.S22});
    }
    return result;
}

} // namespace

TEST_CASE("The curves hold K and mu, and the gain as MAG where stable and MSG where not", "[stability_view]") {
    const GUI::TwoPortSweep sweep = GUI::ComputeTwoPortSweep(MakeResult());
    const GUI::StabilityCurves curves = GUI::BuildStabilityCurves(sweep);
    REQUIRE(curves.K.size() == 3);
    CHECK_THAT(curves.K[0], WithinAbs(0.91726, 1e-5));
    CHECK_THAT(curves.K[2], WithinAbs(2.40004, 1e-4));
    CHECK_THAT(curves.Mu[1], WithinAbs(0.94219, 1e-5));
    // MSG at the unstable points, and at the stable neighbor so that the two kinds join
    CHECK_THAT(curves.MaximumStableGainDecibels[0], WithinAbs(17.2700, 1e-3));
    CHECK_THAT(curves.MaximumStableGainDecibels[1], WithinAbs(17.2700, 1e-3));
    CHECK_THAT(curves.MaximumStableGainDecibels[2], WithinAbs(14.1913, 1e-3));
    CHECK(std::isnan(curves.MaximumAvailableGainDecibels[0]));
    CHECK_THAT(curves.MaximumAvailableGainDecibels[2], WithinAbs(7.5809, 1e-3));
    CHECK(curves.MaximumAvailableGainLabel == "MAG (unconditionally stable)");
    CHECK(curves.MaximumStableGainLabel == "MSG (potentially unstable)");
}

TEST_CASE("The axis of the factors stops at 3 when a value is far larger, and shows room above 1 otherwise",
          "[stability_view]") {
    const GUI::TwoPortSweep sweep = GUI::ComputeTwoPortSweep(MakeResult());
    const GUI::StabilityCurves curves = GUI::BuildStabilityCurves(sweep);
    CHECK_THAT(curves.FactorRange.To, WithinAbs(2.40004 * 1.1, 1e-3));
    CHECK_THAT(curves.FactorRange.From, WithinAbs(0.0, 1e-12));
    GUI::TwoPortSweep large = sweep;
    large.Metrics[0].K = 40.0;
    CHECK_THAT(GUI::BuildStabilityCurves(large).FactorRange.To, WithinAbs(GUI::FACTOR_AXIS_CEILING, 1e-12));
    GUI::TwoPortSweep small = sweep;
    for (GUI::TwoPortMetrics &metrics : small.Metrics) {
        metrics.K = 0.2;
        metrics.Mu = 0.3;
    }
    CHECK_THAT(GUI::BuildStabilityCurves(small).FactorRange.To, WithinAbs(GUI::FACTOR_AXIS_MINIMUM_CEILING, 1e-12));
}

TEST_CASE("An infinite K and an invalid point are gaps in the curves, never a number", "[stability_view]") {
    GUI::TwoPortSweep sweep = GUI::ComputeTwoPortSweep(MakeResult());
    sweep.Metrics[0].K = std::numeric_limits<double>::infinity();
    sweep.Metrics[1].IsValid = false;
    const GUI::StabilityCurves curves = GUI::BuildStabilityCurves(sweep);
    CHECK(std::isnan(curves.K[0]));
    CHECK_FALSE(std::isnan(curves.Mu[0]));
    CHECK(std::isnan(curves.K[1]));
    CHECK(std::isnan(curves.Mu[1]));
}

TEST_CASE("The unilateral gain has its own legend", "[stability_view]") {
    Core::SParameters result;
    result.Frequencies = {1e8, 2e8};
    result.ReferenceOhms = {50.0, 50.0};
    for (int index = 0; index < 2; ++index) {
        result.Values.insert(result.Values.end(),
                             {Complex(0.5, 0.0), Complex(0.0, 0.0), Complex(3.0, 0.0), Complex(0.4, 0.0)});
    }
    const GUI::StabilityCurves curves = GUI::BuildStabilityCurves(GUI::ComputeTwoPortSweep(result));
    CHECK(curves.MaximumAvailableGainLabel == "Unilateral gain (S12 = 0)");
    CHECK_THAT(curves.MaximumAvailableGainDecibels[0], WithinAbs(11.549, 1e-3));
    // K is infinite: only mu is drawn
    CHECK(std::isnan(curves.K[0]));
    CHECK_FALSE(std::isnan(curves.Mu[0]));
}

TEST_CASE("The stability figure has the factors with a line at 1 and the two kinds of gain", "[stability_view]") {
    const GUI::TwoPortSweep sweep = GUI::ComputeTwoPortSweep(MakeResult());
    const GUI::ExportFigure figure = GUI::BuildStabilityFigure(sweep, GUI::ExportRange{.From = 1e8, .To = 4e8});
    REQUIRE(figure.Panels.size() == 2);
    CHECK(figure.XLogarithmic);
    REQUIRE(figure.Panels[0].Series.size() == 3);
    CHECK(figure.Panels[0].Series[2].Dashed);
    CHECK(figure.Panels[0].Series[2].Values == std::vector<double>(3, 1.0));
    CHECK_FALSE(figure.Panels[0].Series[2].InTable);
    REQUIRE(figure.Panels[1].Series.size() == 2);
    CHECK_FALSE(figure.Panels[1].Series[0].Dashed);
    CHECK(figure.Panels[1].Series[1].Dashed);
    CHECK(figure.Panels[1].Series[1].LegendLabel == "MSG (potentially unstable)");
    const auto svg = GUI::RenderSvg(figure, {});
    REQUIRE(svg.has_value());
    CHECK_THAT(*svg, ContainsSubstring("MAG (unconditionally stable)"));
    CHECK_THAT(*svg, ContainsSubstring("stable above"));
}

TEST_CASE("The stability table lists the factors and the gain of each kind without the joining samples",
          "[stability_view]") {
    const GUI::TwoPortSweep sweep = GUI::ComputeTwoPortSweep(MakeResult());
    const std::string csv = GUI::RenderCsv(GUI::BuildStabilityTableFigure(sweep));
    CHECK_THAT(csv, ContainsSubstring("Frequency (Hz),K,mu,mu',|Delta|,MAG (dB),MSG (dB)\n"));
    // The first row is unstable: no MAG, and the MSG of 17.27 dB; the last is stable: MAG alone
    CHECK_THAT(csv, ContainsSubstring("1e+08,0.9172"));
    CHECK_THAT(csv, ContainsSubstring(",,17.26"));
    CHECK_THAT(csv, ContainsSubstring("4e+08,2.4000"));
    CHECK_THAT(csv, ContainsSubstring(",7.58"));
}

TEST_CASE("The Smith figure with circles holds a source and a load circle at the chosen frequency",
          "[stability_view]") {
    const Core::SParameters result = MakeResult();
    const GUI::ExportFigure figure = GUI::BuildSmithFigureWithCircles(result, {{1, 1}, {2, 2}}, 0);
    REQUIRE(figure.Smith.has_value());
    REQUIRE(figure.Smith->Circles.size() == 2);
    const GUI::ExportSmithCircle &source = figure.Smith->Circles[0];
    CHECK(source.Name == "Source stability circle");
    CHECK(source.Shape == "circle");
    CHECK_THAT(source.Radius, WithinAbs(0.43120, 1e-4));
    CHECK(source.FrequencyHertz == 1e8);
    CHECK(source.StableRegion == "outside the circle");
    CHECK_FALSE(source.Boundary.empty());
    CHECK_FALSE(source.Hatch.empty());
    CHECK_THAT(figure.Smith->Title, ContainsSubstring("stability circles at 100MHz"));
    CHECK(figure.Smith->Traces.size() == 2);
}

TEST_CASE("A circle that does not cross the chart is only in the table", "[stability_view]") {
    Core::SParameters result;
    result.Frequencies = {1e8};
    result.ReferenceOhms = {50.0, 50.0};
    // A pad: both circles are of radius 4 around the origin, so the whole chart is stable
    result.Values = {Complex(0.0, 0.0), Complex(0.5, 0.0), Complex(0.5, 0.0), Complex(0.0, 0.0)};
    const GUI::ExportFigure figure = GUI::BuildSmithFigureWithCircles(result, {{1, 1}}, 0);
    REQUIRE(figure.Smith->Circles.size() == 2);
    CHECK(figure.Smith->Circles[0].Boundary.empty());
    CHECK(figure.Smith->Circles[0].Hatch.empty());
    CHECK(figure.Smith->Circles[0].StableRegion == "the whole chart");
    CHECK_THAT(figure.Smith->Title, ContainsSubstring("none crosses the chart"));
    const std::string csv = GUI::RenderCsv(figure);
    CHECK_THAT(csv,
               ContainsSubstring(
                   "Stability circle,Frequency (Hz),Shape,Center real,Center imaginary,Radius,Stable terminations"));
    CHECK_THAT(csv, ContainsSubstring("Source stability circle,1e+08,circle,0,0,4,the whole chart"));
    const auto svg = GUI::RenderSvg(figure, {});
    REQUIRE(svg.has_value());
    CHECK_THAT(*svg, !ContainsSubstring("Source stability circle"));
}

TEST_CASE("The SVG of the Smith chart draws the circles, their hatching and their legend", "[stability_view]") {
    const GUI::ExportFigure figure = GUI::BuildSmithFigureWithCircles(MakeResult(), {{1, 1}}, 0);
    const auto svg = GUI::RenderSvg(figure, {});
    REQUIRE(svg.has_value());
    CHECK_THAT(*svg, ContainsSubstring("Source stability circle"));
    CHECK_THAT(*svg, ContainsSubstring("Load stability circle"));
    CHECK_THAT(*svg, ContainsSubstring("stroke-opacity"));
    CHECK_THAT(*svg, ContainsSubstring("stroke-dasharray"));
}

TEST_CASE("The CSV of the Smith chart keeps its table and adds the circles below it", "[stability_view]") {
    const GUI::ExportFigure plain = GUI::BuildSmithFigure(MakeResult(), {{1, 1}});
    const GUI::ExportFigure with_circles = GUI::BuildSmithFigureWithCircles(MakeResult(), {{1, 1}}, 0);
    const std::string table = GUI::RenderCsv(plain);
    const std::string full = GUI::RenderCsv(with_circles);
    CHECK(full.starts_with(table));
    CHECK_THAT(full, ContainsSubstring("\nStability circle,"));
    CHECK_THAT(full, ContainsSubstring("Load stability circle,1e+08,circle,1.05"));
}

TEST_CASE("A result of another number of ports or a frequency outside the sweep adds no circles", "[stability_view]") {
    Core::SParameters one_port;
    one_port.Frequencies = {1e8};
    one_port.ReferenceOhms = {50.0};
    one_port.Values = {Complex(0.1, 0.0)};
    CHECK(GUI::BuildSmithFigureWithCircles(one_port, {{1, 1}}, 0).Smith->Circles.empty());
    CHECK(GUI::BuildSmithFigureWithCircles(MakeResult(), {{1, 1}}, 9).Smith->Circles.empty());
}
