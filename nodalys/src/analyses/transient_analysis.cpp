/**
 * @file    transient_analysis.cpp
 * @brief   Nonlinear time-domain transient analysis solver with adaptive stepping.
 */

#include "analyses/analyses.h"

#include "analyses/analysis_preamble.h"
#include "analyses/solution_readout.h"
#include "devices/devices.h"
#include "devices/transient_noise.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <limits>
#include <optional>
#include <random>
#include <thread>
#include <utility>

namespace Nodalys {

namespace {

constexpr std::chrono::duration<double> CHUNK_INTERVAL{0.03};
constexpr std::size_t CHUNK_POINT_LIMIT = 4096;
constexpr double LTE_REJECT_FACTOR = 2.0;
constexpr double LTE_SAFETY = 0.9;
constexpr double STEP_GROW_LIMIT = 3.0;
constexpr double STEP_SHRINK_LIMIT = 0.3;
constexpr double MIN_STEP = 1e-15;
/// Share of the way to the following corner that the step leaving a corner covers, as ngspice does.
constexpr double CORNER_STEP_SHARE = 0.1;
/// Factor that a step shrinks by when its solve does not converge.
constexpr double RETRY_STEP_SHRINK = 0.1;
/// Error ratio above which a rejected step is retried with Backward Euler, which does not ring.
constexpr double RESTART_ERROR_RATIO = 4.0;
/// Lowest error ratio that the growth of the step is based on, so a step that made no error cannot grow without bound.
constexpr double MIN_ERROR_RATIO = 1e-3;
/// Exponent that turns an error ratio into a step ratio; the error of the integration methods goes with the cube of the
/// step.
constexpr double STEP_ERROR_EXPONENT = -1.0 / 3.0;
/// How many times the voltage tolerance the local truncation error may be.
constexpr double TRUNCATION_TOLERANCE_FACTOR = 7.0;
/// Relative margin within which a time counts as the stop time or as an output point.
constexpr double TIME_TOLERANCE = 1e-12;
/// Relative margin within which the last output point counts as the stop time.
constexpr double STOP_POINT_TOLERANCE = 1e-9;

/**
 * @struct  SolutionPoint
 * @brief   The solution of the circuit at one instant, with the currents read from it.
 */
struct SolutionPoint {
    double Time;
    const std::vector<double> &X;
    const std::vector<double> &Currents;
};

class TransientCollector {
public:
    TransientCollector(const CompiledCircuit &compiled, const double output_step)
        : m_Compiled(compiled), m_OutputStep(output_step) {
        m_Result.NodeVoltages.resize(static_cast<std::size_t>(compiled.CircuitNodeCount));
        for (const CurrentRef &current : compiled.Currents) {
            m_Result.Currents.push_back({current.Name, {}});
        }
    }

    void Append(const double time, const std::vector<double> &x, const std::vector<double> &currents) {
        m_Result.Times.push_back(time);
        for (int node = 0; node < m_Compiled.CircuitNodeCount; ++node) {
            const double voltage = node == 0 ? 0.0 : x[static_cast<std::size_t>(m_Compiled.System->NodeIndex(node))];
            m_Result.NodeVoltages[static_cast<std::size_t>(node)].push_back(voltage);
        }
        for (std::size_t index = 0; index < currents.size(); ++index) {
            m_Result.Currents[index].Values.push_back(currents[index]);
        }
    }

    void AppendSegment(const SolutionPoint &start, const SolutionPoint &end) {
        if (m_OutputStep <= 0.0) {
            Append(end.Time, end.X, end.Currents);
            return;
        }
        const double segment_duration = end.Time - start.Time;
        if (segment_duration <= 0.0) {
            return;
        }

        m_Scratch.resize(start.X.size());
        m_CurrentsScratch.resize(start.Currents.size());

        while (true) {
            const double target_time = static_cast<double>(m_NextGrid) * m_OutputStep;
            if (target_time > end.Time + TIME_TOLERANCE * m_OutputStep) {
                break;
            }
            const double alpha = std::clamp((target_time - start.Time) / segment_duration, 0.0, 1.0);
            for (std::size_t i = 0; i < m_Scratch.size(); ++i) {
                m_Scratch[i] = start.X[i] + alpha * (end.X[i] - start.X[i]);
            }
            for (std::size_t i = 0; i < m_CurrentsScratch.size(); ++i) {
                m_CurrentsScratch[i] = start.Currents[i] + alpha * (end.Currents[i] - start.Currents[i]);
            }
            Append(target_time, m_Scratch, m_CurrentsScratch);
            ++m_NextGrid;
        }
    }

    /**
     * @brief   Keeps the point solved on a corner of a source, which the output grid would otherwise smooth away.
     */
    void AppendCorner(const double time, const std::vector<double> &x, const std::vector<double> &currents) {
        const std::vector<double> &times = m_Result.Times;
        if (!times.empty()
            && std::fabs(times.back() - time) <= TIME_TOLERANCE * std::max(std::fabs(time), m_OutputStep)) {
            return;
        }
        Append(time, x, currents);
    }

    std::size_t PointCount() const { return m_Result.Times.size(); }

    const TransientResult &View() const { return m_Result; }

    TransientResult Take() { return std::move(m_Result); }

private:
    const CompiledCircuit &m_Compiled;
    double m_OutputStep;
    long m_NextGrid = 1;
    TransientResult m_Result;
    std::vector<double> m_Scratch;
    std::vector<double> m_CurrentsScratch;
};

/**
 * @struct  StepState
 * @brief   Where the time stepping stands: the time, the length of the steps, and the solutions of the last three
 *          points, which the error of the next step is estimated from.
 */
struct StepState {
    double Time = 0.0;
    double Step = 0.0;
    double PreviousStep = 0.0;
    double OlderStep = 0.0;
    /// Backward Euler starts the run and leaves every corner, where the trapezoidal rule would ring.
    bool IsRestart = true;
    double NextCorner = std::numeric_limits<double>::infinity();
    std::vector<double> Now;
    std::vector<double> Old;
    std::vector<double> Older;
    std::vector<double> CurrentsNow;
};

/**
 * @struct  ChunkPublisher
 * @brief   When the points solved so far were last handed to the monitor, and how many of them it has.
 */
struct ChunkPublisher {
    const RunMonitor &Monitor;
    std::chrono::steady_clock::time_point LastSend;
    double ChunkEnd = 0.0;
    std::size_t Sent = 0;
};

bool ApplyLiveChange(Device &device, const LiveChange &change) {
    switch (change.Quantity) {
    case LiveQuantity::RESISTANCE:
        if (auto *resistor = dynamic_cast<ResistorDevice *>(&device)) {
            resistor->SetResistance(change.Value);
            return true;
        }
        return false;
    case LiveQuantity::DC_VOLTAGE:
        if (auto *source = dynamic_cast<VoltageSourceDevice *>(&device)) {
            source->SetDcValue(change.Value);
            return true;
        }
        return false;
    default:
        return false;
    }
}

void ApplyLiveChanges(CompiledCircuit &compiled, const RunMonitor &monitor, std::vector<DiagnosticMessage> *messages) {
    if (!monitor.TakeLiveChanges) {
        return;
    }
    for (const LiveChange &change : monitor.TakeLiveChanges()) {
        const auto found = compiled.ElementsByName.find(change.Element);
        if (found == compiled.ElementsByName.end()) {
            continue;
        }
        if (!ApplyLiveChange(*found->second, change) && messages) {
            messages->push_back({std::format("nodalys: cannot apply live change to {}", change.Element), true});
        }
    }
}

/**
 * @brief   Finds the next corner of any source waveform after an instant, so the step never jumps over an edge.
 */
double FindNextCorner(const CompiledCircuit &compiled, const double time) {
    double corner = std::numeric_limits<double>::infinity();
    for (const std::unique_ptr<Device> &device : compiled.Devices) {
        corner = std::min(corner, device->NextBreakpoint(time));
    }
    return corner;
}

/**
 * @brief   Scales the third divided difference of a point into the local truncation error of the step that made it.
 * @details The trapezoidal rule errs by h^3 y'''/12, and the third divided difference is y'''/6, so the factor is
 *          h^3 / 2. Variable-step BDF-2 errs by the derivative error of its parabola, h (h + h_p) y'''/6, divided by
 *          its leading coefficient a0 = (1 + 2w) / ((1 + w) h), which is h^3 (1 + w)^2 / (w (1 + 2w)) times the
 *          divided difference, with w = h / h_p; at a constant step that is the known 2/9 h^3 y'''.
 * @param[in] method         Method of the step.
 * @param[in] step           Length h of the step.
 * @param[in] previous_step  Length h_p of the step before it.
 * @return  The factor that multiplies the absolute value of the third divided difference.
 */
double TruncationErrorFactor(const IntegrationMethod method, const double step, const double previous_step) {
    const double cube = step * step * step;
    if (method == IntegrationMethod::GEAR2) {
        const double ratio = step / previous_step;
        return cube * (1.0 + ratio) * (1.0 + ratio) / (ratio * (1.0 + 2.0 * ratio));
    }
    return 0.5 * cube;
}

/**
 * @brief   Estimates the local truncation error of a step as a share of what the tolerances allow.
 * @param[in] compiled  Compiled circuit, which tells the nodes that carry a dynamic state.
 * @param[in] state     The stepping, with the points before the step.
 * @param[in] x_new     The solution at the end of the step.
 * @param[in] method    Integration method of the step.
 * @param[in] options   Tolerances of the solve.
 * @return  The largest share over the dynamic nodes; above 1 the step errs more than allowed. Zero while there are
 *          not three steps behind to take the difference from.
 */
double EstimateTruncationRatio(const CompiledCircuit &compiled,
                               const StepState &state,
                               const std::vector<double> &x_new,
                               const IntegrationMethod method,
                               const ConvergenceOptions &options) {
    double ratio = 0.0;
    if (state.PreviousStep <= 0.0 || state.OlderStep <= 0.0) {
        return ratio;
    }
    const int size = compiled.System->GetSize();
    for (int index = 0; index < size; ++index) {
        if (!compiled.DynamicNode[static_cast<std::size_t>(index)]) {
            continue;
        }
        const std::size_t i = static_cast<std::size_t>(index);
        const double d1 = (x_new[i] - state.Now[i]) / state.Step;
        const double d2 = (state.Now[i] - state.Old[i]) / state.PreviousStep;
        const double d3 = (state.Old[i] - state.Older[i]) / state.OlderStep;
        const double d12 = (d1 - d2) / (state.Step + state.PreviousStep);
        const double d23 = (d2 - d3) / (state.PreviousStep + state.OlderStep);
        const double d123 = (d12 - d23) / (state.Step + state.PreviousStep + state.OlderStep);
        const double error = TruncationErrorFactor(method, state.Step, state.PreviousStep) * std::fabs(d123);
        const double scale_v = std::max({std::fabs(x_new[i]), std::fabs(state.Now[i]), std::fabs(state.Old[i])});
        const double tolerance = TRUNCATION_TOLERANCE_FACTOR * (options.RelTol * scale_v + options.VoltageAbsTol);
        ratio = std::max(ratio, error / tolerance);
    }
    return ratio;
}

void SeedDeviceRandomness(CompiledCircuit &compiled, const TransientConfig &settings) {
    const std::uint64_t base_seed =
        settings.Seed != 0 ? static_cast<std::uint64_t>(settings.Seed) : std::random_device{}();
    for (std::size_t index = 0; index < compiled.Devices.size(); ++index) {
        compiled.Devices[index]->SetRandomSeed(MixTransientSeed(base_seed, index));
    }
}

/**
 * @brief   Solves the point the transient starts from: the initial conditions when the circuit has them, the
 *          operating point otherwise.
 * @return  The solution at time zero, or why none was found.
 */
std::expected<std::vector<double>, std::string> SolveStart(CompiledCircuit &compiled,
                                                           const bool has_initial_conditions,
                                                           const double output_step,
                                                           const ConvergenceOptions &options,
                                                           std::vector<DiagnosticMessage> *messages) {
    const TimeContext initial = has_initial_conditions ? TimeContext{.IsTransient = true,
                                                                     .UseInitialConditions = true,
                                                                     .IsTransientStart = true,
                                                                     .Time = 0.0,
                                                                     .TimeStep = output_step}
                                                       : TimeContext{.IsTransientStart = true};
    std::vector<double> x(static_cast<std::size_t>(compiled.System->GetSize()), 0.0);
    const NewtonOutcome outcome = SolveNewton(*compiled.System, compiled.Devices, x, initial, options);
    if (!outcome.Converged) {
        return std::unexpected(DescribeFailure(has_initial_conditions
                                                   ? "Nodalys could not start the transient from the initial conditions"
                                                   : "Nodalys found no operating point to start the transient from",
                                               outcome));
    }
    DescribeOutcome(outcome, has_initial_conditions ? "initial point" : "operating point", messages);
    return x;
}

void AcceptDeviceStep(CompiledCircuit &compiled, const std::vector<double> &x) {
    for (const std::unique_ptr<Device> &device : compiled.Devices) {
        device->AcceptStep(x);
    }
}

void RejectDeviceStep(CompiledCircuit &compiled) {
    for (const std::unique_ptr<Device> &device : compiled.Devices) {
        device->RejectStep();
    }
}

/**
 * @brief   Plans the next step so that it ends on the stop time, or on the corner it would otherwise jump over.
 * @return  True when the step ends on the corner.
 */
bool PlanStep(StepState &state, const double stop_time) {
    state.Step = std::min(state.Step, stop_time - state.Time);
    const bool reaches_corner = state.Time + state.Step >= state.NextCorner;
    if (reaches_corner) {
        state.Step = state.NextCorner - state.Time;
    }
    return reaches_corner;
}

NewtonOutcome SolveStep(CompiledCircuit &compiled,
                        const StepState &state,
                        const IntegrationMethod method,
                        const ConvergenceOptions &options,
                        std::vector<double> &x_new) {
    const TimeContext context{.IsTransient = true,
                              .UseInitialConditions = false,
                              .BackwardEuler = state.IsRestart,
                              .Method = method,
                              .PreviousTimeStep = state.PreviousStep,
                              .Time = state.Time + state.Step,
                              .TimeStep = state.Step};
    return SolveNewton(*compiled.System, compiled.Devices, x_new, context, options);
}

/**
 * @brief   Discards a step whose solve did not converge and shortens the next try.
 * @return  The failure when the step has collapsed, or nothing while there is room to try again.
 */
std::optional<std::string>
RetryAfterNonConvergence(CompiledCircuit &compiled, StepState &state, const NewtonOutcome &outcome) {
    RejectDeviceStep(compiled);
    state.Step *= RETRY_STEP_SHRINK;
    if (state.Step < MIN_STEP) {
        return DescribeFailure(
            std::format("Nodalys could not converge at t = {}; the time step collapsed", state.Time + state.Step),
            outcome);
    }
    return std::nullopt;
}

/**
 * @brief   Discards a step whose error is above the tolerances and shortens the next try by the error.
 * @return  The failure when the step has collapsed, or nothing while there is room to try again.
 */
std::optional<std::string> RetryAfterRejection(CompiledCircuit &compiled, StepState &state, const double ratio) {
    RejectDeviceStep(compiled);
    if (ratio > RESTART_ERROR_RATIO) {
        // Sharp corners (such as a clamp diode turning on or off) excite trapezoidal ringing; retry with
        // Backward Euler without discarding the step history
        state.IsRestart = true;
    }
    state.Step =
        std::max(state.Step * std::max(LTE_SAFETY * std::pow(ratio, STEP_ERROR_EXPONENT), STEP_SHRINK_LIMIT), MIN_STEP);
    if (state.Step <= MIN_STEP) {
        return std::format("Nodalys kept rejecting the time step at t = {}; the step collapsed", state.Time);
    }
    return std::nullopt;
}

/**
 * @brief   Accepts a step: hands it to the devices and the collector, moves the history on and chooses the next step.
 * @param[in,out] compiled        Compiled circuit, whose devices take the step.
 * @param[in,out] state           The stepping, which moves to the end of the step.
 * @param[in,out] collector       Receives the points of the step.
 * @param[in]     x_new           The solution at the end of the step; the history takes it.
 * @param[in]     ratio           Error ratio of the step.
 * @param[in]     reaches_corner  Whether the step ends on a corner of a source.
 * @param[in]     output_step     Largest step, which is the spacing of the output points.
 */
void AcceptStep(CompiledCircuit &compiled,
                StepState &state,
                TransientCollector &collector,
                const std::vector<double> &x_new,
                const double ratio,
                const bool reaches_corner,
                const double output_step) {
    const bool has_switched = std::ranges::any_of(compiled.Devices, [](const std::unique_ptr<Device> &device) {
        return device->HasSwitched();
    });
    AcceptDeviceStep(compiled, x_new);
    std::vector<double> currents_new = ReadCurrentValues(compiled, x_new);
    collector.AppendSegment({.Time = state.Time, .X = state.Now, .Currents = state.CurrentsNow},
                            {.Time = state.Time + state.Step, .X = x_new, .Currents = currents_new});
    if (reaches_corner) {
        collector.AppendCorner(state.NextCorner, x_new, currents_new);
    }
    state.CurrentsNow = std::move(currents_new);
    state.Older = std::exchange(state.Old, std::exchange(state.Now, x_new));
    state.Time = reaches_corner ? state.NextCorner : state.Time + state.Step;
    state.OlderStep = std::exchange(state.PreviousStep, state.Step);
    state.IsRestart = false;
    const double next = state.Step
                      * std::clamp(LTE_SAFETY * std::pow(std::max(ratio, MIN_ERROR_RATIO), STEP_ERROR_EXPONENT),
                                   STEP_SHRINK_LIMIT,
                                   STEP_GROW_LIMIT);
    state.Step = std::clamp(next, MIN_STEP, output_step);
    // A device whose state decides its corners, such as a fuse about to blow, moves its next one at every point
    state.NextCorner = FindNextCorner(compiled, state.Time);
    if (reaches_corner || has_switched) {
        // The slope changes at a corner, and at a switch that flipped inside the step, so the points before it
        // say nothing about the error after it
        state.IsRestart = true;
        state.PreviousStep = 0.0;
        state.OlderStep = 0.0;
        state.Old = state.Now;
        state.Older = state.Now;
        if (std::isfinite(state.NextCorner)) {
            state.Step = std::max(std::min(state.Step, CORNER_STEP_SHARE * (state.NextCorner - state.Time)), MIN_STEP);
        }
    }
}

/**
 * @brief   Copies the points of a result from one on, which are the ones the monitor has not seen yet.
 */
TransientResult SliceFrom(const TransientResult &result, const std::size_t first) {
    TransientResult chunk;
    chunk.Times.assign(result.Times.begin() + static_cast<std::ptrdiff_t>(first), result.Times.end());
    chunk.NodeVoltages.resize(result.NodeVoltages.size());
    for (std::size_t node = 0; node < result.NodeVoltages.size(); ++node) {
        chunk.NodeVoltages[node].assign(result.NodeVoltages[node].begin() + static_cast<std::ptrdiff_t>(first),
                                        result.NodeVoltages[node].end());
    }
    chunk.Currents.resize(result.Currents.size());
    for (std::size_t i = 0; i < result.Currents.size(); ++i) {
        chunk.Currents[i].Name = result.Currents[i].Name;
        chunk.Currents[i].Values.assign(result.Currents[i].Values.begin() + static_cast<std::ptrdiff_t>(first),
                                        result.Currents[i].Values.end());
    }
    chunk.IsComplete = result.IsComplete;
    return chunk;
}

void PublishChunk(const TransientResult &result, ChunkPublisher &publisher, const double progress) {
    const RunMonitor &monitor = publisher.Monitor;
    if (monitor.OnProgress) {
        monitor.OnProgress(progress);
    }
    if (!monitor.PostTransientChunk || result.Times.size() <= publisher.Sent) {
        return;
    }
    const TransientResult chunk = SliceFrom(result, publisher.Sent);
    publisher.Sent = result.Times.size();
    monitor.PostTransientChunk(chunk);
}

/**
 * @brief   Tells whether the points solved since the last chunk are due to go to the monitor.
 * @details They are when a span of the run, a time or a number of points has passed.
 */
bool IsChunkDue(const ChunkPublisher &publisher, const double time, const std::size_t point_count) {
    const bool spans_chunk = publisher.Monitor.ChunkSpan > 0.0 && time >= publisher.ChunkEnd;
    return spans_chunk || std::chrono::steady_clock::now() - publisher.LastSend > CHUNK_INTERVAL
        || point_count - publisher.Sent > CHUNK_POINT_LIMIT;
}

/**
 * @brief   Hands the points to the monitor, lets it hold the run, and applies the changes it asks for.
 * @return  True when the monitor asked the run to stop.
 */
bool ServeChunk(CompiledCircuit &compiled,
                ChunkPublisher &publisher,
                const TransientResult &result,
                const double time,
                const double stop_time,
                std::vector<DiagnosticMessage> *messages) {
    const RunMonitor &monitor = publisher.Monitor;
    PublishChunk(result, publisher, std::min(1.0, time / stop_time));
    // The caller may hold here, to pace a live run or while it is paused, so the changes come after
    const bool goes_on = !monitor.AfterChunk || monitor.AfterChunk(time);
    if (!goes_on || (monitor.ShouldStop && monitor.ShouldStop())) {
        return true;
    }
    ApplyLiveChanges(compiled, monitor, messages);
    publisher.LastSend = std::chrono::steady_clock::now();
    publisher.ChunkEnd = time + monitor.ChunkSpan;
    return false;
}

} // namespace

/**
 * @brief   Computes the response of the circuit over time, with a step that adapts to the error of the integration.
 * @param[in,out] compiled                Compiled circuit with its matrix and devices.
 * @param[in]     settings                Stop time, output step, integration method and seed of the random sources.
 * @param[in]     has_initial_conditions  Whether the run starts from the initial conditions of the circuit instead of
 *                                        its operating point.
 * @param[in]     monitor                 Receives the progress and the points as they are solved, and may hold, change
 *                                        or stop the run.
 * @param[in]     options                 Numerical limits of the solves.
 * @param[out]    messages                Diagnostic messages collected during the analysis; may be null.
 * @return  The points on the output grid and at the corners of the sources, or an error description.
 */
std::expected<TransientResult, std::string> ComputeTransient(CompiledCircuit &compiled,
                                                             const TransientConfig &settings,
                                                             const bool has_initial_conditions,
                                                             const RunMonitor &monitor,
                                                             const ConvergenceOptions &options,
                                                             std::vector<DiagnosticMessage> *messages) {

    const double output_step = settings.TimeStep > 0.0 ? settings.TimeStep : (settings.StopTime / 1000.0);
    TransientCollector collector(compiled, output_step);
    SeedDeviceRandomness(compiled, settings);

    std::expected<std::vector<double>, std::string> x_start =
        SolveStart(compiled, has_initial_conditions, output_step, options, messages);
    if (!x_start) {
        return std::unexpected(x_start.error());
    }
    AcceptDeviceStep(compiled, *x_start);

    StepState state;
    state.CurrentsNow = ReadCurrentValues(compiled, *x_start);
    collector.Append(0.0, *x_start, state.CurrentsNow);
    state.Step = std::min(output_step, settings.StopTime);
    state.Old = *x_start;
    state.Older = *x_start;
    state.Now = std::move(*x_start);
    state.NextCorner = FindNextCorner(compiled, state.Time);
    ChunkPublisher publisher{.Monitor = monitor,
                             .LastSend = std::chrono::steady_clock::now(),
                             .ChunkEnd = monitor.ChunkSpan,
                             .Sent = 0};

    bool is_stopped = false;
    std::optional<std::string> failure;
    while (state.Time < settings.StopTime - TIME_TOLERANCE * std::max(1.0, settings.StopTime)) {
        const bool reaches_corner = PlanStep(state, settings.StopTime);
        std::vector<double> x_new = state.Now;
        const NewtonOutcome outcome = SolveStep(compiled, state, settings.Method, options, x_new);
        if (!outcome.Converged) {
            failure = RetryAfterNonConvergence(compiled, state, outcome);
            if (failure) {
                break;
            }
            continue;
        }
        const double ratio = EstimateTruncationRatio(compiled, state, x_new, settings.Method, options);
        if (ratio > LTE_REJECT_FACTOR) {
            failure = RetryAfterRejection(compiled, state, ratio);
            if (failure) {
                break;
            }
            continue;
        }
        AcceptStep(compiled, state, collector, x_new, ratio, reaches_corner, output_step);
        if (IsChunkDue(publisher, state.Time, collector.PointCount())
            && ServeChunk(compiled, publisher, collector.View(), state.Time, settings.StopTime, messages)) {
            is_stopped = true;
            break;
        }
    }

    const bool is_complete = !is_stopped && !failure;
    if (is_complete
        && (collector.View().Times.empty()
            || std::fabs(collector.View().Times.back() - settings.StopTime)
                   > STOP_POINT_TOLERANCE * std::max(1.0, settings.StopTime))) {
        collector.Append(settings.StopTime, state.Now, state.CurrentsNow);
    }
    TransientResult result = collector.Take();
    result.IsComplete = is_complete;
    PublishChunk(result, publisher, is_complete ? 1.0 : state.Time / settings.StopTime);
    if (failure) {
        return std::unexpected(*failure);
    }
    if (messages) {
        messages->push_back({std::format("nodalys: transient made {} output points", result.Times.size()), false});
    }
    return result;
}

} // namespace Nodalys
