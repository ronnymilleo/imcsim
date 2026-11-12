/**
 * @file    analysis_controls_tests.cpp
 * @brief   Tests for the runs that AnalysisControls starts on the worker thread and takes in when they end.
 */

#include "analysis/analysis_controls.h"
#include "analysis/monte_carlo_results.h"

#include "backends/nodalys/nodalys_backend.h"
#include "circuit/components/capacitor.h"
#include "circuit/components/resistor.h"
#include "circuit/components/source.h"
#include "circuit/components/subcircuit_pin.h"
#include "document/examples.h"
#include "document/schematic.h"
#include "editing/live_input.h"
#include "simulation/simulation_backend.h"
#include "test_printers.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace {

using namespace std::chrono_literals;

/// The longest a test waits for a run before it calls it lost.
constexpr auto WAIT_LIMIT = 20s;

/**
 * @brief   Opens the RC low-pass example, as the editor does, with a transient heavy enough to take a while when
 *          stepped.
 */
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

/**
 * @brief   A transient of half a second at 1 us, which takes long enough to stop in the middle.
 */
void SelectLongTransient(GUI::Schematic &schematic) {
    GUI::SimulationSettings settings = schematic.GetSimulationSettings();
    settings.Selected = GUI::Analysis::TRANSIENT;
    settings.Transient = {.StopTime = 0.5, .TimeStep = 1e-6};
    schematic.SetSimulationSettings(settings);
}

/**
 * @brief   A transient stepped through 50 resistances, each long enough that the run lasts far beyond a few frames.
 */
void SelectLongSteppedTransient(GUI::Schematic &schematic) {
    GUI::SimulationSettings settings = schematic.GetSimulationSettings();
    settings.Selected = GUI::Analysis::TRANSIENT;
    settings.Transient = {.StopTime = 0.05, .TimeStep = 1e-6};
    settings.ParameterStep = {
        .Enabled = true, .Part = "R1", .UsesList = false, .Start = 100.0, .Stop = 5000.0, .Step = 100.0, .List = ""};
    schematic.SetSimulationSettings(settings);
}

/**
 * @brief   Opens an example by its file name, as the editor does.
 */
void OpenExample(GUI::Schematic &schematic, const std::string_view file_name) {
    const auto example = std::ranges::find(GUI::GetExamples(), file_name, &GUI::Example::FileName);
    REQUIRE(example != GUI::GetExamples().end());
    auto loaded = GUI::LoadExample(*example);
    REQUIRE(loaded.has_value());
    schematic.Replace(std::move(loaded->Elements), std::move(loaded->Wires), std::move(loaded->Settings),
                      std::move(loaded->Subcircuits), std::move(loaded->Models));
}

/**
 * @brief   A transient of 10 ms that a live run spreads over the shortest time the user may set.
 */
void SelectLiveTransient(GUI::Schematic &schematic) {
    GUI::SimulationSettings settings = schematic.GetSimulationSettings();
    settings.Selected = GUI::Analysis::TRANSIENT;
    settings.Transient = {.StopTime = 10e-3, .TimeStep = 10e-6};
    settings.LiveSeconds = GUI::MIN_LIVE_SECONDS;
    settings.ParameterStep.Enabled = false;
    schematic.SetSimulationSettings(settings);
}

std::size_t FindElement(const GUI::Schematic &schematic, const std::string_view name) {
    const auto &elements = schematic.GetElements();
    for (std::size_t index = 0; index < elements.size(); ++index) {
        if (elements[index]->GetComponent().GetName() == name) {
            return index;
        }
    }
    FAIL("No part named " << name);
    return 0;
}

/**
 * @brief   The current of a part at the first point at or after an instant.
 */
double CurrentAt(const Core::Transient &transient, const std::string_view name, const double time) {
    const auto trace = std::ranges::find(transient.Currents, name, &Core::ComponentTrace::Name);
    REQUIRE(trace != transient.Currents.end());
    const auto point = std::ranges::lower_bound(transient.Times, time);
    REQUIRE(point != transient.Times.end());
    return trace->Values[static_cast<std::size_t>(point - transient.Times.begin())];
}

/**
 * @brief   Calls Update() as the frame loop does until the run ends.
 */
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

// Calls Update() as the frame loop does until a condition holds
template <typename Condition> bool UpdateUntil(GUI::AnalysisControls &controls, const Condition &condition) {
    const auto deadline = std::chrono::steady_clock::now() + WAIT_LIMIT;
    while (std::chrono::steady_clock::now() < deadline) {
        controls.Update();
        if (condition()) {
            return true;
        }
        std::this_thread::sleep_for(1ms);
    }
    return false;
}

/**
 * @brief   Calls Update() as the frame loop does for a while.
 */
void UpdateFor(GUI::AnalysisControls &controls, const std::chrono::milliseconds duration) {
    const auto end = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < end) {
        controls.Update();
        std::this_thread::sleep_for(1ms);
    }
}

/**
 * @brief   The time the transient on display has reached, or 0 without one.
 */
double ReachedTime(const GUI::Schematic &schematic) {
    const std::optional<Core::Transient> &transient = schematic.GetTransient();
    return transient && !transient->Times.empty() ? transient->Times.back() : 0.0;
}

/**
 * @brief   Runs the selected analysis and takes its result in, as the frame loop does.
 */
void RunAndWait(GUI::AnalysisControls &controls) {
    controls.RunSelected();
    REQUIRE(UpdateUntilIdle(controls));
}

/**
 * @brief   Opens the Monte Carlo example, an RC low-pass with a toleranced resistor and capacitor, and picks the
 *          analysis and the number of runs.
 */
void OpenMonteCarloFilter(GUI::Schematic &schematic, const GUI::Analysis analysis, const int runs) {
    OpenExample(schematic, "monte_carlo_rc_low_pass.imcsim");
    GUI::SimulationSettings settings = schematic.GetSimulationSettings();
    settings.Selected = analysis;
    settings.MonteCarlo.Runs = runs;
    schematic.SetSimulationSettings(settings);
}

/// Answers a transient with a made-up result, and fails the runs whose R1 is above 1k, as a circuit that does not
/// converge would.
class FailingAboveBackend : public Core::SimulationBackend {
public:
    FailingAboveBackend()
        : m_Capabilities{.Analyses = {Core::AnalysisKind::OPERATING_POINT, Core::AnalysisKind::TRANSIENT,
                                      Core::AnalysisKind::AC_SWEEP, Core::AnalysisKind::DC_SWEEP},
                         .UnsupportedParts = {},
                         .UnsupportedPartsByAnalysis = {}} {}

    std::string_view GetName() const override { return "failing"; }
    const Core::BackendCapabilities &GetCapabilities() const override { return m_Capabilities; }
    Core::OperatingPointRun RunOperatingPoint(const Core::Circuit &, const std::optional<double> &) override {
        return {std::unexpected("no"), {}};
    }
    Core::TransientRun RunTransient(const Core::Circuit &circuit, const Core::TransientSettings &,
                                    const std::optional<double> &, const Core::RunMonitor &) override {
        for (const Core::CircuitEntry &entry : circuit.GetEntries()) {
            if (entry.Part->GetName() == "R1" && entry.Part->GetValue() > 1e3) {
                return {std::unexpected("no convergence"), {}};
            }
        }
        return {Core::Transient{.Times = {0.0, 1.0}, .NodeVoltages = {{0.0, 0.0}, {1.0, 1.0}, {2.0, 2.0}}}, {}};
    }
    Core::ACSweepRun RunACSweep(const Core::Circuit &, const Core::ACSweepSettings &,
                                const std::optional<double> &) override {
        return {std::unexpected("no"), {}};
    }
    Core::DCSweepRun RunDCSweep(const Core::Circuit &, const Core::DCSweepSettings &,
                                const std::optional<double> &) override {
        return {std::unexpected("no"), {}};
    }
    Core::NoiseRun RunNoise(const Core::Circuit &, const Core::NoiseSettings &) override {
        return {std::unexpected("no"), {}};
    }
    Core::TransferFunctionRun RunTransferFunction(const Core::Circuit &,
                                                  const Core::TransferFunctionSettings &) override {
        return {std::unexpected("no"), {}};
    }
    Core::SensitivityRun RunSensitivity(const Core::Circuit &, const Core::SensitivitySettings &) override {
        return {std::unexpected("no"), {}};
    }
    Core::PoleZeroRun RunPoleZero(const Core::Circuit &, const Core::PoleZeroSettings &) override {
        return {std::unexpected("no"), {}};
    }
    Core::SParametersRun RunSParameters(const Core::Circuit &, const Core::SParameterSettings &) override {
        return {std::unexpected("no"), {}};
    }

private:
    Core::BackendCapabilities m_Capabilities;
};

/// Puts the failing backend in use for one test and goes back to ngspice at the end, even when a check throws.
struct FailingBackendInUse {
    FailingBackendInUse() { Core::SetBackend(std::make_unique<FailingAboveBackend>()); }
    ~FailingBackendInUse() { Core::SetBackend(nullptr); }
    FailingBackendInUse(const FailingBackendInUse &) = delete;
    FailingBackendInUse &operator=(const FailingBackendInUse &) = delete;
};

/**
 * @class   TransientHold
 * @brief   Holds the worker inside a transient until the test lets it go, so a test can act while a run is surely in
 *          progress, however slow the build.
 */
class TransientHold {
public:
    void Enter() {
        std::unique_lock lock(m_Mutex);
        m_IsHolding = true;
        m_Changed.notify_all();
        m_Changed.wait(lock, [this] { return m_IsReleased; });
    }
    [[nodiscard]] bool WaitUntilHolding() {
        std::unique_lock lock(m_Mutex);
        return m_Changed.wait_for(lock, WAIT_LIMIT, [this] { return m_IsHolding; });
    }
    void Release() {
        const std::lock_guard lock(m_Mutex);
        m_IsReleased = true;
        m_Changed.notify_all();
    }

private:
    std::mutex m_Mutex;
    std::condition_variable m_Changed;
    bool m_IsHolding = false;
    bool m_IsReleased = false;
};

/// Answers as FailingAboveBackend does, once its hold lets the first transient go.
class HeldBackend : public FailingAboveBackend {
public:
    explicit HeldBackend(std::shared_ptr<TransientHold> hold) : m_Hold(std::move(hold)) {}

    Core::TransientRun RunTransient(const Core::Circuit &circuit, const Core::TransientSettings &settings,
                                    const std::optional<double> &temperature_celsius,
                                    const Core::RunMonitor &monitor) override {
        m_Hold->Enter();
        return FailingAboveBackend::RunTransient(circuit, settings, temperature_celsius, monitor);
    }

private:
    std::shared_ptr<TransientHold> m_Hold;
};

/// Puts the held backend in use for one test and goes back to ngspice at the end.
struct HeldBackendInUse {
    explicit HeldBackendInUse(const std::shared_ptr<TransientHold> &hold) {
        Core::SetBackend(std::make_unique<HeldBackend>(hold));
    }
    ~HeldBackendInUse() { Core::SetBackend(nullptr); }
    HeldBackendInUse(const HeldBackendInUse &) = delete;
    HeldBackendInUse &operator=(const HeldBackendInUse &) = delete;
};

/// Lets a held run go when the test ends, before the worker it holds is joined, even when a check throws.
struct ReleaseAtEnd {
    std::shared_ptr<TransientHold> Hold;
    ~ReleaseAtEnd() { Hold->Release(); }
};

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

TEST_CASE("A finished run keeps the time it took, for the status bar", "[analysis_controls]") {
    GUI::Schematic schematic;
    OpenRCFilter(schematic);
    Select(schematic, GUI::Analysis::TRANSIENT);
    GUI::AnalysisControls controls(schematic);
    CHECK_FALSE(controls.GetRunSeconds(GUI::Analysis::TRANSIENT).has_value());

    controls.RunSelected();
    REQUIRE(UpdateUntilIdle(controls));
    const std::optional<double> seconds = controls.GetRunSeconds(GUI::Analysis::TRANSIENT);
    REQUIRE(seconds.has_value());
    CHECK(*seconds >= 0.0);
    // Only the analysis that ran has a time
    CHECK_FALSE(controls.GetRunSeconds(GUI::Analysis::AC_SWEEP).has_value());
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
    const auto hold = std::make_shared<TransientHold>();
    const HeldBackendInUse backend(hold);
    GUI::Schematic schematic;
    OpenRCFilter(schematic);
    SelectLongSteppedTransient(schematic);
    GUI::AnalysisControls controls(schematic);
    const ReleaseAtEnd release{.Hold = hold};

    // The stop comes while the first step is held, so the run cannot end before it
    controls.RunSelected();
    REQUIRE(hold->WaitUntilHolding());
    controls.StopRun();
    CHECK(controls.IsStopping());
    hold->Release();
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

    Select(schematic, GUI::Analysis::SENSITIVITY);
    RunAndWait(controls);
    REQUIRE(schematic.GetSensitivity().has_value());
    CHECK_FALSE(schematic.GetSensitivity()->Parts.empty());
    CHECK(controls.GetLastRun() == GUI::Analysis::SENSITIVITY);

    Select(schematic, GUI::Analysis::POLE_ZERO);
    RunAndWait(controls);
    REQUIRE(schematic.GetPoleZero().has_value());
    CHECK_FALSE(schematic.GetPoleZero()->Roots.empty());
    CHECK(controls.GetLastRun() == GUI::Analysis::POLE_ZERO);
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

TEST_CASE("A stepped operating point delivers one result per value and shows the first", "[analysis_controls]") {
    GUI::Schematic schematic;
    OpenRCFilter(schematic);
    GUI::AnalysisControls controls(schematic);
    GUI::SimulationSettings settings = schematic.GetSimulationSettings();
    settings.Selected = GUI::Analysis::OPERATING_POINT;
    settings.ParameterStep = {
        .Enabled = true, .Part = "R1", .UsesList = true, .Start = 0.0, .Stop = 0.0, .Step = 0.0, .List = "1k, 2k, 5k"};
    schematic.SetSimulationSettings(settings);
    RunAndWait(controls);
    REQUIRE_FALSE(controls.GetError(GUI::Analysis::OPERATING_POINT));
    REQUIRE(schematic.GetOperatingPointSteps().has_value());
    const GUI::StepFamily<Core::OperatingPoint> &steps = *schematic.GetOperatingPointSteps();
    CHECK(steps.Parameter == "R1");
    CHECK(steps.Values == std::vector<double>{1e3, 2e3, 5e3});
    CHECK(steps.Results.size() == 3);
    CHECK(steps.Selected == 0);
    CHECK(schematic.GetOperatingPoint().has_value());
}

TEST_CASE("A Monte Carlo operating point measures every node voltage and every current in each run",
          "[analysis_controls][monte_carlo]") {
    GUI::Schematic schematic;
    OpenMonteCarloFilter(schematic, GUI::Analysis::OPERATING_POINT, 4);
    GUI::AnalysisControls controls(schematic);
    RunAndWait(controls);
    REQUIRE_FALSE(controls.GetError(GUI::Analysis::OPERATING_POINT));
    REQUIRE(schematic.GetOperatingPointSteps().has_value());
    CHECK(schematic.GetOperatingPointSteps()->Parameter == GUI::MONTE_CARLO_PARAMETER);
    CHECK(schematic.GetOperatingPointSteps()->Results.size() == 5);
    REQUIRE(schematic.GetMonteCarlo().has_value());
    const GUI::MonteCarloTable &table = *schematic.GetMonteCarlo();
    CHECK(table.Runs.size() == 5);
    CHECK_FALSE(GUI::ListTableNodes(table).empty());
    CHECK(std::ranges::contains(GUI::ListTableCurrents(table), "R1"));
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
    for (const GUI::Analysis analysis : {GUI::Analysis::DC_SWEEP, GUI::Analysis::NOISE,
                                         GUI::Analysis::TRANSFER_FUNCTION, GUI::Analysis::SENSITIVITY}) {
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
    CHECK(std::string(GUI::GetAnalysisName(GUI::Analysis::SENSITIVITY)) == "Sensitivity");
    CHECK(std::string(GUI::GetAnalysisName(GUI::Analysis::POLE_ZERO)) == "Poles and zeros");
    CHECK(std::string(GUI::GetAnalysisName(GUI::Analysis::S_PARAMETERS)) == "S-parameters");
    CHECK(std::string(GUI::GetAnalysisName(GUI::Analysis::TRUTH_TABLE)) == "Truth table");
    CHECK(std::string(GUI::GetAnalysisName(GUI::Analysis::HARMONIC_BALANCE)) == "Harmonic balance");
    CHECK(static_cast<std::size_t>(GUI::Analysis::HARMONIC_BALANCE) + 1 == GUI::ANALYSIS_COUNT);
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

TEST_CASE("Live is for a transient that is not stepped, and says why for anything else", "[analysis_controls][live]") {
    GUI::Schematic schematic;
    OpenRCFilter(schematic);
    GUI::AnalysisControls controls(schematic);
    CHECK_FALSE(controls.IsLiveModeOn());
    controls.SetLiveMode(true);
    CHECK(controls.IsLiveModeOn());

    SelectLiveTransient(schematic);
    CHECK_FALSE(controls.FindLiveObstacle().has_value());
    Select(schematic, GUI::Analysis::AC_SWEEP);
    CHECK(controls.FindLiveObstacle().has_value());
    SelectLongSteppedTransient(schematic);
    CHECK(controls.FindLiveObstacle().has_value());

    // A stepped transient runs as usual, whatever the mode
    controls.RunSelected();
    CHECK_FALSE(controls.IsLive());
    controls.StopRun();
    REQUIRE(UpdateUntilIdle(controls));
}

TEST_CASE("A live run goes on past its stop time and a switch flipped on the way changes it from then on",
          "[analysis_controls][live]") {
    GUI::Schematic schematic;
    OpenExample(schematic, "switches_and_or.imcsim");
    SelectLiveTransient(schematic);
    GUI::AnalysisControls controls(schematic);
    controls.SetLiveMode(true);

    controls.RunSelected();
    CHECK(controls.IsLive());
    CHECK(controls.IsLiveResult());
    CHECK(schematic.IsLiveRun());
    // SW1 is closed and SW2 open, so the LED behind both is dark until SW2 closes, a few milliseconds in
    REQUIRE(UpdateUntil(controls, [&] { return ReachedTime(schematic) > 3e-3; }));
    const double flipped_at = ReachedTime(schematic);
    const std::size_t circuit_version = schematic.GetCircuitVersion();
    Core::Component &switch_two = schematic.GetElement(FindElement(schematic, "SW2")).GetComponent();
    GUI::Flip(switch_two);
    schematic.MarkLiveChanged(switch_two);
    // The run goes on with the change instead of stopping
    CHECK(schematic.GetCircuitVersion() == circuit_version);
    const double stop_time = schematic.GetSimulationSettings().Transient.StopTime;
    REQUIRE(UpdateUntil(controls, [&] { return ReachedTime(schematic) > stop_time * 1.2; }));

    CHECK(controls.IsRunning());
    CHECK(controls.IsLive());
    const Core::Transient &transient = *schematic.GetTransient();
    CHECK_FALSE(transient.IsComplete);
    CHECK(std::abs(CurrentAt(transient, "D1", flipped_at / 2.0)) < 1e-6);
    CHECK(CurrentAt(transient, "D1", transient.Times.back()) > 1e-3);

    // Discard ends the run and drops what it made
    controls.DiscardLive();
    CHECK_FALSE(controls.IsLive());
    CHECK_FALSE(schematic.IsLiveRun());
    CHECK_FALSE(schematic.GetTransient().has_value());
    REQUIRE(UpdateUntil(controls, [&] { return !controls.GetRunningAnalysis(); }));
    CHECK_FALSE(schematic.GetTransient().has_value());
    CHECK(controls.GetOutcome(GUI::Analysis::TRANSIENT) == GUI::RunOutcome::NONE);
}

TEST_CASE("A run holds the backend until it ends, a paused live run too, so it is not replaced under it",
          "[analysis_controls][live]") {
    GUI::Schematic schematic;
    OpenRCFilter(schematic);
    SelectLiveTransient(schematic);
    GUI::AnalysisControls controls(schematic);
    CHECK_FALSE(controls.IsBackendInUse());
    controls.SetLiveMode(true);

    controls.RunSelected();
    CHECK(controls.IsBackendInUse());
    REQUIRE(UpdateUntil(controls, [&] { return ReachedTime(schematic) > 1e-3; }));
    controls.StopRun();
    // Paused, the run is not running, yet its worker still holds the backend
    CHECK_FALSE(controls.IsRunning());
    CHECK(controls.IsBackendInUse());

    controls.DiscardLive();
    REQUIRE(UpdateUntil(controls, [&] { return !controls.IsBackendInUse(); }));
}

TEST_CASE("Stopping a live run pauses it with its points, Continue takes it on and Run starts over",
          "[analysis_controls][live]") {
    GUI::Schematic schematic;
    OpenRCFilter(schematic);
    SelectLiveTransient(schematic);
    GUI::AnalysisControls controls(schematic);
    controls.SetLiveMode(true);

    controls.RunSelected();
    CHECK(controls.GetRunAction() == GUI::RunAction::STOP);
    REQUIRE(UpdateUntil(controls, [&] { return ReachedTime(schematic) > 2e-3; }));
    controls.StopRun();
    CHECK(controls.IsLivePaused());
    CHECK_FALSE(controls.IsRunning());
    CHECK(controls.IsLive());
    CHECK(controls.GetRunAction() == GUI::RunAction::CONTINUE);
    // The chunk under way may still arrive; after that the run holds
    UpdateFor(controls, 100ms);
    const double paused_at = ReachedTime(schematic);
    UpdateFor(controls, 300ms);
    CHECK(ReachedTime(schematic) == paused_at);
    REQUIRE(schematic.GetTransient().has_value());
    CHECK_FALSE(schematic.GetTransient()->IsComplete);

    controls.PressRun();
    CHECK_FALSE(controls.IsLivePaused());
    CHECK(controls.IsRunning());
    REQUIRE(UpdateUntil(controls, [&] { return ReachedTime(schematic) > paused_at + 1e-3; }));
    // What the run made before the pause is still there
    CHECK(schematic.GetTransient()->Times.front() == 0.0);

    // Run, not Continue, starts a new run from zero once the paused one has ended
    controls.StopRun();
    REQUIRE(controls.IsLivePaused());
    const double restarted_from = ReachedTime(schematic);
    controls.RunSelected();
    REQUIRE(UpdateUntil(controls, [&] {
        return controls.IsRunning() && schematic.GetTransient() && ReachedTime(schematic) < restarted_from;
    }));
    CHECK(controls.IsLive());
    controls.DiscardLive();
    REQUIRE(UpdateUntil(controls, [&] { return !controls.GetRunningAnalysis(); }));
}

TEST_CASE("Running another analysis while a live run is paused ends the live run and keeps its points",
          "[analysis_controls][live]") {
    GUI::Schematic schematic;
    OpenRCFilter(schematic);
    SelectLiveTransient(schematic);
    GUI::AnalysisControls controls(schematic);
    controls.SetLiveMode(true);

    controls.RunSelected();
    REQUIRE(UpdateUntil(controls, [&] { return ReachedTime(schematic) > 1e-3; }));
    controls.StopRun();
    Select(schematic, GUI::Analysis::OPERATING_POINT);
    CHECK(controls.GetRunAction() == GUI::RunAction::RUN);
    controls.PressRun();
    REQUIRE(UpdateUntil(controls, [&] { return schematic.GetOperatingPoint().has_value(); }));
    REQUIRE(UpdateUntilIdle(controls));

    CHECK_FALSE(controls.IsLive());
    CHECK_FALSE(schematic.IsLiveRun());
    REQUIRE(schematic.GetTransient().has_value());
    CHECK_FALSE(schematic.GetTransient()->IsComplete);
    CHECK(controls.GetOutcome(GUI::Analysis::TRANSIENT) == GUI::RunOutcome::STOPPED);
}

TEST_CASE("A Monte Carlo AC sweep delivers the nominal run first and a table of every run",
          "[analysis_controls][monte_carlo]") {
    GUI::Schematic schematic;
    OpenMonteCarloFilter(schematic, GUI::Analysis::AC_SWEEP, 20);
    GUI::AnalysisControls controls(schematic);

    RunAndWait(controls);
    REQUIRE_FALSE(controls.GetError(GUI::Analysis::AC_SWEEP));
    REQUIRE(schematic.GetACSweepSteps().has_value());
    const GUI::StepFamily<Core::ACSweep> &family = *schematic.GetACSweepSteps();
    CHECK(family.Parameter == GUI::MONTE_CARLO_PARAMETER);
    CHECK(family.Results.size() == 21);
    CHECK(family.Values.front() == 0.0);
    CHECK(family.Values.back() == 20.0);
    REQUIRE(schematic.GetMonteCarlo().has_value());
    const GUI::MonteCarloTable &table = *schematic.GetMonteCarlo();
    CHECK(table.Runs.size() == 21);
    CHECK(table.Parts == std::vector<std::string>{"R1", "C1"});
    CHECK(table.Runs.front().Values == std::vector<double>{1e3, 100e-9});
    CHECK(table.Runs[1].Values != table.Runs.front().Values);
    CHECK_FALSE(table.Columns.empty());
    CHECK(controls.GetOutcome(GUI::Analysis::AC_SWEEP) == GUI::RunOutcome::SUCCEEDED);
}

TEST_CASE("The same seed draws the same values and another seed draws others", "[analysis_controls][monte_carlo]") {
    GUI::Schematic schematic;
    OpenMonteCarloFilter(schematic, GUI::Analysis::AC_SWEEP, 5);
    GUI::AnalysisControls controls(schematic);
    RunAndWait(controls);
    const std::vector<double> first = schematic.GetMonteCarlo()->Runs[3].Values;
    RunAndWait(controls);
    CHECK(schematic.GetMonteCarlo()->Runs[3].Values == first);

    GUI::SimulationSettings settings = schematic.GetSimulationSettings();
    settings.MonteCarlo.Seed += 1;
    schematic.SetSimulationSettings(settings);
    RunAndWait(controls);
    CHECK(schematic.GetMonteCarlo()->Runs[3].Values != first);
}

TEST_CASE("The cutoff of the filter spreads around its nominal value over the runs",
          "[analysis_controls][monte_carlo]") {
    GUI::Schematic schematic;
    OpenMonteCarloFilter(schematic, GUI::Analysis::AC_SWEEP, 100);
    GUI::AnalysisControls controls(schematic);
    RunAndWait(controls);
    REQUIRE(schematic.GetMonteCarlo().has_value());
    const GUI::MonteCarloTable &table = *schematic.GetMonteCarlo();
    const auto column =
        std::ranges::find(table.Columns, GUI::MeasureColumn{.Node = 2, .Kind = GUI::MeasureKind::UPPER_CUTOFF});
    REQUIRE(column != table.Columns.end());
    const auto index = static_cast<std::size_t>(column - table.Columns.begin());
    const std::vector<double> samples = GUI::CollectMeasure(table, index);
    CHECK(samples.size() == 100);
    const std::optional<GUI::SampleStatistics> statistics = GUI::ComputeSampleStatistics(samples);
    REQUIRE(statistics);
    const double nominal = 1.0 / (2.0 * 3.14159265358979 * 1e3 * 100e-9);
    CHECK(std::abs(statistics->Mean - nominal) < 0.02 * nominal);
    // R within 5 % and C within 10 %, each at three deviations, give about 3.7 % in all
    CHECK(statistics->StandardDeviation > 0.02 * nominal);
    CHECK(statistics->StandardDeviation < 0.06 * nominal);
    CHECK(statistics->Maximum < 1.13 * nominal);
    CHECK(statistics->Minimum > 0.88 * nominal);

    // The tab opens on this quantity, whichever node is measured first, since nothing else in the filter spreads
    const std::vector<int> measured = {1, 2};
    const GUI::MeasureColumn chosen = table.Columns[GUI::FindDefaultColumn(table, measured)];
    INFO(chosen.Node << " " << GUI::GetMeasureName(chosen.Kind));
    CHECK(chosen == *column);
}

TEST_CASE("Only the first runs keep their traces, and every run keeps its measurements",
          "[analysis_controls][monte_carlo]") {
    GUI::Schematic schematic;
    OpenMonteCarloFilter(schematic, GUI::Analysis::AC_SWEEP, 150);
    GUI::AnalysisControls controls(schematic);
    RunAndWait(controls);
    REQUIRE(schematic.GetACSweepSteps().has_value());
    CHECK(schematic.GetACSweepSteps()->Results.size() == 101);
    CHECK(schematic.GetMonteCarlo()->Runs.size() == 151);
    CHECK(schematic.GetMonteCarlo()->Runs.back().Measures.size() == schematic.GetMonteCarlo()->Columns.size());
}

TEST_CASE("A Monte Carlo transient and DC sweep deliver their runs as steps and curves",
          "[analysis_controls][monte_carlo]") {
    GUI::Schematic schematic;
    OpenMonteCarloFilter(schematic, GUI::Analysis::TRANSIENT, 4);
    GUI::AnalysisControls controls(schematic);
    RunAndWait(controls);
    REQUIRE(schematic.GetTransientSteps().has_value());
    CHECK(schematic.GetTransientSteps()->Results.size() == 5);
    CHECK(schematic.GetMonteCarlo()->Runs.size() == 5);
    // The run on show is the nominal one
    CHECK(schematic.GetTransientSteps()->Selected == 0);

    Select(schematic, GUI::Analysis::DC_SWEEP);
    RunAndWait(controls);
    REQUIRE(schematic.GetDCSweep().has_value());
    CHECK(schematic.GetDCSweep()->Curves.size() == 5);
    CHECK(schematic.GetDCSweep()->SteppedSource == GUI::MONTE_CARLO_PARAMETER);
    CHECK(schematic.GetDCSweep()->Curves[3].StepValue == 3.0);
    CHECK(schematic.GetMonteCarlo().has_value());
}

TEST_CASE("A new single run drops the Monte Carlo table", "[analysis_controls][monte_carlo]") {
    GUI::Schematic schematic;
    OpenMonteCarloFilter(schematic, GUI::Analysis::AC_SWEEP, 3);
    GUI::AnalysisControls controls(schematic);
    RunAndWait(controls);
    REQUIRE(schematic.GetMonteCarlo().has_value());
    GUI::SimulationSettings settings = schematic.GetSimulationSettings();
    settings.MonteCarlo.Enabled = false;
    schematic.SetSimulationSettings(settings);
    RunAndWait(controls);
    CHECK_FALSE(schematic.GetMonteCarlo().has_value());
    CHECK_FALSE(schematic.GetACSweepSteps().has_value());
    CHECK(schematic.GetACSweep().has_value());
}

TEST_CASE("A Monte Carlo run without a tolerance, or with a second stepped source, says why it cannot start",
          "[analysis_controls][monte_carlo]") {
    GUI::Schematic schematic;
    OpenMonteCarloFilter(schematic, GUI::Analysis::AC_SWEEP, 5);
    GUI::AnalysisControls controls(schematic);
    for (const std::unique_ptr<GUI::UIElement> &element : schematic.GetElements()) {
        element->GetComponent().ClearTolerance();
    }
    schematic.MarkModified();
    controls.RunSelected();
    REQUIRE(controls.GetError(GUI::Analysis::AC_SWEEP));
    CHECK(controls.GetError(GUI::Analysis::AC_SWEEP)->contains("No part has a tolerance"));
    CHECK(controls.GetOutcome(GUI::Analysis::AC_SWEEP) == GUI::RunOutcome::FAILED);
    CHECK_FALSE(controls.IsRunning());

    OpenMonteCarloFilter(schematic, GUI::Analysis::DC_SWEEP, 5);
    GUI::SimulationSettings settings = schematic.GetSimulationSettings();
    settings.StepSource = true;
    schematic.SetSimulationSettings(settings);
    controls.RunSelected();
    REQUIRE(controls.GetError(GUI::Analysis::DC_SWEEP));
    CHECK(controls.GetError(GUI::Analysis::DC_SWEEP)->contains("second stepped source"));
}

TEST_CASE("Live is refused while Monte Carlo is on", "[analysis_controls][monte_carlo]") {
    GUI::Schematic schematic;
    OpenMonteCarloFilter(schematic, GUI::Analysis::TRANSIENT, 5);
    GUI::AnalysisControls controls(schematic);
    const std::optional<std::string> obstacle = controls.FindLiveObstacle();
    REQUIRE(obstacle);
    CHECK(obstacle->contains("Monte Carlo"));
}

TEST_CASE("Stopping a Monte Carlo run keeps the runs made and says it stopped early",
          "[analysis_controls][monte_carlo]") {
    GUI::Schematic schematic;
    OpenMonteCarloFilter(schematic, GUI::Analysis::TRANSIENT, 1000);
    GUI::SimulationSettings settings = schematic.GetSimulationSettings();
    settings.Transient = {.StopTime = 0.5, .TimeStep = 1e-6};
    schematic.SetSimulationSettings(settings);
    GUI::AnalysisControls controls(schematic);

    controls.RunSelected();
    REQUIRE(UpdateUntil(controls, [&] { return controls.GetRunProgress() > 0.0; }));
    controls.StopRun();
    REQUIRE(UpdateUntilIdle(controls));

    REQUIRE(schematic.GetTransientSteps().has_value());
    REQUIRE(schematic.GetMonteCarlo().has_value());
    CHECK(schematic.GetMonteCarlo()->Runs.size() < 1001);
    CHECK(schematic.GetTransientSteps()->Results.size() == schematic.GetMonteCarlo()->Runs.size());
    CHECK(controls.GetOutcome(GUI::Analysis::TRANSIENT) == GUI::RunOutcome::SUCCEEDED_WITH_WARNINGS);
}

TEST_CASE("A run that fails is skipped with a warning, but the nominal run has to work",
          "[analysis_controls][monte_carlo]") {
    const FailingBackendInUse backend;
    GUI::Schematic schematic;
    OpenMonteCarloFilter(schematic, GUI::Analysis::TRANSIENT, 30);
    GUI::AnalysisControls controls(schematic);
    RunAndWait(controls);

    REQUIRE(schematic.GetMonteCarlo().has_value());
    const GUI::MonteCarloTable &table = *schematic.GetMonteCarlo();
    CHECK(table.Runs.size() == 31);
    const auto failed = std::ranges::count_if(table.Runs, &GUI::MonteCarloRun::IsFailed);
    CHECK(failed > 0);
    CHECK(failed < 30);
    CHECK(schematic.GetTransientSteps()->Results.size() == 31 - static_cast<std::size_t>(failed));
    CHECK(table.Runs.front().IsFailed == false);
    for (const GUI::MonteCarloRun &run : table.Runs) {
        CHECK(run.IsFailed == (run.Values[0] > 1e3));
    }
    CHECK(controls.GetOutcome(GUI::Analysis::TRANSIENT) == GUI::RunOutcome::SUCCEEDED_WITH_WARNINGS);

    // With the nominal circuit failing too, nothing is kept
    GUI::SimulationSettings settings = schematic.GetSimulationSettings();
    for (const std::unique_ptr<GUI::UIElement> &element : schematic.GetElements()) {
        if (element->GetComponent().GetName() == "R1") {
            element->GetComponent().SetValue(2e3);
            element->GetComponent().ClearTolerance();
        }
    }
    schematic.MarkModified();
    schematic.SetSimulationSettings(settings);
    controls.RunSelected();
    REQUIRE(UpdateUntilIdle(controls));
    REQUIRE(controls.GetError(GUI::Analysis::TRANSIENT));
    CHECK(controls.GetError(GUI::Analysis::TRANSIENT)->contains("The nominal run failed"));
    CHECK_FALSE(schematic.GetMonteCarlo().has_value());
}

TEST_CASE("The S-parameter analysis runs the example on the worker, and refuses a circuit with one port",
          "[analysis_controls]") {
    GUI::Schematic schematic;
    OpenExample(schematic, "s_parameters_lc_low_pass.imcsim");
    GUI::AnalysisControls controls(schematic);
    CHECK(schematic.GetSimulationSettings().Selected == GUI::Analysis::S_PARAMETERS);
    RunAndWait(controls);
    REQUIRE(schematic.GetSParameters().has_value());
    CHECK(schematic.GetSParameters()->GetPortCount() == 2);
    CHECK(controls.GetLastRun() == GUI::Analysis::S_PARAMETERS);
    CHECK(controls.GetOutcome(GUI::Analysis::S_PARAMETERS) == GUI::RunOutcome::SUCCEEDED);

    // Turning a port off leaves one; the run is refused before ngspice, which a single port would stop for good
    for (const auto &element : schematic.GetElements()) {
        if (element->GetComponent().GetType() == Core::ComponentType::PIN) {
            auto &marker = static_cast<Core::SubcircuitPinMarker &>(element->GetComponent());
            Core::PortSettings port = marker.GetPort();
            port.IsPort = port.Number == 1;
            marker.SetPort(port);
        }
    }
    schematic.MarkModified();
    RunAndWait(controls);
    CHECK(controls.GetOutcome(GUI::Analysis::S_PARAMETERS) == GUI::RunOutcome::FAILED);
    REQUIRE(controls.GetError(GUI::Analysis::S_PARAMETERS).has_value());
    CHECK(controls.GetError(GUI::Analysis::S_PARAMETERS)->contains("at least two S-parameter ports"));
    CHECK_FALSE(schematic.GetSParameters().has_value());
    // The library was not stopped: the operating point still runs
    Select(schematic, GUI::Analysis::OPERATING_POINT);
    RunAndWait(controls);
    CHECK(controls.GetOutcome(GUI::Analysis::OPERATING_POINT) != GUI::RunOutcome::FAILED);
    CHECK(schematic.GetOperatingPoint().has_value());
}

TEST_CASE("Harmonic balance runs on Nodalys through AnalysisControls and fails cleanly on ngspice",
          "[analysis_controls][hb]") {
    struct BackendRestorer {
        ~BackendRestorer() { Core::SetBackend(nullptr); }
    } restore;

    GUI::Schematic schematic;
    OpenRCFilter(schematic);
    Core::Source *source = nullptr;
    const Core::Resistor *resistor = nullptr;
    const Core::Capacitor *capacitor = nullptr;
    for (const auto &element : schematic.GetElements()) {
        Core::Component &component = element->GetComponent();
        if (component.GetType() == Core::ComponentType::VOLTAGE_SOURCE && source == nullptr) {
            source = &static_cast<Core::Source &>(component);
        } else if (component.GetType() == Core::ComponentType::RESISTOR && resistor == nullptr) {
            resistor = &static_cast<const Core::Resistor &>(component);
        } else if (component.GetType() == Core::ComponentType::CAPACITOR && capacitor == nullptr) {
            capacitor = &static_cast<const Core::Capacitor &>(component);
        }
    }
    REQUIRE(source != nullptr);
    REQUIRE(resistor != nullptr);
    REQUIRE(capacitor != nullptr);
    constexpr double FREQ = 1e3;
    constexpr double AMP = 2.0;
    source->SetSourceType(Core::Source::SourceType::AC);
    source->SetAC({.Amplitude = AMP, .Frequency = FREQ});
    const std::string resistor_name = resistor->GetName();
    schematic.MarkModified();

    GUI::SimulationSettings settings = schematic.GetSimulationSettings();
    settings.Selected = GUI::Analysis::HARMONIC_BALANCE;
    settings.HarmonicBalance = {.FundamentalFrequency = FREQ, .Harmonics = 4, .Samples = 0};
    schematic.SetSimulationSettings(settings);

    // Default ngspice backend refuses Harmonic Balance cleanly
    GUI::AnalysisControls controls(schematic);
    RunAndWait(controls);
    CHECK(controls.GetOutcome(GUI::Analysis::HARMONIC_BALANCE) == GUI::RunOutcome::FAILED);
    CHECK_FALSE(schematic.GetHarmonicBalance().has_value());
    REQUIRE(controls.GetError(GUI::Analysis::HARMONIC_BALANCE).has_value());
    CHECK(controls.GetError(GUI::Analysis::HARMONIC_BALANCE)->contains("does not run"));

    // Nodalys solves Harmonic Balance and delivers complex spectra matching analytical impedance
    Core::SetBackend(std::make_unique<Core::NodalysBackend>());
    RunAndWait(controls);
    CHECK(controls.GetOutcome(GUI::Analysis::HARMONIC_BALANCE) == GUI::RunOutcome::SUCCEEDED);
    REQUIRE(schematic.GetHarmonicBalance().has_value());
    const Core::HarmonicBalance &hb = *schematic.GetHarmonicBalance();
    CHECK(hb.Frequencies.size() == 5);
    const auto r_trace = std::ranges::find(hb.CurrentPhasors, resistor_name, &Core::ComponentPhasors::Name);
    REQUIRE(r_trace != hb.CurrentPhasors.end());
    REQUIRE(r_trace->Phasors.size() == 5);
    const double omega = 2.0 * std::numbers::pi * FREQ;
    const double reactance = 1.0 / (omega * capacitor->GetValue());
    const double expected_current = AMP / std::hypot(resistor->GetValue(), reactance);
    CHECK(std::abs(std::abs(r_trace->Phasors[1]) - expected_current) < 1e-6);
}
