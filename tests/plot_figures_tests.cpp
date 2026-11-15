/**
 * @file    plot_figures_tests.cpp
 * @brief   Tests for the export figures of the Output window plots.
 */

#include "plots/math_channels.h"
#include "plots/plot_export.h"
#include "plots/plot_figures.h"
#include "schematic/schematic.h"
#include <catch2/catch_test_macros.hpp>
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
