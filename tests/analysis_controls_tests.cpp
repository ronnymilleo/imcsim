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
#include <string>
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

// A transient of half a second at 1 us, which takes long enough to stop in the middle
void SelectLongTransient(GUI::Schematic &schematic) {
    GUI::SimulationSettings settings = schematic.GetSimulationSettings();
    settings.Selected = GUI::Analysis::TRANSIENT;
    settings.Transient = {.StopTime = 0.5, .TimeStep = 1e-6};
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

// Runs the selected analysis and takes its result in, as the frame loop does
void RunAndWait(GUI::AnalysisControls &controls) {
    controls.RunSelected();
    REQUIRE(UpdateUntilIdle(controls));
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

TEST_CASE("Every analysis runs on the worker and stores its result in the schematic", "[analysis_controls]") {
    GUI::Schematic schematic;
    OpenRCFilter(schematic);
    GUI::AnalysisControls controls(schematic);

    Select(schematic, GUI::Analysis::OPERATING_POINT);
    RunAndWait(controls);
    CHECK(schematic.GetOperatingPoint().has_value());

    Select(schematic, GUI::Analysis::AC_SWEEP);
    RunAndWait(controls);
    CHECK(schematic.GetACSweep().has_value());

    Select(schematic, GUI::Analysis::DC_SWEEP);
    RunAndWait(controls);
    CHECK(schematic.GetDCSweep().has_value());

    Select(schematic, GUI::Analysis::NOISE);
    RunAndWait(controls);
    CHECK(schematic.GetNoise().has_value());

    Select(schematic, GUI::Analysis::TRANSFER_FUNCTION);
    RunAndWait(controls);
    CHECK(schematic.GetTransferFunction().has_value());
    CHECK(controls.GetLastRun() == GUI::Analysis::TRANSFER_FUNCTION);
}

TEST_CASE("A stepped AC sweep and a stepped DC sweep deliver one result per value", "[analysis_controls]") {
    GUI::Schematic schematic;
    OpenRCFilter(schematic);
    GUI::AnalysisControls controls(schematic);
    GUI::SimulationSettings settings = schematic.GetSimulationSettings();
    settings.ParameterStep = {
        .Enabled = true, .Part = "R1", .UsesList = true, .Start = 0.0, .Stop = 0.0, .Step = 0.0, .List = "1k, 2k"};

    settings.Selected = GUI::Analysis::AC_SWEEP;
    schematic.SetSimulationSettings(settings);
    RunAndWait(controls);
    REQUIRE(schematic.GetACSweepSteps().has_value());
    CHECK(schematic.GetACSweepSteps()->Results.size() == 2);

    settings.Selected = GUI::Analysis::DC_SWEEP;
    schematic.SetSimulationSettings(settings);
    RunAndWait(controls);
    CHECK(schematic.GetDCSweep().has_value());
}

TEST_CASE("A run that cannot start says why, and a circuit that cannot run fails with the reason",
          "[analysis_controls]") {
    GUI::Schematic schematic;
    OpenRCFilter(schematic);
    GUI::AnalysisControls controls(schematic);

    // The example has no logic input
    Select(schematic, GUI::Analysis::TRUTH_TABLE);
    controls.RunSelected();
    CHECK_FALSE(controls.IsRunning());
    CHECK(controls.GetOutcome(GUI::Analysis::TRUTH_TABLE) == GUI::RunOutcome::FAILED);
    REQUIRE(controls.GetError(GUI::Analysis::TRUTH_TABLE).has_value());
    CHECK(controls.GetError(GUI::Analysis::TRUTH_TABLE)->contains("Logic input"));

    // A stepped parameter without values
    GUI::SimulationSettings settings = schematic.GetSimulationSettings();
    settings.Selected = GUI::Analysis::TRANSIENT;
    settings.ParameterStep = {
        .Enabled = true, .Part = "R1", .UsesList = true, .Start = 0.0, .Stop = 0.0, .Step = 0.0, .List = "abc"};
    schematic.SetSimulationSettings(settings);
    controls.RunSelected();
    CHECK(controls.GetOutcome(GUI::Analysis::TRANSIENT) == GUI::RunOutcome::FAILED);

    // A circuit without a source, or without even a ground
    GUI::Schematic empty;
    GUI::AnalysisControls empty_controls(empty);
    for (const GUI::Analysis analysis :
         {GUI::Analysis::DC_SWEEP, GUI::Analysis::NOISE, GUI::Analysis::TRANSFER_FUNCTION}) {
        Select(empty, analysis);
        empty_controls.RunSelected();
        CHECK(empty_controls.GetOutcome(analysis) == GUI::RunOutcome::FAILED);
    }
    Select(empty, GUI::Analysis::OPERATING_POINT);
    RunAndWait(empty_controls);
    CHECK(empty_controls.GetOutcome(GUI::Analysis::OPERATING_POINT) == GUI::RunOutcome::FAILED);
    CHECK_FALSE(empty.GetOperatingPoint().has_value());
}

TEST_CASE("Analyses have names, and all of them are counted", "[analysis_controls]") {
    CHECK(std::string(GUI::GetAnalysisName(GUI::Analysis::AC_SWEEP)) == "AC sweep");
    CHECK(std::string(GUI::GetAnalysisName(GUI::Analysis::TRUTH_TABLE)) == "Truth table");
    CHECK(static_cast<std::size_t>(GUI::Analysis::TRUTH_TABLE) + 1 == GUI::ANALYSIS_COUNT);
}

TEST_CASE("A transient grows in the schematic while it runs, and the whole run replaces its chunks",
          "[analysis_controls]") {
    GUI::Schematic schematic;
    OpenRCFilter(schematic);
    SelectLongTransient(schematic);
    GUI::AnalysisControls controls(schematic);

    controls.RunSelected();
    // The first chunk starts the result, which is incomplete until the run ends
    const auto deadline = std::chrono::steady_clock::now() + WAIT_LIMIT;
    while (!schematic.GetTransient() && std::chrono::steady_clock::now() < deadline) {
        controls.Update();
        std::this_thread::sleep_for(1ms);
    }
    REQUIRE(schematic.GetTransient().has_value());
    CHECK_FALSE(schematic.GetTransient()->IsComplete);
    CHECK(controls.IsRunning());
    const std::size_t first_version = schematic.GetTransientVersion();

    std::size_t points = schematic.GetTransient()->Times.size();
    while (controls.IsRunning() && std::chrono::steady_clock::now() < deadline) {
        controls.Update();
        if (!controls.IsRunning()) {
            break;
        }
        // The chunks add points without counting as a new result, so the view is not reset under the user
        CHECK(schematic.GetTransientVersion() == first_version);
        CHECK(schematic.GetTransient()->Times.size() >= points);
        points = schematic.GetTransient()->Times.size();
        std::this_thread::sleep_for(1ms);
    }
    REQUIRE_FALSE(controls.IsRunning());
    REQUIRE(schematic.GetTransient().has_value());
    CHECK(schematic.GetTransient()->IsComplete);
    CHECK(schematic.GetTransientVersion() == first_version + 1);
    CHECK(controls.GetOutcome(GUI::Analysis::TRANSIENT) == GUI::RunOutcome::SUCCEEDED);
}

TEST_CASE("Stopping a transient keeps the points made so far and reports it as stopped", "[analysis_controls]") {
    GUI::Schematic schematic;
    OpenRCFilter(schematic);
    SelectLongTransient(schematic);
    GUI::AnalysisControls controls(schematic);

    controls.RunSelected();
    const auto deadline = std::chrono::steady_clock::now() + WAIT_LIMIT;
    while (!schematic.GetTransient() && std::chrono::steady_clock::now() < deadline) {
        controls.Update();
        std::this_thread::sleep_for(1ms);
    }
    REQUIRE(schematic.GetTransient().has_value());
    controls.StopRun();
    CHECK(controls.IsStopping());
    REQUIRE(UpdateUntilIdle(controls));

    REQUIRE(schematic.GetTransient().has_value());
    CHECK_FALSE(schematic.GetTransient()->IsComplete);
    CHECK_FALSE(schematic.GetTransient()->Times.empty());
    CHECK(schematic.GetTransient()->Times.back() < schematic.GetSimulationSettings().Transient.StopTime);
    CHECK(controls.GetOutcome(GUI::Analysis::TRANSIENT) == GUI::RunOutcome::STOPPED);
}

TEST_CASE("Editing the circuit during a transient drops what it made", "[analysis_controls]") {
    GUI::Schematic schematic;
    OpenRCFilter(schematic);
    SelectLongTransient(schematic);
    GUI::AnalysisControls controls(schematic);

    controls.RunSelected();
    const auto deadline = std::chrono::steady_clock::now() + WAIT_LIMIT;
    while (!schematic.GetTransient() && std::chrono::steady_clock::now() < deadline) {
        controls.Update();
        std::this_thread::sleep_for(1ms);
    }
    schematic.MarkModified();
    REQUIRE(UpdateUntilIdle(controls));
    CHECK_FALSE(schematic.GetTransient().has_value());
    CHECK(controls.GetOutcome(GUI::Analysis::TRANSIENT) == GUI::RunOutcome::NONE);
}
