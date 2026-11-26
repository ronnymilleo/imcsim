/**
 * @file    run_status_tests.cpp
 * @brief   Tests for the text of the run status in the status bar of the editor.
 */

#include "analysis/analysis_controls.h"
#include "analysis/run_status.h"
#include "schematic/examples.h"
#include "schematic/schematic.h"
#include "test_printers.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <optional>
#include <string>
#include <thread>

namespace {

using namespace std::chrono_literals;

Core::Transient MakeTransient(const std::size_t points) {
    Core::Transient transient;
    for (std::size_t index = 0; index < points; ++index) {
        transient.Times.push_back(static_cast<double>(index) * 1e-3);
    }
    return transient;
}

} // namespace

TEST_CASE("A run in progress tells its share done, or that it is stopping", "[run_status]") {
    CHECK(GUI::FormatRunProgress("Transient", false, 0.0) == "Transient running");
    CHECK(GUI::FormatRunProgress("Transient", false, 0.256) == "Transient running, 26%");
    CHECK(GUI::FormatRunProgress("AC Sweep", true, 0.5) == "Stopping AC Sweep");
}

TEST_CASE("A live run tells how far it has got, running or paused", "[run_status]") {
    CHECK(GUI::FormatLiveProgress(false, std::nullopt) == "Transient live");
    CHECK(GUI::FormatLiveProgress(true, Core::Transient{}) == "Transient paused");
    CHECK(GUI::FormatLiveProgress(false, MakeTransient(13)) == "Transient live at 12.00ms");
    CHECK(GUI::FormatLiveProgress(true, MakeTransient(13)) == "Transient paused at 12.00ms");
}

TEST_CASE("Live time formatting keeps fixed decimals per engineering decade", "[run_status]") {
    CHECK(GUI::FormatLiveTime(0.0) == "0s");
    CHECK(GUI::FormatLiveTime(1.234e-3) == "1.234ms");
    CHECK(GUI::FormatLiveTime(12.3e-3) == "12.30ms");
    CHECK(GUI::FormatLiveTime(123.4e-3) == "123.4ms");
    CHECK(GUI::FormatLiveTime(1.5) == "1.500s");
}

TEST_CASE("The cost of a run is the steps and the time of a transient, and the time of any other analysis",
          "[run_status]") {
    const std::optional<Core::Transient> transient = MakeTransient(2000);
    CHECK(GUI::DescribeRunCost(0.041, GUI::Analysis::TRANSIENT, transient) == "2000 time steps in 41ms");
    CHECK(GUI::DescribeRunCost(0.041, GUI::Analysis::TRANSIENT, std::nullopt) == "41ms");
    CHECK(GUI::DescribeRunCost(0.041, GUI::Analysis::AC_SWEEP, transient) == "41ms");
    // A live run keeps pace with the clock, so it has no cost to tell
    CHECK(GUI::DescribeRunCost(std::nullopt, GUI::Analysis::TRANSIENT, transient).empty());
}

TEST_CASE("Before any run the status has no text", "[run_status]") {
    GUI::Schematic schematic;
    GUI::ResultStore results;
    GUI::AnalysisControls controls(schematic, results);
    const GUI::RunStatusLine line = GUI::DescribeRunStatus(controls, std::nullopt);
    CHECK(line.Text.empty());
    CHECK_FALSE(line.IsRunning);
}

TEST_CASE("A finished run says so, with what it made and how long it took", "[run_status]") {
    GUI::Schematic schematic;
    GUI::ResultStore results;
    schematic.SetListener(&results);
    const auto example = std::ranges::find(GUI::GetExamples(), "rc_low_pass_filter.imcsim", &GUI::Example::FileName);
    REQUIRE(example != GUI::GetExamples().end());
    auto loaded = GUI::LoadExample(*example);
    REQUIRE(loaded.has_value());
    schematic.Replace(std::move(loaded->Elements), std::move(loaded->Wires), std::move(loaded->Settings));
    GUI::SimulationSettings settings = schematic.GetSimulationSettings();
    settings.Selected = GUI::Analysis::TRANSIENT;
    schematic.SetSimulationSettings(settings);
    GUI::AnalysisControls controls(schematic, results);

    controls.RunSelected();
    const GUI::RunStatusLine running = GUI::DescribeRunStatus(controls, std::nullopt);
    CHECK(running.IsRunning);
    CHECK(running.Text.starts_with("Transient"));

    const auto deadline = std::chrono::steady_clock::now() + 20s;
    while (controls.IsRunning() && std::chrono::steady_clock::now() < deadline) {
        controls.Update();
        std::this_thread::sleep_for(1ms);
    }
    REQUIRE_FALSE(controls.IsRunning());
    const GUI::RunStatusLine done = GUI::DescribeRunStatus(controls, results.GetTransient());
    CHECK_FALSE(done.IsRunning);
    CHECK(done.Text.starts_with("Transient done"));
    CHECK(done.Detail.find("time steps in") != std::string::npos);
    CHECK_FALSE(done.TakesWholeLine);
}

TEST_CASE("The Run button names the key of the Run command, except Stop, which the key never does", "[run_status]") {
    CHECK(GUI::GetRunButtonLabel(GUI::RunAction::RUN) == "Run (F5)");
    CHECK(GUI::GetRunButtonLabel(GUI::RunAction::CONTINUE) == "Continue (F5)");
    CHECK(GUI::GetRunButtonLabel(GUI::RunAction::STOP) == "Stop");
}
