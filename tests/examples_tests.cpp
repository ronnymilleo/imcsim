/**
 * @file    examples_tests.cpp
 * @brief   Tests that every example circuit opens cleanly and simulates with its own settings.
 */

#include "examples.h"
#include "schematic.h"
#include "simulator.h"
#include "test_printers.h"
#include "trace_statistics.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <numbers>
#include <string>
#include <string_view>

namespace {

// Loads an example into a schematic, as the editor does
void OpenOrFail(const GUI::Example &example, GUI::Schematic &schematic) {
    auto loaded = GUI::LoadExample(example);
    if (!loaded) {
        FAIL(loaded.error());
    }
    CHECK(loaded->Warnings.empty());
    schematic.Replace(std::move(loaded->Elements), std::move(loaded->Wires), std::move(loaded->Settings));
    schematic.RestoreMeasurements(loaded->Measurements);
    // Every saved voltage lands on a node, so none is dropped on the way in
    CHECK(schematic.SaveMeasurements() == loaded->Measurements);
}

} // namespace

TEST_CASE("Every example opens without warnings, measures existing traces and runs", "[examples]") {
    REQUIRE_FALSE(GUI::GetExamples().empty());
    for (const GUI::Example &example : GUI::GetExamples()) {
        INFO(example.Title);
        GUI::Schematic schematic;
        OpenOrFail(example, schematic);
        CHECK_FALSE(schematic.GetElements().empty());

        const Core::OperatingPointRun operating_point = Core::RunOperatingPoint(schematic.BuildCircuit());
        REQUIRE(operating_point.Result.has_value());
        const GUI::SavedMeasurements measurements = schematic.SaveMeasurements();
        CHECK_FALSE((measurements.Voltages.empty() && measurements.Currents.empty()));
        for (const std::string &name : measurements.Currents) {
            INFO(name);
            CHECK(std::ranges::contains(operating_point.Result->Currents, name, &Core::ComponentCurrent::Name));
        }
        const Core::TransientRun transient =
            Core::RunTransient(schematic.BuildCircuit(), schematic.GetSimulationSettings().Transient);
        CHECK(transient.Result.has_value());
    }
}

TEST_CASE("The BJT example draws one collector curve per base current", "[examples]") {
    const auto example = std::ranges::find(GUI::GetExamples(), std::string_view("bjt_output_characteristics.imcsim"),
                                           &GUI::Example::FileName);
    REQUIRE(example != GUI::GetExamples().end());
    GUI::Schematic schematic;
    OpenOrFail(*example, schematic);

    const GUI::SimulationSettings &settings = schematic.GetSimulationSettings();
    REQUIRE(settings.StepSource);
    const Core::DCSweepRun run =
        Core::RunDCSweep(schematic.BuildCircuit(), {.Swept = settings.SweptRange, .Stepped = settings.SteppedRange});
    REQUIRE(run.Result.has_value());
    CHECK(run.Result->Curves.size() == 5);
}

TEST_CASE("The RC example measures its corner frequency over the whole sweep", "[examples]") {
    const auto example =
        std::ranges::find(GUI::GetExamples(), std::string_view("rc_low_pass_filter.imcsim"), &GUI::Example::FileName);
    REQUIRE(example != GUI::GetExamples().end());
    GUI::Schematic schematic;
    OpenOrFail(*example, schematic);

    const Core::ACSweepRun run = Core::RunACSweep(schematic.BuildCircuit(), schematic.GetSimulationSettings().ACSweep);
    REQUIRE(run.Result.has_value());
    const Core::ACSweep &sweep = *run.Result;
    // Node 2 is the capacitor, the output of the filter
    REQUIRE(sweep.NodeMagnitudesDecibels.size() > 2);
    const auto statistics =
        GUI::ComputeBodeStatistics(sweep.Frequencies, sweep.NodeMagnitudesDecibels[2], sweep.NodePhasesDegrees[2],
                                   sweep.Frequencies.front(), sweep.Frequencies.back());
    REQUIRE(statistics);
    REQUIRE(statistics->UpperCutoff);
    const double corner = 1.0 / (2.0 * std::numbers::pi * 1e3 * 100e-9);
    CHECK_THAT(*statistics->UpperCutoff, Catch::Matchers::WithinRel(corner, 2e-2));
    CHECK_FALSE(statistics->LowerCutoff);
}
