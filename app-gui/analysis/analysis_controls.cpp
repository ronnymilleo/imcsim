/**
 * @file    analysis_controls.cpp
 * @brief   Runs of the analyses on the worker thread and what they leave in the schematic, shared by the editor
 *          toolbar and the Analysis Settings window.
 */

#include "analysis/analysis_controls.h"

#include "analysis/analysis_inputs.h"
#include "drawing/theme.h"
#include "imgui.h"
#include "simulation/circuit_checks.h"
#include "simulation/circuit_copy.h"
#include "simulation/monte_carlo.h"
#include <algorithm>
#include <chrono>
#include <expected>
#include <format>
#include <iterator>
#include <memory>
#include <utility>

namespace GUI {

namespace {

std::size_t ToIndex(const Analysis analysis) {
    return static_cast<std::size_t>(analysis);
}

/**
 * @brief   Hands a result to the schematic, or clears the old one so stale values never sit next to an error.
 */
template <typename Result, typename Store>
std::optional<std::string> StoreResult(std::expected<Result, std::string> result, Store store) {
    if (result) {
        store(std::optional<Result>(std::move(*result)));
        return std::nullopt;
    }
    store(std::optional<Result>());
    return std::move(result.error());
}

/**
 * @struct  SteppedRuns
 * @brief   The results of an analysis run once per step, or why a step failed, and what ngspice printed in all.
 */
template <typename Result> struct SteppedRuns {
    std::expected<StepFamily<Result>, std::string> Family;
    std::vector<Core::SimulatorMessage> Messages;
};

/**
 * @brief   Runs once per value and stops at the first step that fails or when asked to; the output of each step is
 *          headed by its value, so a warning can be traced to the step that printed it.
 */
template <typename Result, typename RunStep>
SteppedRuns<Result> RunSteps(const ParameterStepSettings &step, const std::string &unit,
                             const std::vector<double> &values, Core::RunControl &control, const RunStep &run_step) {
    StepFamily<Result> family{.Parameter = GetParameterLabel(step), .Unit = unit, .Values = values, .Results = {}};
    std::vector<Core::SimulatorMessage> messages;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (control.IsCancelled()) {
            return {std::unexpected("Stopped"), std::move(messages)};
        }
        control.SetProgress(static_cast<double>(index) / static_cast<double>(values.size()));
        const std::string label = FormatStepValue(family.Parameter, values[index], unit);
        auto run = run_step(Core::ParameterValue{step.Part, values[index]});
        messages.push_back({std::format("Step {}", label), false});
        std::ranges::move(run.Messages, std::back_inserter(messages));
        if (!run.Result) {
            return {std::unexpected(std::format("At {}: {}", label, run.Result.error())), std::move(messages)};
        }
        family.Results.push_back(std::move(*run.Result));
    }
    return {std::move(family), std::move(messages)};
}

/// Runs whose traces are kept besides the nominal one; the rest only leave their measurements, since the traces of a
/// thousand transients would not fit in memory.
constexpr std::size_t MAX_KEPT_TRACES = 100;

/**
 * @struct  MonteCarloRuns
 * @brief   The traces kept from the runs of a Monte Carlo analysis, or why it could not run, what every run measured
 *          and what ngspice printed in all.
 */
template <typename Result> struct MonteCarloRuns {
    std::expected<StepFamily<Result>, std::string> Family;
    MonteCarloTable Table;
    std::vector<Core::SimulatorMessage> Messages;
};

MonteCarloTable MakeMonteCarloTable(const Core::Circuit &circuit, const Core::MonteCarloSettings &settings) {
    MonteCarloTable table{.RequestedRuns = static_cast<std::size_t>(settings.Runs)};
    for (const Core::Component *part : Core::FindVariedParts(circuit)) {
        table.Parts.push_back(part->GetName());
        table.Units.emplace_back(part->GetUnit());
    }
    return table;
}

/**
 * @brief   Runs the nominal circuit and then each draw, and goes on past a run that fails to converge, since a random
 *          draw may land on values the solver cannot handle; the nominal run has to work.
 * @details A stop keeps the runs made. Only the first runs keep their traces; every run keeps its measurements.
 */
template <typename Result, typename RunCircuit, typename Measure>
MonteCarloRuns<Result> RunMonteCarlo(const Core::MonteCarloSettings &settings, const Core::Circuit &circuit,
                                     Core::RunControl &control, const RunCircuit &run_circuit, const Measure &measure) {
    StepFamily<Result> family{.Parameter = MONTE_CARLO_PARAMETER, .Unit = "", .Values = {}, .Results = {}};
    MonteCarloTable table = MakeMonteCarloTable(circuit, settings);
    std::vector<Core::SimulatorMessage> messages;
    for (int run = 0; run <= settings.Runs && !control.IsCancelled(); ++run) {
        control.SetProgress(static_cast<double>(run) / static_cast<double>(settings.Runs + 1));
        const Core::MonteCarloCircuit built = Core::BuildMonteCarloCircuit(circuit, settings, run);
        auto outcome = run_circuit(built.Simulated);
        messages.push_back({run == 0 ? std::string("Nominal run") : std::format("Run {}", run), false});
        std::ranges::move(outcome.Messages, std::back_inserter(messages));
        MonteCarloRun entry{.Values = built.Values};
        if (!outcome.Result) {
            if (run == 0) {
                return {std::unexpected(std::format("The nominal run failed: {}", outcome.Result.error())),
                        {},
                        std::move(messages)};
            }
            entry.IsFailed = true;
            entry.Error = std::move(outcome.Result.error());
        } else {
            Measurement measured = measure(*outcome.Result);
            if (run == 0) {
                table.Columns = std::move(measured.Columns);
            }
            entry.Measures = std::move(measured.Values);
            if (family.Results.size() <= MAX_KEPT_TRACES) {
                family.Values.push_back(static_cast<double>(run));
                family.Results.push_back(std::move(*outcome.Result));
            }
        }
        table.Runs.push_back(std::move(entry));
    }
    if (table.Runs.empty()) {
        return {std::unexpected("Stopped"), {}, std::move(messages)};
    }
    return {std::move(family), std::move(table), std::move(messages)};
}

/**
 * @brief   Hands the traces and the table of the runs to the schematic, or reports why they did not run.
 */
template <typename Result, typename Store>
std::optional<std::string> StoreMonteCarlo(std::expected<StepFamily<Result>, std::string> family, MonteCarloTable table,
                                           Schematic &schematic, Store store) {
    if (!family) {
        return std::move(family.error());
    }
    store(std::move(*family));
    schematic.SetMonteCarlo(std::move(table));
    return std::nullopt;
}

/**
 * @brief   A job that simulates its own copy of the circuit, so the user can go on editing, and hands the run it makes
 *          to the thread that polls, where store puts it away.
 * @details A job that was stopped delivers nothing, unless what it has made is worth keeping, as the points of a
 *          transient.
 */
template <typename Run, bool KeepsPartial = false, typename Work, typename Store>
Core::Job MakeJob(const Core::Circuit &circuit, Work work, Store store) {
    const auto copy = std::make_shared<const Core::CircuitCopy>(circuit);
    return [copy, work = std::move(work), store = std::move(store)](Core::RunControl &control) -> Core::Delivery {
        auto run = std::make_shared<Run>(work(copy->GetCircuit(), control));
        if (control.IsCancelled() && !KeepsPartial) {
            return {};
        }
        return [run, store] { store(std::move(*run)); };
    };
}

/**
 * @brief   Hands the steps to the schematic, or reports the step that failed; the old result is cleared before the
 *          steps run.
 */
template <typename Result, typename Store>
std::optional<std::string> StoreSteps(std::expected<StepFamily<Result>, std::string> family, Store store) {
    if (!family) {
        return std::move(family.error());
    }
    store(std::move(*family));
    return std::nullopt;
}

/**
 * @brief   The warnings the stored result of one analysis carries, or none when it has no result or no warnings.
 */
std::vector<std::string> ListWarningsOfResult(const Schematic &schematic, const Analysis analysis) {
    if (analysis == Analysis::OPERATING_POINT && schematic.GetOperatingPoint()) {
        return schematic.GetOperatingPoint()->Warnings;
    }
    if (analysis == Analysis::TRANSIENT && schematic.GetTransient()) {
        return schematic.GetTransient()->Warnings;
    }
    if (analysis == Analysis::POLE_ZERO && schematic.GetPoleZero()) {
        return schematic.GetPoleZero()->Warnings;
    }
    if (analysis == Analysis::S_PARAMETERS && schematic.GetSParameters()) {
        return schematic.GetSParameters()->Warnings;
    }
    if (analysis == Analysis::TRUTH_TABLE && schematic.GetTruthTable()) {
        return schematic.GetTruthTable()->Warnings;
    }
    if (analysis == Analysis::HARMONIC_BALANCE && schematic.GetHarmonicBalance()) {
        return schematic.GetHarmonicBalance()->Warnings;
    }
    return {};
}

} // namespace

/**
 * @brief   Creates the controls for a schematic.
 * @param[in] schematic  Schematic whose analyses are set and run; it must outlive the controls.
 */
AnalysisControls::AnalysisControls(Schematic &schematic) : m_Schematic(schematic) {
}

/**
 * @brief   Starts the selected analysis with its settings on the worker thread.
 * @note    The result, or the error, goes to the schematic when the run ends, which Update() notices. Nothing happens
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
    if (analysis == Analysis::TRANSIENT && m_Schematic.GetTransient() && !m_Schematic.GetTransient()->IsComplete) {
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

/**
 * @brief   A stepped or Monte Carlo operating point keeps every step, and the schematic shows the selected one.
 */
void AnalysisControls::RunOperatingPointAnalysis(const Core::Circuit &circuit, const std::optional<StepPlan> &steps,
                                                 const std::optional<Core::MonteCarloSettings> &monte_carlo) {
    if (monte_carlo) {
        StartRun(Analysis::OPERATING_POINT,
                 MakeJob<MonteCarloRuns<Core::OperatingPoint>, true>(
                     circuit,
                     [monte_carlo = *monte_carlo](const Core::Circuit &copy, Core::RunControl &control) {
                         return RunMonteCarlo<Core::OperatingPoint>(
                             monte_carlo, copy, control,
                             [](const Core::Circuit &varied) { return Core::RunOperatingPoint(varied); },
                             MeasureOperatingPoint);
                     },
                     [this](MonteCarloRuns<Core::OperatingPoint> runs) {
                         Finish(Analysis::OPERATING_POINT,
                                StoreMonteCarlo(
                                    std::move(runs.Family), std::move(runs.Table), m_Schematic,
                                    [this](auto family) { m_Schematic.SetOperatingPointSteps(std::move(family)); }),
                                std::move(runs.Messages));
                     }));
        return;
    }
    if (steps) {
        StartRun(Analysis::OPERATING_POINT,
                 MakeJob<SteppedRuns<Core::OperatingPoint>>(
                     circuit,
                     [plan = *steps](const Core::Circuit &copy, Core::RunControl &control) {
                         return RunSteps<Core::OperatingPoint>(
                             plan.Settings, plan.Unit, plan.Values, control,
                             [&](const Core::ParameterValue &value) { return Core::RunOperatingPoint(copy, value); });
                     },
                     [this](SteppedRuns<Core::OperatingPoint> runs) {
                         Finish(
                             Analysis::OPERATING_POINT,
                             StoreSteps(std::move(runs.Family),
                                        [this](auto family) { m_Schematic.SetOperatingPointSteps(std::move(family)); }),
                             std::move(runs.Messages));
                     }));
        return;
    }
    StartRun(Analysis::OPERATING_POINT,
             MakeJob<Core::OperatingPointRun>(
                 circuit, [](const Core::Circuit &copy, Core::RunControl &) { return Core::RunOperatingPoint(copy); },
                 [this](Core::OperatingPointRun run) {
                     Finish(Analysis::OPERATING_POINT,
                            StoreResult(std::move(run.Result),
                                        [this](auto result) { m_Schematic.SetOperatingPoint(std::move(result)); }),
                            std::move(run.Messages));
                 }));
}

/**
 * @brief   A live run is a transient that goes on to its limit at the pace the settings give, unless the user stops it
 *          first.
 */
void AnalysisControls::RunTransientAnalysis(const Core::Circuit &circuit, const std::optional<StepPlan> &steps,
                                            const std::optional<Core::MonteCarloSettings> &monte_carlo) {
    const Core::TransientSettings settings = m_Schematic.GetSimulationSettings().Transient;
    m_IsLiveResult = false;
    if (monte_carlo) {
        StartRun(Analysis::TRANSIENT,
                 MakeJob<MonteCarloRuns<Core::Transient>, true>(
                     circuit,
                     [settings, monte_carlo = *monte_carlo](const Core::Circuit &copy, Core::RunControl &control) {
                         return RunMonteCarlo<Core::Transient>(
                             monte_carlo, copy, control,
                             [&](const Core::Circuit &varied) { return Core::RunTransient(varied, settings); },
                             MeasureTransient);
                     },
                     [this](MonteCarloRuns<Core::Transient> runs) {
                         Finish(
                             Analysis::TRANSIENT,
                             StoreMonteCarlo(std::move(runs.Family), std::move(runs.Table), m_Schematic,
                                             [this](auto family) { m_Schematic.SetTransientSteps(std::move(family)); }),
                             std::move(runs.Messages));
                     }));
        return;
    }
    if (steps) {
        StartRun(Analysis::TRANSIENT,
                 MakeJob<SteppedRuns<Core::Transient>>(
                     circuit,
                     [settings, plan = *steps](const Core::Circuit &copy, Core::RunControl &control) {
                         return RunSteps<Core::Transient>(plan.Settings, plan.Unit, plan.Values, control,
                                                          [&](const Core::ParameterValue &value) {
                                                              return Core::RunTransient(copy, settings, value);
                                                          });
                     },
                     [this](SteppedRuns<Core::Transient> runs) {
                         Finish(Analysis::TRANSIENT,
                                StoreSteps(std::move(runs.Family),
                                           [this](auto family) { m_Schematic.SetTransientSteps(std::move(family)); }),
                                std::move(runs.Messages));
                     }));
        return;
    }
    // The points arrive in chunks while the run goes on, so the old result gives way to the first one
    m_Schematic.SetTransient(std::nullopt);
    const std::size_t circuit_version = m_Schematic.GetCircuitVersion();
    m_IsLiveRun = m_IsLiveModeOn && !FindLiveObstacle();
    m_IsLiveResult = m_IsLiveRun;
    m_Schematic.SetLiveRun(m_IsLiveRun);
    Core::TransientSettings run_settings = settings;
    double pace = 0.0;
    if (m_IsLiveRun) {
        pace = settings.StopTime / m_Schematic.GetSimulationSettings().LiveSeconds;
        // One step short of the most a transient may span, which the check would refuse after rounding
        // A noise or random source steps the transient on its own, so the finest step counts for the points
        const double finest_step =
            std::min(settings.TimeStep, Core::FindFinestSignalStep(circuit).value_or(settings.TimeStep));
        run_settings.StopTime =
            std::min(pace * LIVE_RUN_LIMIT_SECONDS, finest_step * static_cast<double>(Core::MAX_TRANSIENT_POINTS - 1));
    }
    const bool is_live = m_IsLiveRun;
    StartRun(Analysis::TRANSIENT,
             MakeJob<Core::TransientRun, true>(
                 circuit,
                 [this, run_settings, circuit_version, pace](const Core::Circuit &copy, Core::RunControl &control) {
                     const auto post_chunk = [this, &control, circuit_version](Core::Transient chunk) {
                         control.Post(
                             [this, circuit_version, points = std::make_shared<Core::Transient>(std::move(chunk))] {
                                 if (m_Schematic.GetCircuitVersion() == circuit_version) {
                                     m_Schematic.AppendTransient(std::move(*points));
                                 }
                             });
                     };
                     return Core::RunTransient(copy, run_settings, std::nullopt,
                                               {.Control = &control, .OnChunk = post_chunk, .LivePace = pace});
                 },
                 [this, is_live](Core::TransientRun run) {
                     if (is_live && run.Result && run.Result->IsComplete) {
                         run.Result->Warnings.emplace_back(
                             "The live run reached its limit, an hour on the clock or a million time steps, whichever "
                             "comes first; Run starts a new one");
                     }
                     Finish(Analysis::TRANSIENT,
                            StoreResult(std::move(run.Result),
                                        [this](auto result) { m_Schematic.SetTransient(std::move(result)); }),
                            std::move(run.Messages));
                 }));
}

void AnalysisControls::RunACSweepAnalysis(const Core::Circuit &circuit, const std::optional<StepPlan> &steps,
                                          const std::optional<Core::MonteCarloSettings> &monte_carlo) {
    const Core::ACSweepSettings settings = m_Schematic.GetSimulationSettings().ACSweep;
    if (monte_carlo) {
        StartRun(Analysis::AC_SWEEP,
                 MakeJob<MonteCarloRuns<Core::ACSweep>, true>(
                     circuit,
                     [settings, monte_carlo = *monte_carlo](const Core::Circuit &copy, Core::RunControl &control) {
                         return RunMonteCarlo<Core::ACSweep>(
                             monte_carlo, copy, control,
                             [&](const Core::Circuit &varied) { return Core::RunACSweep(varied, settings); },
                             MeasureACSweep);
                     },
                     [this](MonteCarloRuns<Core::ACSweep> runs) {
                         Finish(
                             Analysis::AC_SWEEP,
                             StoreMonteCarlo(std::move(runs.Family), std::move(runs.Table), m_Schematic,
                                             [this](auto family) { m_Schematic.SetACSweepSteps(std::move(family)); }),
                             std::move(runs.Messages));
                     }));
        return;
    }
    if (steps) {
        StartRun(Analysis::AC_SWEEP,
                 MakeJob<SteppedRuns<Core::ACSweep>>(
                     circuit,
                     [settings, plan = *steps](const Core::Circuit &copy, Core::RunControl &control) {
                         return RunSteps<Core::ACSweep>(plan.Settings, plan.Unit, plan.Values, control,
                                                        [&](const Core::ParameterValue &value) {
                                                            return Core::RunACSweep(copy, settings, value);
                                                        });
                     },
                     [this](SteppedRuns<Core::ACSweep> runs) {
                         Finish(Analysis::AC_SWEEP,
                                StoreSteps(std::move(runs.Family),
                                           [this](auto family) { m_Schematic.SetACSweepSteps(std::move(family)); }),
                                std::move(runs.Messages));
                     }));
        return;
    }
    StartRun(Analysis::AC_SWEEP,
             MakeJob<Core::ACSweepRun>(
                 circuit,
                 [settings](const Core::Circuit &copy, Core::RunControl &) { return Core::RunACSweep(copy, settings); },
                 [this](Core::ACSweepRun run) {
                     Finish(Analysis::AC_SWEEP,
                            StoreResult(std::move(run.Result),
                                        [this](auto result) { m_Schematic.SetACSweep(std::move(result)); }),
                            std::move(run.Messages));
                 }));
}

/**
 * @brief   A source that is unset, or that left the circuit, uses a default; a stepped parameter and a second stepped
 *          source would both make the family of curves, so only one may be on.
 */
void AnalysisControls::RunDCSweepAnalysis(const Core::Circuit &circuit, const std::optional<StepPlan> &steps,
                                          const std::optional<Core::MonteCarloSettings> &monte_carlo) {
    const SimulationSettings &settings = m_Schematic.GetSimulationSettings();
    const std::vector<SweepSource> sources = ListSweepSources(circuit);
    if (sources.empty() || ((steps || monte_carlo) && settings.StepSource)) {
        m_Schematic.SetDCSweep(std::nullopt);
        Finish(Analysis::DC_SWEEP,
               sources.empty() ? std::format("Add {} to sweep", Core::SWEEPABLE_SOURCES)
               : monte_carlo   ? "Monte Carlo makes one curve per run, so turn off the second stepped source"
                               : "Step either a second source or a parameter, not both",
               {});
        return;
    }
    Core::DCSweepSettings run_settings{.Swept = settings.SweptRange};
    run_settings.Swept.Source = ResolveSweepSource(settings.SweptRange, sources, SWEPT_SOURCE_INDEX);
    if (settings.StepSource) {
        run_settings.Stepped = settings.SteppedRange;
        run_settings.Stepped->Source = ResolveSweepSource(settings.SteppedRange, sources, STEPPED_SOURCE_INDEX);
    }
    if (monte_carlo) {
        StartRun(Analysis::DC_SWEEP,
                 MakeJob<MonteCarloRuns<Core::DCSweep>, true>(
                     circuit,
                     [run_settings, monte_carlo = *monte_carlo](const Core::Circuit &copy, Core::RunControl &control) {
                         return RunMonteCarlo<Core::DCSweep>(
                             monte_carlo, copy, control,
                             [&](const Core::Circuit &varied) { return Core::RunDCSweep(varied, run_settings); },
                             MeasureDCSweep);
                     },
                     [this](MonteCarloRuns<Core::DCSweep> runs) {
                         Finish(Analysis::DC_SWEEP,
                                StoreMonteCarlo(std::move(runs.Family), std::move(runs.Table), m_Schematic,
                                                [this](auto family) {
                                                    m_Schematic.SetDCSweep(JoinDCSweepSteps(std::move(family)));
                                                }),
                                std::move(runs.Messages));
                     }));
        return;
    }
    if (steps) {
        StartRun(Analysis::DC_SWEEP,
                 MakeJob<SteppedRuns<Core::DCSweep>>(
                     circuit,
                     [run_settings, plan = *steps](const Core::Circuit &copy, Core::RunControl &control) {
                         return RunSteps<Core::DCSweep>(plan.Settings, plan.Unit, plan.Values, control,
                                                        [&](const Core::ParameterValue &value) {
                                                            return Core::RunDCSweep(copy, run_settings, value);
                                                        });
                     },
                     [this](SteppedRuns<Core::DCSweep> runs) {
                         Finish(Analysis::DC_SWEEP,
                                StoreSteps(std::move(runs.Family),
                                           [this](auto family) {
                                               m_Schematic.SetDCSweep(JoinDCSweepSteps(std::move(family)));
                                           }),
                                std::move(runs.Messages));
                     }));
        return;
    }
    StartRun(Analysis::DC_SWEEP,
             MakeJob<Core::DCSweepRun>(
                 circuit,
                 [run_settings](const Core::Circuit &copy, Core::RunControl &) {
                     return Core::RunDCSweep(copy, run_settings);
                 },
                 [this](Core::DCSweepRun run) {
                     Finish(Analysis::DC_SWEEP,
                            StoreResult(std::move(run.Result),
                                        [this](auto result) { m_Schematic.SetDCSweep(std::move(result)); }),
                            std::move(run.Messages));
                 }));
}

void AnalysisControls::RunNoiseAnalysis(const Core::Circuit &circuit) {
    const SimulationSettings &settings = m_Schematic.GetSimulationSettings();
    const std::vector<std::string> sources = Core::GetNoiseInputSources(circuit);
    const std::optional<int> output = ResolveOutputNode(settings.NoiseOutput, circuit, m_Schematic);
    if (sources.empty() || !output) {
        m_Schematic.SetNoise(std::nullopt);
        Finish(Analysis::NOISE,
               sources.empty() ? "A noise analysis needs an AC source as its input; select a voltage or current "
                                 "source and set it to AC"
                               : "Connect some parts: the noise needs a node to measure",
               {});
        return;
    }
    const Core::NoiseSettings run_settings{.OutputNode = *output,
                                           .InputSource = ResolveOptionalInput(settings.NoiseSource, sources),
                                           .StartFrequency = settings.NoiseRange.StartFrequency,
                                           .StopFrequency = settings.NoiseRange.StopFrequency,
                                           .PointsPerDecade = settings.NoiseRange.PointsPerDecade};
    StartRun(Analysis::NOISE,
             MakeJob<Core::NoiseRun>(
                 circuit,
                 [run_settings](const Core::Circuit &copy, Core::RunControl &) {
                     return Core::RunNoise(copy, run_settings);
                 },
                 [this](Core::NoiseRun run) {
                     Finish(Analysis::NOISE,
                            StoreResult(std::move(run.Result),
                                        [this](auto result) { m_Schematic.SetNoise(std::move(result)); }),
                            std::move(run.Messages));
                 }));
}

void AnalysisControls::RunTransferFunctionAnalysis(const Core::Circuit &circuit) {
    const SimulationSettings &settings = m_Schematic.GetSimulationSettings();
    const std::vector<std::string> sources = Core::GetTransferFunctionInputSources(circuit);
    const std::optional<int> output = ResolveOutputNode(settings.TransferFunctionOutput, circuit, m_Schematic);
    if (sources.empty() || !output) {
        m_Schematic.SetTransferFunction(std::nullopt);
        Finish(Analysis::TRANSFER_FUNCTION,
               sources.empty()
                   ? std::format("A transfer function analysis needs an input source; add {}", Core::SWEEPABLE_SOURCES)
                   : "Connect some parts: the transfer function needs a node to measure",
               {});
        return;
    }
    const Core::TransferFunctionSettings run_settings{
        .OutputNode = *output, .InputSource = ResolveOptionalInput(settings.TransferFunctionSource, sources)};
    StartRun(Analysis::TRANSFER_FUNCTION,
             MakeJob<Core::TransferFunctionRun>(
                 circuit,
                 [run_settings](const Core::Circuit &copy, Core::RunControl &) {
                     return Core::RunTransferFunction(copy, run_settings);
                 },
                 [this](Core::TransferFunctionRun run) {
                     Finish(Analysis::TRANSFER_FUNCTION,
                            StoreResult(std::move(run.Result),
                                        [this](auto result) { m_Schematic.SetTransferFunction(std::move(result)); }),
                            std::move(run.Messages));
                 }));
}

void AnalysisControls::RunSensitivityAnalysis(const Core::Circuit &circuit) {
    const SimulationSettings &settings = m_Schematic.GetSimulationSettings();
    const std::optional<int> output = ResolveOutputNode(settings.SensitivityOutput, circuit, m_Schematic);
    if (!output) {
        m_Schematic.SetSensitivity(std::nullopt);
        Finish(Analysis::SENSITIVITY, "Connect some parts: the sensitivity needs a node to measure", {});
        return;
    }
    const Core::SensitivitySettings run_settings{.OutputNode = *output};
    StartRun(Analysis::SENSITIVITY,
             MakeJob<Core::SensitivityRun>(
                 circuit,
                 [run_settings](const Core::Circuit &copy, Core::RunControl &) {
                     return Core::RunSensitivity(copy, run_settings);
                 },
                 [this](Core::SensitivityRun run) {
                     Finish(Analysis::SENSITIVITY,
                            StoreResult(std::move(run.Result),
                                        [this](auto result) { m_Schematic.SetSensitivity(std::move(result)); }),
                            std::move(run.Messages));
                 }));
}

void AnalysisControls::RunPoleZeroAnalysis(const Core::Circuit &circuit) {
    const SimulationSettings &settings = m_Schematic.GetSimulationSettings();
    const std::vector<std::string> sources = Core::GetTransferFunctionInputSources(circuit);
    const std::optional<int> output = ResolveOutputNode(settings.PoleZeroOutput, circuit, m_Schematic);
    if (sources.empty() || !output) {
        m_Schematic.SetPoleZero(std::nullopt);
        Finish(Analysis::POLE_ZERO,
               sources.empty()
                   ? std::format("A pole-zero analysis needs an input source; add {}", Core::SWEEPABLE_SOURCES)
                   : "Connect some parts: the pole-zero analysis needs a node to measure",
               {});
        return;
    }
    const Core::PoleZeroSettings run_settings{.OutputNode = *output,
                                              .InputSource = ResolveOptionalInput(settings.PoleZeroSource, sources)};
    StartRun(Analysis::POLE_ZERO,
             MakeJob<Core::PoleZeroRun>(
                 circuit,
                 [run_settings](const Core::Circuit &copy, Core::RunControl &) {
                     return Core::RunPoleZero(copy, run_settings);
                 },
                 [this](Core::PoleZeroRun run) {
                     Finish(Analysis::POLE_ZERO,
                            StoreResult(std::move(run.Result),
                                        [this](auto result) { m_Schematic.SetPoleZero(std::move(result)); }),
                            std::move(run.Messages));
                 }));
}

void AnalysisControls::RunSParameterAnalysis(const Core::Circuit &circuit) {
    const Core::SParameterSettings run_settings = m_Schematic.GetSimulationSettings().SParameters;
    StartRun(Analysis::S_PARAMETERS,
             MakeJob<Core::SParametersRun>(
                 circuit,
                 [run_settings](const Core::Circuit &copy, Core::RunControl &) {
                     return Core::RunSParameters(copy, run_settings);
                 },
                 [this](Core::SParametersRun run) {
                     Finish(Analysis::S_PARAMETERS,
                            StoreResult(std::move(run.Result),
                                        [this](auto result) { m_Schematic.SetSParameters(std::move(result)); }),
                            std::move(run.Messages));
                 }));
}

void AnalysisControls::RunTruthTableAnalysis(const Core::Circuit &circuit) {
    const std::vector<std::string> available = Core::GetTruthTableInputs(circuit);
    const std::vector<int> outputs = ListMeasuredNodes(circuit, m_Schematic);
    if (available.empty() || outputs.empty()) {
        m_Schematic.SetTruthTable(std::nullopt);
        Finish(Analysis::TRUTH_TABLE,
               available.empty() ? "A truth table needs a logic input to vary; add a Logic input"
                                 : "Measure a voltage with the Probe tool: the truth table reads the measured nodes",
               {});
        return;
    }
    const Core::TruthTableSettings run_settings{
        .Inputs = ResolveTruthTableInputs(m_Schematic.GetSimulationSettings().TruthTableInputs, available),
        .OutputNodes = outputs};
    StartRun(Analysis::TRUTH_TABLE,
             MakeJob<Core::TruthTableRun>(
                 circuit,
                 [run_settings](const Core::Circuit &copy, Core::RunControl &control) {
                     return Core::RunTruthTable(copy, run_settings, &control);
                 },
                 [this](Core::TruthTableRun run) {
                     Finish(Analysis::TRUTH_TABLE,
                            StoreResult(std::move(run.Result),
                                        [this](auto result) { m_Schematic.SetTruthTable(std::move(result)); }),
                            std::move(run.Messages));
                 }));
}

void AnalysisControls::RunHarmonicBalanceAnalysis(const Core::Circuit &circuit) {
    const Core::HarmonicBalanceSettings run_settings = m_Schematic.GetSimulationSettings().HarmonicBalance;
    StartRun(Analysis::HARMONIC_BALANCE,
             MakeJob<Core::HarmonicBalanceRun>(
                 circuit,
                 [run_settings](const Core::Circuit &copy, Core::RunControl &) {
                     return Core::RunHarmonicBalance(copy, run_settings);
                 },
                 [this](Core::HarmonicBalanceRun run) {
                     Finish(Analysis::HARMONIC_BALANCE,
                            StoreResult(std::move(run.Result),
                                        [this](auto result) { m_Schematic.SetHarmonicBalance(std::move(result)); }),
                            std::move(run.Messages));
                 }));
}

void AnalysisControls::ClearResult(const Analysis analysis) {
    switch (analysis) {
    case Analysis::OPERATING_POINT:
        m_Schematic.SetOperatingPoint(std::nullopt);
        break;
    case Analysis::TRANSIENT:
        m_Schematic.SetTransient(std::nullopt);
        break;
    case Analysis::AC_SWEEP:
        m_Schematic.SetACSweep(std::nullopt);
        break;
    case Analysis::DC_SWEEP:
        m_Schematic.SetDCSweep(std::nullopt);
        break;
    case Analysis::NOISE:
        m_Schematic.SetNoise(std::nullopt);
        break;
    case Analysis::TRANSFER_FUNCTION:
        m_Schematic.SetTransferFunction(std::nullopt);
        break;
    case Analysis::SENSITIVITY:
        m_Schematic.SetSensitivity(std::nullopt);
        break;
    case Analysis::POLE_ZERO:
        m_Schematic.SetPoleZero(std::nullopt);
        break;
    case Analysis::S_PARAMETERS:
        m_Schematic.SetSParameters(std::nullopt);
        break;
    case Analysis::TRUTH_TABLE:
        m_Schematic.SetTruthTable(std::nullopt);
        break;
    case Analysis::HARMONIC_BALANCE:
        m_Schematic.SetHarmonicBalance(std::nullopt);
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
    if (const auto &monte_carlo = m_Schematic.GetMonteCarlo(); monte_carlo && IsRepeatable(analysis)) {
        std::vector<std::string> warnings = ListMonteCarloWarnings(*monte_carlo);
        if (analysis == Analysis::TRANSIENT && m_Schematic.GetTransient()) {
            std::ranges::copy(m_Schematic.GetTransient()->Warnings, std::back_inserter(warnings));
        }
        if (analysis == Analysis::OPERATING_POINT && m_Schematic.GetOperatingPoint()) {
            std::ranges::copy(m_Schematic.GetOperatingPoint()->Warnings, std::back_inserter(warnings));
        }
        return warnings;
    }
    return ListWarningsOfResult(m_Schematic, analysis);
}

bool AnalysisControls::HasResult(const Analysis analysis) const {
    switch (analysis) {
    case Analysis::OPERATING_POINT:
        return m_Schematic.GetOperatingPoint().has_value();
    case Analysis::TRANSIENT:
        return m_Schematic.GetTransient().has_value();
    case Analysis::AC_SWEEP:
        return m_Schematic.GetACSweep().has_value();
    case Analysis::DC_SWEEP:
        return m_Schematic.GetDCSweep().has_value();
    case Analysis::NOISE:
        return m_Schematic.GetNoise().has_value();
    case Analysis::TRANSFER_FUNCTION:
        return m_Schematic.GetTransferFunction().has_value();
    case Analysis::SENSITIVITY:
        return m_Schematic.GetSensitivity().has_value();
    case Analysis::POLE_ZERO:
        return m_Schematic.GetPoleZero().has_value();
    case Analysis::S_PARAMETERS:
        return m_Schematic.GetSParameters().has_value();
    case Analysis::TRUTH_TABLE:
        return m_Schematic.GetTruthTable().has_value();
    case Analysis::HARMONIC_BALANCE:
        return m_Schematic.GetHarmonicBalance().has_value();
    }
    return false;
}

} // namespace GUI
