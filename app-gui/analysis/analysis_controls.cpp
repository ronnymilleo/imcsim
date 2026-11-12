/**
 * @file    analysis_controls.cpp
 * @brief   Runs of the analyses on the worker thread and what they leave in the results, shared by the editor
 *          toolbar and the Analysis Settings window.
 */

#include "analysis/analysis_controls.h"

#include "analysis/analysis_inputs.h"
#include "base/theme.h"
#include "imgui.h"
#include <algorithm>
#include <chrono>
#include <iterator>
#include <utility>

namespace GUI {

namespace {

std::size_t ToIndex(const Analysis analysis) {
    return static_cast<std::size_t>(analysis);
}

/**
 * @brief   The warnings the stored result of one analysis carries, or none when it has no result or no warnings.
 */
std::vector<std::string> ListWarningsOfResult(const ResultStore &results, const Analysis analysis) {
    if (analysis == Analysis::OPERATING_POINT && results.GetOperatingPoint()) {
        return results.GetOperatingPoint()->Warnings;
    }
    if (analysis == Analysis::TRANSIENT && results.GetTransient()) {
        return results.GetTransient()->Warnings;
    }
    if (analysis == Analysis::POLE_ZERO && results.GetPoleZero()) {
        return results.GetPoleZero()->Warnings;
    }
    if (analysis == Analysis::S_PARAMETERS && results.GetSParameters()) {
        return results.GetSParameters()->Warnings;
    }
    if (analysis == Analysis::TRUTH_TABLE && results.GetTruthTable()) {
        return results.GetTruthTable()->Warnings;
    }
    if (analysis == Analysis::HARMONIC_BALANCE && results.GetHarmonicBalance()) {
        return results.GetHarmonicBalance()->Warnings;
    }
    return {};
}

} // namespace
/**
 * @brief   Creates the controls for a schematic.
 * @param[in] schematic  Schematic whose analyses are set and run; it must outlive the controls.
 * @param[in] results    Where the results of the runs go; they must outlive the controls.
 */
AnalysisControls::AnalysisControls(Schematic &schematic, ResultStore &results)
    : m_Schematic(schematic), m_Results(results) {
}

/**
 * @brief   Starts the selected analysis with its settings on the worker thread.
 * @note    The result, or the error, goes to the results when the run ends, which Update() notices. Nothing happens
 *          while another run is in progress. A paused live run holds the worker, so it ends first, keeping its points,
 *          and the analysis starts in the Update() that sees it end.
 */
void AnalysisControls::RunSelected() {
    if (IsRunning()) {
        return;
    }
    if (m_RunningAnalysis) {
        m_Service.Stop();
        m_IsRunPending = true;
        return;
    }
    const SimulationSettings &settings = m_Schematic.GetSimulationSettings();
    const Analysis analysis = settings.Selected;
    const Core::Circuit circuit = m_Schematic.BuildCircuit();
    // The noise, the transfer function and the other analyses run once, whatever the step and Monte Carlo settings
    std::optional<StepPlan> steps;
    std::optional<Core::MonteCarloSettings> monte_carlo;
    const bool is_repeated = IsRepeatable(analysis);
    if (is_repeated && settings.MonteCarlo.Enabled) {
        ClearResult(analysis);
        if (const std::optional<std::string> problem = Core::CheckMonteCarlo(circuit, settings.MonteCarlo)) {
            Finish(analysis, problem, {});
            return;
        }
        monte_carlo = settings.MonteCarlo;
    } else if (is_repeated && settings.ParameterStep.Enabled) {
        ClearResult(analysis);
        const std::string unit = GetParameterUnit(circuit, settings.ParameterStep.Part);
        auto values = ListStepValues(settings.ParameterStep, unit);
        if (!values) {
            Finish(analysis, std::move(values.error()), {});
            return;
        }
        steps = StepPlan{.Settings = settings.ParameterStep, .Unit = unit, .Values = std::move(*values)};
    }
    StartAnalysis(analysis, circuit, steps, monte_carlo);
}

/**
 * @brief   Asks the run in progress to stop.
 * @note    A transient keeps the points made until the end of its current chunk. A live one only pauses, so
 *          ContinueLive() can take it on; stopping it once paused ends it. The other runs stop between their steps (the
 *          values of a stepped analysis, the combinations of a truth table) and drop their result, so a single
 *          simulation ends before the stop shows; IsStopping() is true until then. A Monte Carlo analysis keeps the
 * runs made.
 */
void AnalysisControls::StopRun() {
    if (m_IsLiveRun && !m_Service.IsPaused()) {
        m_Service.Pause();
        return;
    }
    m_Service.Stop();
}

/**
 * @brief   Tells what the Run button does now.
 * @return  Stop while a run goes on, Continue while a live run is paused and the transient is selected, and Run
 *          otherwise, which ends a paused live run before the selected analysis starts.
 */
RunAction AnalysisControls::GetRunAction() const {
    if (IsRunning()) {
        return RunAction::STOP;
    }
    if (IsLivePaused() && m_Schematic.GetSimulationSettings().Selected == Analysis::TRANSIENT) {
        return RunAction::CONTINUE;
    }
    return RunAction::RUN;
}

/**
 * @brief   Does what the Run button does now (see GetRunAction()).
 */
void AnalysisControls::PressRun() {
    switch (GetRunAction()) {
    case RunAction::STOP:
        StopRun();
        break;
    case RunAction::CONTINUE:
        ContinueLive();
        break;
    case RunAction::RUN:
    default:
        RunSelected();
        break;
    }
}

/**
 * @brief   Takes in the result of the run that ended, stops a run whose circuit was edited, hands a live run the parts
 *          the user changed and starts the run asked for while a paused live run held the worker. Call it every frame.
 */
void AnalysisControls::Update() {
    if (!m_RunningAnalysis) {
        return;
    }
    // An edit drops the results of the last run, so the answer of one that started before it would not fit
    if (m_Schematic.GetCircuitVersion() != m_RunCircuitVersion) {
        m_Service.Cancel();
    }
    if (m_IsLiveRun) {
        m_Service.SendChanges(m_Schematic.TakeLiveChanges());
    }
    m_Service.Poll();
    if (m_Service.IsBusy()) {
        return;
    }
    m_RunningAnalysis.reset();
    if (std::exchange(m_IsLiveRun, false)) {
        m_Schematic.SetLiveRun(false);
    }
    if (std::exchange(m_IsRunPending, false)) {
        RunSelected();
    }
}

/**
 * @brief   Tells whether a run is in progress.
 * @return  True from RunSelected() until its result arrives or the stop takes effect; false while a live run is
 *          paused, which holds as a stopped one does.
 */
bool AnalysisControls::IsRunning() const {
    return m_RunningAnalysis.has_value() && !IsLivePaused();
}

/**
 * @brief   Tells whether the run in progress was asked to stop and has not yet.
 * @return  True between StopRun() and the end of the run.
 */
bool AnalysisControls::IsStopping() const {
    return m_Service.IsCancelling();
}

/**
 * @brief   Tells whether a run still holds the simulation backend, so it must not be replaced yet.
 * @return  True while a run goes on, is stopping or is a live run on pause, whose worker keeps the backend it started
 *          on.
 */
bool AnalysisControls::IsBackendInUse() const {
    return m_RunningAnalysis.has_value() || m_Service.IsBusy();
}

/**
 * @brief   Returns the analysis in progress.
 * @return  The analysis, or no value when none runs.
 */
std::optional<Analysis> AnalysisControls::GetRunningAnalysis() const {
    return m_RunningAnalysis;
}

/**
 * @brief   Returns how far the run in progress has got.
 * @return  A fraction from 0 to 1; only the runs made of steps, such as a stepped analysis or a truth table, report
 *          it, and the others stay at 0 until they end.
 */
double AnalysisControls::GetRunProgress() const {
    return m_Service.GetProgress();
}

/**
 * @brief   Tells whether Live mode is on.
 * @return  True when the next transient runs live, if FindLiveObstacle() finds nothing in the way.
 */
bool AnalysisControls::IsLiveModeOn() const {
    return m_IsLiveModeOn;
}

/**
 * @brief   Turns Live mode on or off.
 * @param[in] is_on  True to run the next transients live.
 * @note    The run in progress goes on as it started.
 */
void AnalysisControls::SetLiveMode(const bool is_on) {
    m_IsLiveModeOn = is_on;
}

/**
 * @brief   Tells why Run would not start a live run now.
 * @return  The reason to show the user, or no value when the selected analysis is a transient that can run live.
 */
std::optional<std::string> AnalysisControls::FindLiveObstacle() const {
    const SimulationSettings &settings = m_Schematic.GetSimulationSettings();
    if (settings.Selected != Analysis::TRANSIENT) {
        return "Only a transient runs live; pick the transient to use Live";
    }
    if (settings.MonteCarlo.Enabled) {
        return "A Monte Carlo analysis runs once per draw, so it cannot run live; use a single run to use Live";
    }
    if (settings.ParameterStep.Enabled) {
        return "A stepped transient runs once per value, so it cannot run live; turn the stepping off to use Live";
    }
    return std::nullopt;
}

/**
 * @brief   Tells whether the run in progress is live.
 * @return  True from the start of a live transient until it ends or is discarded, paused or not.
 */
bool AnalysisControls::IsLive() const {
    return m_IsLiveRun;
}

/**
 * @brief   Tells whether a live run is paused.
 * @return  True from the StopRun() that paused it until ContinueLive(), DiscardLive() or its end.
 */
bool AnalysisControls::IsLivePaused() const {
    return m_IsLiveRun && m_Service.IsPaused();
}

/**
 * @brief   Takes a paused live run on from where it stopped, with the parts as the user left them.
 */
void AnalysisControls::ContinueLive() {
    if (IsLivePaused()) {
        m_Service.Resume();
    }
}

/**
 * @brief   Ends a live run, paused or not, and drops its points.
 */
void AnalysisControls::DiscardLive() {
    if (!m_IsLiveRun) {
        return;
    }
    m_Service.Cancel();
    m_IsLiveRun = false;
    m_IsLiveResult = false;
    m_Schematic.SetLiveRun(false);
    ClearResult(Analysis::TRANSIENT);
    m_Statuses[ToIndex(Analysis::TRANSIENT)] = {};
}

/**
 * @brief   Tells whether the transient on display came from a live run.
 * @return  True from the start of a live run until another transient starts or the live one is discarded, so its
 *          points are seen a stop time at a time.
 */
bool AnalysisControls::IsLiveResult() const {
    return m_IsLiveResult;
}

/**
 * @brief   Returns the analysis that ran last.
 * @return  The analysis, or no value before the first run.
 */
std::optional<Analysis> AnalysisControls::GetLastRun() const {
    return m_LastRun;
}

/**
 * @brief   Counts the runs that finished, so a caller can tell a new outcome from one it has already seen.
 * @return  The number of runs finished since the start, failed ones included.
 */
std::size_t AnalysisControls::GetFinishedRunCount() const {
    return m_FinishedRunCount;
}

/**
 * @brief   Tells how the last run of an analysis went.
 * @param[in] analysis  Analysis to check.
 * @return  Failed while its last run has an error; Stopped for a transient that ended early; with warnings when the
 *          result or ngspice reported some; None when it has no result, such as after the circuit changed.
 */
RunOutcome AnalysisControls::GetOutcome(const Analysis analysis) const {
    const RunStatus &status = m_Statuses[ToIndex(analysis)];
    if (status.Error) {
        return RunOutcome::FAILED;
    }
    if (!HasResult(analysis)) {
        return RunOutcome::NONE;
    }
    if (analysis == Analysis::TRANSIENT && m_Results.GetTransient() && !m_Results.GetTransient()->IsComplete) {
        return RunOutcome::STOPPED;
    }
    const bool ngspice_warned = std::ranges::any_of(
        status.Messages, [](const Core::SimulatorMessage &message) { return message.FromErrorStream; });
    return ngspice_warned || !GetResultWarnings(analysis).empty() ? RunOutcome::SUCCEEDED_WITH_WARNINGS
                                                                  : RunOutcome::SUCCEEDED;
}

/**
 * @brief   Returns why the last run of an analysis failed.
 * @param[in] analysis  Analysis to check.
 * @return  The error, or no value when the last run succeeded or there was none.
 */
const std::optional<std::string> &AnalysisControls::GetError(const Analysis analysis) const {
    return m_Statuses[ToIndex(analysis)].Error;
}

/**
 * @brief   How long the last run of an analysis took, from Run to its result.
 * @param[in] analysis  The analysis.
 * @return  The wall time in seconds, or none before a run, for a run refused before it started, and for a live run,
 *          which keeps pace with the clock.
 */
std::optional<double> AnalysisControls::GetRunSeconds(const Analysis analysis) const {
    return m_Statuses[ToIndex(analysis)].Seconds;
}

/**
 * @brief   Draws what the last run of an analysis reported: its error or its warnings, and the ngspice output.
 * @param[in] analysis  Analysis whose last run is shown.
 */
void AnalysisControls::DrawRunReport(const Analysis analysis) const {
    const RunStatus &status = m_Statuses[ToIndex(analysis)];
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetFontSize() * 30.0f);
    if (status.Error) {
        ImGui::TextColored(GetErrorTextColor(), "%s", status.Error->c_str());
    } else if (HasResult(analysis)) {
        ImGui::Text("%s done", GetAnalysisName(analysis));
        for (const std::string &warning : GetResultWarnings(analysis)) {
            ImGui::TextColored(GetWarningTextColor(), "%s", warning.c_str());
        }
        if (std::ranges::any_of(status.Messages,
                                [](const Core::SimulatorMessage &message) { return message.FromErrorStream; })) {
            ImGui::TextColored(GetWarningTextColor(), "ngspice reported warnings; check its output before trusting "
                                                      "the results");
        }
    } else {
        ImGui::TextDisabled("No results for the current circuit");
    }
    ImGui::PopTextWrapPos();
    if (status.Messages.empty() || !ImGui::CollapsingHeader("ngspice output")) {
        return;
    }
    for (const Core::SimulatorMessage &message : status.Messages) {
        if (message.FromErrorStream) {
            ImGui::TextColored(GetWarningTextColor(), "%s", message.Text.c_str());
        } else {
            ImGui::TextUnformatted(message.Text.c_str());
        }
    }
}

/**
 * @brief   Hands the job to the worker and remembers the circuit it started from.
 */
void AnalysisControls::StartRun(const Analysis analysis, Core::Job job) {
    m_RunCircuitVersion = m_Schematic.GetCircuitVersion();
    m_RunStart = std::chrono::steady_clock::now();
    m_RunningAnalysis = analysis;
    m_Service.Submit(std::move(job));
}

/**
 * @brief   The analysis picked, on the circuit built from the schematic, run once or once per step.
 */
void AnalysisControls::StartAnalysis(const Analysis analysis, const Core::Circuit &circuit,
                                     const std::optional<StepPlan> &steps,
                                     const std::optional<Core::MonteCarloSettings> &monte_carlo) {
    switch (analysis) {
    case Analysis::OPERATING_POINT:
        RunOperatingPointAnalysis(circuit, steps, monte_carlo);
        break;
    case Analysis::TRANSIENT:
        RunTransientAnalysis(circuit, steps, monte_carlo);
        break;
    case Analysis::AC_SWEEP:
        RunACSweepAnalysis(circuit, steps, monte_carlo);
        break;
    case Analysis::DC_SWEEP:
        RunDCSweepAnalysis(circuit, steps, monte_carlo);
        break;
    case Analysis::NOISE:
        RunNoiseAnalysis(circuit);
        break;
    case Analysis::TRANSFER_FUNCTION:
        RunTransferFunctionAnalysis(circuit);
        break;
    case Analysis::SENSITIVITY:
        RunSensitivityAnalysis(circuit);
        break;
    case Analysis::POLE_ZERO:
        RunPoleZeroAnalysis(circuit);
        break;
    case Analysis::S_PARAMETERS:
        RunSParameterAnalysis(circuit);
        break;
    case Analysis::TRUTH_TABLE:
        RunTruthTableAnalysis(circuit);
        break;
    case Analysis::HARMONIC_BALANCE:
        RunHarmonicBalanceAnalysis(circuit);
        break;
    default:
        break;
    }
}

void AnalysisControls::ClearResult(const Analysis analysis) {
    switch (analysis) {
    case Analysis::OPERATING_POINT:
        m_Results.SetOperatingPoint(std::nullopt);
        break;
    case Analysis::TRANSIENT:
        m_Results.SetTransient(std::nullopt);
        break;
    case Analysis::AC_SWEEP:
        m_Results.SetACSweep(std::nullopt);
        break;
    case Analysis::DC_SWEEP:
        m_Results.SetDCSweep(std::nullopt);
        break;
    case Analysis::NOISE:
        m_Results.SetNoise(std::nullopt);
        break;
    case Analysis::TRANSFER_FUNCTION:
        m_Results.SetTransferFunction(std::nullopt);
        break;
    case Analysis::SENSITIVITY:
        m_Results.SetSensitivity(std::nullopt);
        break;
    case Analysis::POLE_ZERO:
        m_Results.SetPoleZero(std::nullopt);
        break;
    case Analysis::S_PARAMETERS:
        m_Results.SetSParameters(std::nullopt);
        break;
    case Analysis::TRUTH_TABLE:
        m_Results.SetTruthTable(std::nullopt);
        break;
    case Analysis::HARMONIC_BALANCE:
        m_Results.SetHarmonicBalance(std::nullopt);
        break;
    default:
        break;
    }
}

void AnalysisControls::Finish(const Analysis analysis, std::optional<std::string> error,
                              std::vector<Core::SimulatorMessage> messages) {
    const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - m_RunStart;
    m_Statuses[ToIndex(analysis)] = {.Error = std::move(error),
                                     .Messages = std::move(messages),
                                     .Seconds = m_IsLiveRun ? std::nullopt : std::optional(elapsed.count())};
    m_LastRun = analysis;
    ++m_FinishedRunCount;
}

/**
 * @brief   Rating warnings, such as a transistor past its maximum voltage, which the operating point and the transient
 *          check, the combinations of a truth table that have no operating point, and the runs of a Monte Carlo
 *          analysis that failed or did not run.
 */
std::vector<std::string> AnalysisControls::GetResultWarnings(const Analysis analysis) const {
    if (const auto &monte_carlo = m_Results.GetMonteCarlo(); monte_carlo && IsRepeatable(analysis)) {
        std::vector<std::string> warnings = ListMonteCarloWarnings(*monte_carlo);
        if (analysis == Analysis::TRANSIENT && m_Results.GetTransient()) {
            std::ranges::copy(m_Results.GetTransient()->Warnings, std::back_inserter(warnings));
        }
        if (analysis == Analysis::OPERATING_POINT && m_Results.GetOperatingPoint()) {
            std::ranges::copy(m_Results.GetOperatingPoint()->Warnings, std::back_inserter(warnings));
        }
        return warnings;
    }
    return ListWarningsOfResult(m_Results, analysis);
}

bool AnalysisControls::HasResult(const Analysis analysis) const {
    switch (analysis) {
    case Analysis::OPERATING_POINT:
        return m_Results.GetOperatingPoint().has_value();
    case Analysis::TRANSIENT:
        return m_Results.GetTransient().has_value();
    case Analysis::AC_SWEEP:
        return m_Results.GetACSweep().has_value();
    case Analysis::DC_SWEEP:
        return m_Results.GetDCSweep().has_value();
    case Analysis::NOISE:
        return m_Results.GetNoise().has_value();
    case Analysis::TRANSFER_FUNCTION:
        return m_Results.GetTransferFunction().has_value();
    case Analysis::SENSITIVITY:
        return m_Results.GetSensitivity().has_value();
    case Analysis::POLE_ZERO:
        return m_Results.GetPoleZero().has_value();
    case Analysis::S_PARAMETERS:
        return m_Results.GetSParameters().has_value();
    case Analysis::TRUTH_TABLE:
        return m_Results.GetTruthTable().has_value();
    case Analysis::HARMONIC_BALANCE:
        return m_Results.GetHarmonicBalance().has_value();
    }
    return false;
}

} // namespace GUI
