/**
 * @file    analysis_controls_tests.cpp
 * @brief   Tests for the runs that AnalysisControls starts on the worker thread and takes in when they end.
 */

#include "analysis_controls.h"

#include "examples.h"
#include "schematic.h"
#include "test_printers.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <thread>

namespace {

using namespace std::chrono_literals;

// The longest a test waits for a run before it calls it lost
constexpr auto WAIT_LIMIT = 20s;

// Opens the RC low-pass example, as the editor does, with a transient heavy enough to take a while when stepped
void OpenRCFilter(GUI::Schematic &schematic) {
    const auto example = std::ranges::find(GUI::GetExamples(), "rc_low_pass_filter.imcsim", &GUI::Example::FileName);
    REQUIRE(example != GUI::GetExamples().end());
    auto loaded = GUI::LoadExample(*example);
    REQUIRE(loaded.has_value());
    schematic.Replace(std::move(loaded->Elements), std::move(loaded->Wires), std::move(loaded->Settings));
}

void Select(GUI::Schematic &schematic, const GUI::Analysis analysis) {
    GUI::SimulationSettings settings = schematic.GetSimulationSettings();
    settings.Selected = analysis;
    schematic.SetSimulationSettings(settings);
}

// A transient stepped through 50 resistances, each long enough that the run lasts far beyond a few frames
void SelectLongSteppedTransient(GUI::Schematic &schematic) {
    GUI::SimulationSettings settings = schematic.GetSimulationSettings();
    settings.Selected = GUI::Analysis::TRANSIENT;
    settings.Transient = {.StopTime = 0.05, .TimeStep = 1e-6};
    settings.ParameterStep = {
        .Enabled = true, .Part = "R1", .UsesList = false, .Start = 100.0, .Stop = 5000.0, .Step = 100.0, .List = ""};
    schematic.SetSimulationSettings(settings);
}

// Calls Update() as the frame loop does until the run ends
bool UpdateUntilIdle(GUI::AnalysisControls &controls) {
    const auto deadline = std::chrono::steady_clock::now() + WAIT_LIMIT;
    while (std::chrono::steady_clock::now() < deadline) {
        controls.Update();
        if (!controls.IsRunning()) {
            return true;
        }
        std::this_thread::sleep_for(1ms);
    }
    return false;
}

} // namespace

TEST_CASE("A run goes on in the background and its result reaches the schematic when Update takes it in",
          "[analysis_controls]") {
    GUI::Schematic schematic;
    OpenRCFilter(schematic);
    Select(schematic, GUI::Analysis::TRANSIENT);
    GUI::AnalysisControls controls(schematic);

    controls.RunSelected();
    CHECK(controls.IsRunning());
    CHECK(controls.GetRunningAnalysis() == GUI::Analysis::TRANSIENT);
    // Nothing is stored before Update runs on this thread
    CHECK_FALSE(schematic.GetTransient().has_value());

    REQUIRE(UpdateUntilIdle(controls));
    REQUIRE(schematic.GetTransient().has_value());
    CHECK(controls.GetOutcome(GUI::Analysis::TRANSIENT) != GUI::RunOutcome::FAILED);
    CHECK(controls.GetLastRun() == GUI::Analysis::TRANSIENT);
}

TEST_CASE("Run does nothing while another run is in progress", "[analysis_controls]") {
    GUI::Schematic schematic;
    OpenRCFilter(schematic);
    SelectLongSteppedTransient(schematic);
    GUI::AnalysisControls controls(schematic);

    controls.RunSelected();
    Select(schematic, GUI::Analysis::OPERATING_POINT);
    controls.RunSelected();
    CHECK(controls.GetRunningAnalysis() == GUI::Analysis::TRANSIENT);

    controls.StopRun();
    REQUIRE(UpdateUntilIdle(controls));
}

TEST_CASE("Stopping a stepped run ends it between steps and keeps no result", "[analysis_controls]") {
    GUI::Schematic schematic;
    OpenRCFilter(schematic);
    SelectLongSteppedTransient(schematic);
    GUI::AnalysisControls controls(schematic);

    controls.RunSelected();
    controls.StopRun();
    CHECK(controls.IsStopping());
    REQUIRE(UpdateUntilIdle(controls));

    CHECK_FALSE(controls.IsStopping());
    CHECK_FALSE(schematic.GetTransientSteps().has_value());
    CHECK(controls.GetOutcome(GUI::Analysis::TRANSIENT) == GUI::RunOutcome::NONE);
}

TEST_CASE("Editing the circuit stops the run, whose answer would no longer fit", "[analysis_controls]") {
    GUI::Schematic schematic;
    OpenRCFilter(schematic);
    SelectLongSteppedTransient(schematic);
    GUI::AnalysisControls controls(schematic);

    controls.RunSelected();
    schematic.MarkModified();
    REQUIRE(UpdateUntilIdle(controls));

    CHECK_FALSE(schematic.GetTransientSteps().has_value());
    CHECK(controls.GetOutcome(GUI::Analysis::TRANSIENT) == GUI::RunOutcome::NONE);
}

TEST_CASE("A stepped run reports its progress and delivers one transient per value", "[analysis_controls]") {
    GUI::Schematic schematic;
    OpenRCFilter(schematic);
    GUI::SimulationSettings settings = schematic.GetSimulationSettings();
    settings.Selected = GUI::Analysis::TRANSIENT;
    settings.ParameterStep = {
        .Enabled = true, .Part = "R1", .UsesList = true, .Start = 0.0, .Stop = 0.0, .Step = 0.0, .List = "1k, 2k, 4k"};
    schematic.SetSimulationSettings(settings);
    GUI::AnalysisControls controls(schematic);

    controls.RunSelected();
    REQUIRE(UpdateUntilIdle(controls));
    REQUIRE(schematic.GetTransientSteps().has_value());
    CHECK(schematic.GetTransientSteps()->Results.size() == 3);
}
