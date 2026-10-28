/**
 * @file    analysis_controls.cpp
 * @brief   Runs of the analyses on the worker thread and what they leave in the schematic, shared by the editor
 *          toolbar and the Analysis Settings window.
 */

#include "analysis_controls.h"

#include "analysis_inputs.h"
#include "circuit_checks.h"
#include "circuit_copy.h"
#include "imgui.h"
#include "theme.h"
#include <algorithm>
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

// Hands a result to the schematic, or clears the old one so stale values never sit next to an error
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

// Runs once per value and stops at the first step that fails or when asked to; the output of each step is headed by
// its value, so a warning can be traced to the step that printed it
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

// A job that simulates its own copy of the circuit, so the user can go on editing, and hands the run it makes to the
// thread that polls, where store puts it away. A job that was stopped delivers nothing, unless what it has made is
// worth keeping, as the points of a transient
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

// Hands the steps to the schematic, or reports the step that failed; the old result is cleared before the steps run
template <typename Result, typename Store>
std::optional<std::string> StoreSteps(std::expected<StepFamily<Result>, std::string> family, Store store) {
    if (!family) {
        return std::move(family.error());
    }
    store(std::move(*family));
    return std::nullopt;
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
    // The operating point, the noise and the transfer function analyses run once, whatever the step settings
    std::optional<StepPlan> steps;
    if (settings.ParameterStep.Enabled &&
        (analysis == Analysis::TRANSIENT || analysis == Analysis::AC_SWEEP || analysis == Analysis::DC_SWEEP)) {
        ClearResult(analysis);
        const std::string unit = GetParameterUnit(circuit, settings.ParameterStep.Part);
        auto values = ListStepValues(settings.ParameterStep, unit);
        if (!values) {
            Finish(analysis, std::move(values.error()), {});
            return;
        }
        steps = StepPlan{.Settings = settings.ParameterStep, .Unit = unit, .Values = std::move(*values)};
    }
    StartAnalysis(analysis, circuit, steps);
}

/**
 * @brief   Asks the run in progress to stop.
 * @note    A transient keeps the points made until the end of its current chunk. A live one only pauses, so
 *          ContinueLive() can take it on; stopping it once paused ends it. The other runs stop between their steps (the
 *          values of a stepped analysis, the combinations of a truth table) and drop their result, so a single
 *          simulation ends before the stop shows; IsStopping() is true until then.
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

// Hands the job to the worker and remembers the circuit it started from
void AnalysisControls::StartRun(const Analysis analysis, Core::Job job) {
    m_RunCircuitVersion = m_Schematic.GetCircuitVersion();
    m_RunningAnalysis = analysis;
    m_Service.Submit(std::move(job));
}

// The analysis picked, on the circuit built from the schematic, run once or once per step
void AnalysisControls::StartAnalysis(const Analysis analysis, const Core::Circuit &circuit,
                                     const std::optional<StepPlan> &steps) {
    switch (analysis) {
    case Analysis::OPERATING_POINT:
        RunOperatingPointAnalysis(circuit);
        break;
    case Analysis::TRANSIENT:
        RunTransientAnalysis(circuit, steps);
        break;
    case Analysis::AC_SWEEP:
        RunACSweepAnalysis(circuit, steps);
        break;
    case Analysis::DC_SWEEP:
        RunDCSweepAnalysis(circuit, steps);
        break;
    case Analysis::NOISE:
        RunNoiseAnalysis(circuit);
        break;
    case Analysis::TRANSFER_FUNCTION:
        RunTransferFunctionAnalysis(circuit);
        break;
    case Analysis::TRUTH_TABLE:
        RunTruthTableAnalysis(circuit);
        break;
    default:
        break;
    }
}

void AnalysisControls::RunOperatingPointAnalysis(const Core::Circuit &circuit) {
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

// A live run is a transient that goes on to its limit at the pace the settings give, unless the user stops it first
void AnalysisControls::RunTransientAnalysis(const Core::Circuit &circuit, const std::optional<StepPlan> &steps) {
    const Core::TransientSettings settings = m_Schematic.GetSimulationSettings().Transient;
    m_IsLiveResult = false;
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
        run_settings.StopTime = std::min(pace * LIVE_RUN_LIMIT_SECONDS,
                                         settings.TimeStep * static_cast<double>(Core::MAX_TRANSIENT_POINTS - 1));
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

void AnalysisControls::RunACSweepAnalysis(const Core::Circuit &circuit, const std::optional<StepPlan> &steps) {
    const Core::ACSweepSettings settings = m_Schematic.GetSimulationSettings().ACSweep;
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

// A source that is unset, or that left the circuit, uses a default; a stepped parameter and a second stepped source
// would both make the family of curves, so only one may be on
void AnalysisControls::RunDCSweepAnalysis(const Core::Circuit &circuit, const std::optional<StepPlan> &steps) {
    const SimulationSettings &settings = m_Schematic.GetSimulationSettings();
    const std::vector<SweepSource> sources = ListSweepSources(circuit);
    if (sources.empty() || (steps && settings.StepSource)) {
        m_Schematic.SetDCSweep(std::nullopt);
        Finish(Analysis::DC_SWEEP,
               sources.empty() ? "Add a voltage source, current source or VCC to sweep"
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
                   ? "A transfer function analysis needs an input source; add a voltage source, current source or VCC"
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
    case Analysis::TRUTH_TABLE:
        m_Schematic.SetTruthTable(std::nullopt);
        break;
    }
}

void AnalysisControls::Finish(const Analysis analysis, std::optional<std::string> error,
                              std::vector<Core::SimulatorMessage> messages) {
    m_Statuses[ToIndex(analysis)] = {.Error = std::move(error), .Messages = std::move(messages)};
    m_LastRun = analysis;
}

// Rating warnings, such as a transistor past its maximum voltage, which the operating point and the transient check,
// and the combinations of a truth table that have no operating point
std::vector<std::string> AnalysisControls::GetResultWarnings(const Analysis analysis) const {
    if (analysis == Analysis::OPERATING_POINT && m_Schematic.GetOperatingPoint()) {
        return m_Schematic.GetOperatingPoint()->Warnings;
    }
    if (analysis == Analysis::TRANSIENT && m_Schematic.GetTransient()) {
        return m_Schematic.GetTransient()->Warnings;
    }
    if (analysis == Analysis::TRUTH_TABLE && m_Schematic.GetTruthTable()) {
        return m_Schematic.GetTruthTable()->Warnings;
    }
    return {};
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
    case Analysis::TRUTH_TABLE:
        return m_Schematic.GetTruthTable().has_value();
    }
    return false;
}

} // namespace GUI
