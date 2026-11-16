/**
 * @file    plot_figures_tests.cpp
 * @brief   Tests for the export figures of the Output window plots.
 */

#include "plots/math_channels.h"
#include "plots/plot_export.h"
#include "plots/plot_figures.h"
#include "schematic/schematic.h"
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <optional>
#include <set>
#include <string>
#include <vector>

TEST_CASE("The Bode figure exports math channels on both panels, an impedance on an axis of its own",
          "[plot_figures]") {
    Core::ACSweep sweep;
    sweep.Frequencies = {10.0, 100.0, 1000.0};
    sweep.NodeMagnitudesDecibels = {{}, {0.0, 0.0, 0.0}};
    sweep.NodePhasesDegrees = {{}, {0.0, 0.0, 0.0}};
    const GUI::MathTrace magnitude{
        .Label = "M1: V(1)/I(R1)", .Values = {60.0, 60.0, 60.0}, .Unit = "Ohm", .Axis = ImAxis_Y3, .Color = 0};
    GUI::MathTrace phase = magnitude;
    phase.Values = {-45.0, -45.0, -45.0};
    const GUI::ACMathTraces math{.Magnitudes = {magnitude}, .Phases = {phase}};
    // Nothing is measured, so the math channel is the only trace of each panel
    GUI::Schematic schematic;
    const GUI::ResultStore results;
    const GUI::ExportFigure figure =
        GUI::BuildACSweepFigure(sweep, math, results, schematic, {.From = 10.0, .To = 1000.0});
    REQUIRE(figure.Panels.size() == 2);
    const GUI::ExportPanel &magnitudes = figure.Panels[0];
    REQUIRE(magnitudes.Series.size() == 1);
    CHECK(magnitudes.Series[0].Values == magnitude.Values);
    CHECK(magnitudes.Series[0].Heavy);
    CHECK(magnitudes.YAxes.at(magnitudes.Series[0].Axis).Name == "Ohm (dB)");
    const GUI::ExportPanel &phases = figure.Panels[1];
    REQUIRE(phases.Series.size() == 1);
    CHECK(phases.Series[0].Values == phase.Values);
    CHECK(phases.YAxes.at(phases.Series[0].Axis).Name == "Ohm phase (deg)");
}

TEST_CASE("The AC sensitivity figure holds a gain panel and a phase panel with the checked parts",
          "[plot_figures][ac_sensitivity_view]") {
    // Three parts at two frequencies: the first moves nothing, the second only the phase and the third both, with no
    // figure at the second frequency
    Core::ACSensitivity sensitivity;
    sensitivity.Frequencies = {10.0, 100.0};
    sensitivity.Output = {1.0, 0.5};
    sensitivity.Parts = {
        {.Name = "V1", .MagnitudeDecibelsPerPercent = {0.0, 0.0}, .PhaseDegreesPerPercent = {0.0, 0.0}},
        {.Name = "C1", .MagnitudeDecibelsPerPercent = {0.0, 0.0}, .PhaseDegreesPerPercent = {-2.0, -1.0}},
        {.Name = "R1",
         .MagnitudeDecibelsPerPercent = {-0.5, std::nullopt},
         .PhaseDegreesPerPercent = {-0.1, std::nullopt}}};
    const std::vector<GUI::ACSensitivityCurve> curves = GUI::MakeACSensitivityCurves(sensitivity);
    const std::vector<ImU32> colors = {IM_COL32(255, 0, 0, 255), IM_COL32(0, 255, 0, 255), IM_COL32(0, 0, 255, 255)};

    const GUI::ExportFigure figure =
        GUI::BuildACSensitivityFigure(sensitivity, curves, colors, {"R1", "C1"}, "V(OUT)", {.From = 20.0, .To = 50.0});

    CHECK(figure.XLogarithmic);
    CHECK(figure.Xs == sensitivity.Frequencies);
    REQUIRE(figure.XRange.has_value());
    CHECK(figure.XRange->From == 20.0);
    REQUIRE(figure.Panels.size() == 2);
    CHECK(figure.Panels[0].Title.contains("V(OUT)"));
    CHECK(figure.Panels[0].YAxes.front().Unit == "dB");
    CHECK(figure.Panels[1].YAxes.front().Unit == "deg");
    // The parts keep the order of the result, in their own colors, and the unchecked source is not drawn
    REQUIRE(figure.Panels[0].Series.size() == 2);
    CHECK(figure.Panels[0].Series[0].Name == "C1");
    CHECK(figure.Panels[0].Series[0].Color.Green == 255);
    CHECK(figure.Panels[0].Series[1].Name == "R1");
    CHECK(figure.Panels[0].Series[1].Values[0] == -0.5);
    CHECK(std::isnan(figure.Panels[0].Series[1].Values[1]));
    CHECK(figure.Panels[1].Series[0].Values[1] == -1.0);
}
