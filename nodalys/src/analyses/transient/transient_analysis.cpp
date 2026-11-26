/**
 * @file    transient_analysis.cpp
 * @brief   Nonlinear time-domain transient analysis solver with adaptive stepping.
 */

#include "analyses/analyses.h"

#include "analyses/common/analysis_preamble.h"
#include "analyses/common/solution_readout.h"
#include "analyses/transient/transient_collector.h"
#include "devices/sources/independent_sources.h"
#include "devices/sources/transient_noise.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <limits>
#include <optional>
#include <random>
#include <utility>

namespace Nodalys {

namespace {

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
/// Share of the largest step below which a step that switches a device is accepted whatever its error estimate: the
/// estimate then measures the jump in slope at the switch, which no smaller step removes.
constexpr double SWITCH_RESOLUTION_SHARE = 1e-6;

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
    double NextSourceCorner = std::numeric_limits<double>::infinity();
    std::vector<double> Now;
    std::vector<double> Old;
    std::vector<double> Older;
    std::vector<double> CurrentsNow;
};

/**
 * @struct  SteppingOutcome
 * @brief   Outcome of the adaptive transient time-stepping loop.
 */
struct SteppingOutcome {
    bool IsStopped = false;
    std::optional<std::string> Failure;
};

/**
 * @brief   Finds the next corner of any source or delay waveform after an instant, so the step never jumps over an
 * edge.
 */
double FindNextCorner(const CompiledCircuit &compiled, const double time) {
    double corner = std::numeric_limits<double>::infinity();
    for (const std::unique_ptr<Device> &device : compiled.Devices) {
        if (const TransientDevice *transient = device->AsTransient()) {
            corner = std::min(corner, transient->NextBreakpoint(time));
        }
    }
    return corner;
}

double FindNextSourceCorner(const CompiledCircuit &compiled, const double time) {
    double corner = std::numeric_limits<double>::infinity();
    for (const std::unique_ptr<Device> &device : compiled.Devices) {
        if (device->AsIndependentSource() != nullptr) {
            if (const TransientDevice *transient = device->AsTransient()) {
                corner = std::min(corner, transient->NextBreakpoint(time));
            }
        }
    }
    return corner;
}

double FindMaxDeviceStep(const CompiledCircuit &compiled, const double stop_time) {
    double limit = std::numeric_limits<double>::infinity();
    for (const std::unique_ptr<Device> &device : compiled.Devices) {
        if (const TransientDevice *transient = device->AsTransient()) {
            limit = std::min(limit, transient->MaxTimeStep(stop_time));
        }
    }
    return limit;
}

void NoteDeviceCorners(CompiledCircuit &compiled, const double time, const double max_step) {
    for (const std::unique_ptr<Device> &device : compiled.Devices) {
        if (TransientDevice *transient = device->AsTransient()) {
            transient->NoteCorner(time, max_step);
        }
    }
}

std::optional<std::string> CheckTransientDevices(const CompiledCircuit &compiled) {
    for (const std::unique_ptr<Device> &device : compiled.Devices) {
        if (const FrequencyDevice *freq = device->AsFrequency()) {
            if (const std::string_view reason = freq->TransientUnsupportedReason(); !reason.empty()) {
                return std::string(reason);
            }
        }
    }
    return std::nullopt;
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
        if (IndependentSourceDevice *source = compiled.Devices[index]->AsIndependentSource()) {
            source->SetRandomSeed(MixTransientSeed(base_seed, index));
        }
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
    if (const std::optional<std::string> unsupported = CheckTransientDevices(compiled)) {
        return std::unexpected(*unsupported);
    }
    const TimeContext initial = has_initial_conditions ? TimeContext{.IsTransient = true,
                                                                     .UseInitialConditions = true,
                                                                     .IsTransientStart = true,
                                                                     .Time = 0.0,
                                                                     .TimeStep = output_step}
                                                       : TimeContext{.IsTransientStart = true};
    std::vector<double> x(static_cast<std::size_t>(compiled.System->GetSize()), 0.0);
    const NewtonOutcome outcome = SolveNewton(compiled, x, initial, options);
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
        if (TransientDevice *transient = device->AsTransient()) {
            transient->AcceptStep(x);
        }
    }
}

/**
 * @brief   Tells whether a device changed its discrete state, such as a switch or a latch, in the step just solved.
 */
bool HasDeviceSwitched(const CompiledCircuit &compiled) {
    return std::ranges::any_of(compiled.Devices, [](const std::unique_ptr<Device> &device) {
        const TransientDevice *transient = device->AsTransient();
        return transient != nullptr && transient->HasSwitched();
    });
}

/**
 * @brief   Decides whether the error estimate of a solved step rejects it.
 * @details A device that switches inside the step bends the waveforms there, which the estimate, built from the
 *          points before the switch, reads as an error that no shorter step removes. Such a step is accepted once it
 *          is shorter than a tiny share of the largest step, and the history restarts after it, as after a corner.
 * @return  True when the step must be retried shorter.
 */
bool IsRejectedByError(const double ratio, const double step, const double max_step, const bool has_switched) {
    if (ratio <= LTE_REJECT_FACTOR) {
        return false;
    }
    return !has_switched || step > SWITCH_RESOLUTION_SHARE * max_step;
}

void RejectDeviceStep(CompiledCircuit &compiled) {
    for (const std::unique_ptr<Device> &device : compiled.Devices) {
        if (TransientDevice *transient = device->AsTransient()) {
            transient->RejectStep();
        }
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
    return SolveNewton(compiled, x_new, context, options);
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

void ResetAfterDiscontinuity(StepState &state) {
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

/**
 * @brief   Accepts a step: hands it to the devices and the collector, moves the history on and chooses the next step.
 */
void AcceptStep(CompiledCircuit &compiled,
                StepState &state,
                TransientCollector &collector,
                const std::vector<double> &x_new,
                const double ratio,
                const bool reaches_corner,
                const double max_step) {
    const bool has_switched = HasDeviceSwitched(compiled);
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
    state.Step = std::clamp(next, MIN_STEP, max_step);
    const bool reaches_source_corner = reaches_corner && state.NextCorner >= state.NextSourceCorner;
    if (reaches_source_corner || has_switched) {
        NoteDeviceCorners(compiled, state.Time, max_step);
    }
    // A device whose state decides its corners, such as a fuse about to blow, moves its next one at every point
    state.NextCorner = FindNextCorner(compiled, state.Time);
    state.NextSourceCorner = FindNextSourceCorner(compiled, state.Time);
    if (reaches_corner || has_switched) {
        ResetAfterDiscontinuity(state);
    }
}

StepState InitStepState(CompiledCircuit &compiled,
                        std::vector<double> x_start,
                        const double max_step,
                        const double stop_time,
                        TransientCollector &collector) {
    StepState state;
    state.CurrentsNow = ReadCurrentValues(compiled, x_start);
    collector.Append(0.0, x_start, state.CurrentsNow);
    state.Step = std::min(max_step, stop_time);
    state.Old = x_start;
    state.Older = x_start;
    state.Now = std::move(x_start);
    NoteDeviceCorners(compiled, 0.0, max_step);
    state.NextCorner = FindNextCorner(compiled, state.Time);
    state.NextSourceCorner = FindNextSourceCorner(compiled, state.Time);
    return state;
}

SteppingOutcome RunSteppingLoop(CompiledCircuit &compiled,
                                const TransientConfig &settings,
                                const double max_step,
                                const ConvergenceOptions &options,
                                StepState &state,
                                TransientCollector &collector,
                                ChunkPublisher &publisher,
                                std::vector<DiagnosticMessage> *messages) {
    SteppingOutcome result;
    const double stop_limit = settings.StopTime - TRANSIENT_TIME_TOLERANCE * std::max(1.0, settings.StopTime);
    while (state.Time < stop_limit) {
        const bool reaches_corner = PlanStep(state, settings.StopTime);
        std::vector<double> x_new = state.Now;
        const NewtonOutcome outcome = SolveStep(compiled, state, settings.Method, options, x_new);
        if (!outcome.Converged) {
            result.Failure = RetryAfterNonConvergence(compiled, state, outcome);
            if (result.Failure) {
                break;
            }
            continue;
        }
        const double ratio = EstimateTruncationRatio(compiled, state, x_new, settings.Method, options);
        if (IsRejectedByError(ratio, state.Step, max_step, HasDeviceSwitched(compiled))) {
            result.Failure = RetryAfterRejection(compiled, state, ratio);
            if (result.Failure) {
                break;
            }
            continue;
        }
        AcceptStep(compiled, state, collector, x_new, ratio, reaches_corner, max_step);
        if (IsChunkDue(publisher, state.Time, collector.PointCount())
            && ServeChunk(compiled, publisher, collector.View(), state.Time, settings.StopTime, messages)) {
            result.IsStopped = true;
            break;
        }
    }
    return result;
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
    const double max_step = std::min(output_step, FindMaxDeviceStep(compiled, settings.StopTime));
    TransientCollector collector(compiled, output_step);
    SeedDeviceRandomness(compiled, settings);
    std::expected<std::vector<double>, std::string> x_start =
        SolveStart(compiled, has_initial_conditions, output_step, options, messages);
    if (!x_start) {
        return std::unexpected(x_start.error());
    }
    AcceptDeviceStep(compiled, *x_start);
    StepState state = InitStepState(compiled, std::move(*x_start), max_step, settings.StopTime, collector);
    ChunkPublisher publisher{.Monitor = monitor,
                             .LastSend = std::chrono::steady_clock::now(),
                             .ChunkEnd = monitor.ChunkSpan,
                             .Sent = 0};
    const SteppingOutcome stepping =
        RunSteppingLoop(compiled, settings, max_step, options, state, collector, publisher, messages);
    const bool is_complete = !stepping.IsStopped && !stepping.Failure;
    if (is_complete) {
        collector.AppendFinalPointIfNeeded(settings.StopTime, state.Now, state.CurrentsNow);
    }
    TransientResult result = collector.Take();
    result.IsComplete = is_complete;
    PublishChunk(result, publisher, is_complete ? 1.0 : state.Time / settings.StopTime);
    if (stepping.Failure) {
        return std::unexpected(*stepping.Failure);
    }
    if (messages) {
        messages->push_back({std::format("nodalys: transient made {} output points", result.Times.size()), false});
    }
    return result;
}

} // namespace Nodalys
