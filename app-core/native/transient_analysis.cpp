/**
 * @file    transient_analysis.cpp
 * @brief   Time-domain transient analysis (`ComputeTransient`) of the Nodalys solver using Backward-Euler startup,
 *          Trapezoidal integration, and SPICE 3rd divided-difference local truncation error (LTE) step control.
 */

#include "native/analyses.h"

#include "native/devices.h"
#include "simulation/live_change.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <thread>
#include <utility>

namespace Core::Native {

namespace {

/// A chunk of a transient is sent after this much wall time, like the ngspice backend targets.
constexpr std::chrono::duration<double> CHUNK_INTERVAL{0.03};
/// A chunk is sent after this many points even when little wall time passed, so a fast run stays responsive.
constexpr std::size_t CHUNK_POINT_LIMIT = 4096;
/// The LTE estimate may overshoot the tolerance this much before the step is rejected outright.
constexpr double LTE_REJECT_FACTOR = 2.0;
/// Safety margin of the LTE step control.
constexpr double LTE_SAFETY = 0.9;
/// The step grows or shrinks at most this much per point, which keeps the predictor meaningful.
constexpr double STEP_GROW_LIMIT = 3.0;
constexpr double STEP_SHRINK_LIMIT = 0.3;
/// Smallest integration step; reaching it without convergence ends the run.
constexpr double MIN_STEP = 1e-15;
/// The live run waits for the clock and for pauses in slices this long, so a stop waits little.
constexpr std::chrono::duration<double> PACE_WAIT_SLICE{0.01};

/**
 * @class   TransientCollector
 * @brief   Accumulates the output points of a transient, interpolating internal solver steps onto the uniform output grid.
 */
class TransientCollector {
public:
    TransientCollector(const CompiledCircuit &compiled, const double output_step)
        : m_Compiled(compiled), m_OutputStep(output_step) {
        m_Result.NodeVoltages.resize(static_cast<std::size_t>(compiled.CircuitNodeCount));
        for (const CurrentRef &current : compiled.Currents) {
            m_Result.Currents.push_back({current.Name, {}});
        }
    }

    std::vector<double> ComputeCurrents(const std::vector<double> &x) const {
        std::vector<double> currents(m_Compiled.Currents.size(), 0.0);
        for (std::size_t index = 0; index < m_Compiled.Currents.size(); ++index) {
            double current = 0.0;
            for (const CurrentContribution &contribution : m_Compiled.Currents[index].Contributions) {
                current += contribution.Element->TerminalCurrent(contribution.Terminal, x);
            }
            currents[index] = current;
        }
        return currents;
    }

    /// Appends the point of time `t` with solution `x` and precomputed currents.
    void Append(const double time, const std::vector<double> &x, const std::vector<double> &currents) {
        m_Result.Times.push_back(time);
        for (int node = 0; node < m_Compiled.CircuitNodeCount; ++node) {
            m_Result.NodeVoltages[static_cast<std::size_t>(node)].push_back(NodeVoltage(x, node));
        }
        for (std::size_t index = 0; index < m_Compiled.Currents.size(); ++index) {
            m_Result.Currents[index].Values.push_back(currents[index]);
        }
    }

    /// Appends the output-grid points of the segment `(t0, x0, i0) -> (t1, x1, i1)`; `t0` itself is already stored.
    /// Grid points strictly inside the segment are stored by linear interpolation; when the segment end lands on the
    /// grid it is stored with the exact solution and currents.
    void AppendSegment(const double t0, const std::vector<double> &x0, const std::vector<double> &i0,
                       const double t1, const std::vector<double> &x1, const std::vector<double> &i1) {
        const double h = m_OutputStep;
        const double eps = 1e-9 * std::max(1.0, std::fabs(t1));
        while (static_cast<double>(m_NextGrid) * h < t1 - eps) {
            const double grid = static_cast<double>(m_NextGrid) * h;
            const double share = (grid - t0) / (t1 - t0);
            m_Scratch.resize(x0.size());
            std::ranges::transform(x0, x1, m_Scratch.begin(),
                                   [share](const double a, const double b) { return a + share * (b - a); });
            m_CurrentsScratch.resize(i0.size());
            std::ranges::transform(i0, i1, m_CurrentsScratch.begin(),
                                   [share](const double a, const double b) { return a + share * (b - a); });
            Append(grid, m_Scratch, m_CurrentsScratch);
            ++m_NextGrid;
        }
        if (std::fabs(static_cast<double>(m_NextGrid) * h - t1) <= eps) {
            Append(t1, x1, i1);
            ++m_NextGrid;
        }
    }

    std::size_t PointCount() const { return m_Result.Times.size(); }

    const Transient &View() const { return m_Result; }

    Transient &MutableView() { return m_Result; }

    Transient Take() { return std::move(m_Result); }

private:
    double NodeVoltage(const std::vector<double> &x, const int node) const {
        const int index = m_Compiled.System->NodeIndex(node);
        return index < 0 ? 0.0 : x[static_cast<std::size_t>(index)];
    }

    const CompiledCircuit &m_Compiled;
    double m_OutputStep;
    /// Index of the next output-grid point to store; state, so float drift cannot store a grid point twice.
    long m_NextGrid = 1;
    Transient m_Result;
    std::vector<double> m_Scratch;
    std::vector<double> m_CurrentsScratch;
};

/// Applies the live changes a run received since the last chunk, by the element name they address.
void ApplyLiveChanges(CompiledCircuit &compiled, RunControl &control, std::vector<SimulatorMessage> &messages) {
    for (const LiveChange &change : control.TakeChanges()) {
        const auto found = compiled.ElementsByName.find(change.Element);
        if (found == compiled.ElementsByName.end()) {
            continue;
        }
        bool applied = false;
        if (change.Quantity == LiveQuantity::RESISTANCE) {
            if (auto *resistor = dynamic_cast<ResistorDevice *>(found->second)) {
                resistor->SetResistance(change.Value);
                applied = true;
            }
        } else if (change.Quantity == LiveQuantity::DC_VOLTAGE) {
            if (auto *source = dynamic_cast<VoltageSourceDevice *>(found->second)) {
                source->SetDcValue(change.Value);
                applied = true;
            }
        }
        if (!applied) {
            messages.push_back({std::format("native: cannot apply the live change to {}", change.Element), true});
        }
    }
}

/**
 * @brief   Holds a live run at its pace: waits until the clock reaches the simulated time, then while it is paused,
 *          moving the clock start by the pause so the run never catches up, and returns whether the run goes on.
 */
bool KeepLivePace(std::chrono::steady_clock::time_point &start, const double reached, const double pace,
                  const RunControl *control) {
    const auto due = start + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                 std::chrono::duration<double>(reached / pace));
    while (control == nullptr || !control->IsCancelled()) {
        const std::chrono::duration<double> left = due - std::chrono::steady_clock::now();
        if (left <= std::chrono::duration<double>::zero()) {
            break;
        }
        std::this_thread::sleep_for(std::min(left, PACE_WAIT_SLICE));
    }
    auto paused = std::chrono::steady_clock::duration::zero();
    while (control != nullptr && control->IsPaused() && !control->IsCancelled()) {
        std::this_thread::sleep_for(PACE_WAIT_SLICE);
        paused = std::chrono::steady_clock::now() - due;
    }
    if (paused > std::chrono::steady_clock::duration::zero()) {
        start += paused;
    }
    return control == nullptr || !control->IsCancelled();
}

/**
 * @brief   Sends the points made since the last chunk, if the monitor wants them, and reports the progress.
 */
bool PublishChunk(Transient &accumulated, const RunMonitor &monitor, const double progress, std::size_t &sent) {
    if (monitor.Control != nullptr) {
        monitor.Control->SetProgress(progress);
    }
    if (!monitor.OnChunk) {
        return true;
    }
    Transient chunk;
    const std::size_t count = accumulated.Times.size();
    chunk.Times.assign(accumulated.Times.begin() + static_cast<std::ptrdiff_t>(sent), accumulated.Times.end());
    chunk.NodeVoltages.resize(accumulated.NodeVoltages.size());
    for (std::size_t node = 0; node < accumulated.NodeVoltages.size(); ++node) {
        chunk.NodeVoltages[node].assign(accumulated.NodeVoltages[node].begin() + static_cast<std::ptrdiff_t>(sent),
                                        accumulated.NodeVoltages[node].end());
    }
    chunk.Currents.resize(accumulated.Currents.size());
    for (std::size_t current = 0; current < accumulated.Currents.size(); ++current) {
        chunk.Currents[current].Name = accumulated.Currents[current].Name;
        chunk.Currents[current].Values.assign(accumulated.Currents[current].Values.begin() +
                                                  static_cast<std::ptrdiff_t>(sent),
                                              accumulated.Currents[current].Values.end());
    }
    sent = count;
    monitor.OnChunk(std::move(chunk));
    return true;
}

} // namespace

std::expected<Transient, std::string> ComputeTransient(CompiledCircuit &compiled, const TransientSettings &settings,
                                                       const bool has_initial_conditions, const RunMonitor &monitor,
                                                       const ConvergenceOptions &options,
                                                       std::vector<SimulatorMessage> &messages) {
    const int size = compiled.System->Size();
    TransientCollector collector(compiled, settings.TimeStep);

    // The starting point: the operating point, or the forced initial conditions of a uic run
    std::vector<double> x_now(static_cast<std::size_t>(size), 0.0);
    if (has_initial_conditions) {
        const TimeContext initial{
            .IsTransient = true, .UseInitialConditions = true, .Time = 0.0, .TimeStep = settings.TimeStep};
        const NewtonOutcome outcome = SolveDc(*compiled.System, compiled.Devices, x_now, options, initial);
        if (!outcome.Converged) {
            return std::unexpected("The native solver could not start the transient from the initial conditions");
        }
        DescribeOutcome(outcome, "initial point", messages);
    } else {
        const NewtonOutcome outcome = SolveDc(*compiled.System, compiled.Devices, x_now, options, TimeContext{});
        if (!outcome.Converged) {
            return std::unexpected("The native solver found no operating point to start the transient from");
        }
        DescribeOutcome(outcome, "operating point", messages);
    }
    for (const std::unique_ptr<Device> &device : compiled.Devices) {
        device->AcceptStep(x_now);
    }
    std::vector<double> currents_now = collector.ComputeCurrents(x_now);
    collector.Append(0.0, x_now, currents_now);

    const bool is_live = monitor.LivePace > 0.0;
    auto start = std::chrono::steady_clock::now();
    auto last_send = std::chrono::steady_clock::now();
    std::size_t sent = 0;
    double time = 0.0;
    double step = std::min(settings.TimeStep, settings.StopTime);
    double previous_step = 0.0;
    double older_step = 0.0;
    bool first_step = true;
    std::vector<double> x_old = x_now;
    std::vector<double> x_older = x_now;
    bool stopped = false;
    std::string failure;

    while (time < settings.StopTime - 1e-12 * std::max(1.0, settings.StopTime)) {
        step = std::min(step, settings.StopTime - time);
        const TimeContext context{.IsTransient = true,
                                  .UseInitialConditions = false,
                                  .BackwardEuler = first_step,
                                  .Time = time + step,
                                  .TimeStep = step};
        std::vector<double> x_new = x_now;
        const NewtonOutcome outcome = SolveDc(*compiled.System, compiled.Devices, x_new, options, context);
        if (!outcome.Converged) {
            // A failed Newton at this step: reject it, shrink the step and try again
            for (const std::unique_ptr<Device> &device : compiled.Devices) {
                device->RejectStep();
            }
            step *= 0.1;
            if (step < MIN_STEP) {
                failure =
                    std::format("The native solver could not converge at t = {}; the time step collapsed", time + step);
                break;
            }
            continue;
        }

        // Local truncation error of the trapezoidal step from the 3rd divided difference across the last three points
        // (Nagel SPICE2 Eq. 5.26 / SPICE3f5 CKTterr: LTE_trap = (h^3 / 12) * |x'''| = (h^3 / 2) * |d123|).
        double ratio = 0.0;
        if (previous_step > 0.0 && older_step > 0.0) {
            for (int index = 0; index < size; ++index) {
                if (!compiled.DynamicNode[static_cast<std::size_t>(index)]) {
                    continue;
                }
                const std::size_t i = static_cast<std::size_t>(index);
                const double d1 = (x_new[i] - x_now[i]) / step;
                const double d2 = (x_now[i] - x_old[i]) / previous_step;
                const double d3 = (x_old[i] - x_older[i]) / older_step;
                const double d12 = (d1 - d2) / (step + previous_step);
                const double d23 = (d2 - d3) / (previous_step + older_step);
                const double d123 = (d12 - d23) / (step + previous_step + older_step);
                const double error = 0.5 * step * step * step * std::fabs(d123);
                const double scale_v = std::max({std::fabs(x_new[i]), std::fabs(x_now[i]), std::fabs(x_old[i])});
                const double tolerance = 7.0 * (options.RelTol * scale_v + options.VoltageAbsTol);
                ratio = std::max(ratio, error / tolerance);
            }
        }
        if (ratio > LTE_REJECT_FACTOR) {
            for (const std::unique_ptr<Device> &device : compiled.Devices) {
                device->RejectStep();
            }
            step = std::max(step * std::max(LTE_SAFETY * std::pow(ratio, -1.0 / 3.0), STEP_SHRINK_LIMIT), MIN_STEP);
            if (step <= MIN_STEP) {
                failure =
                    std::format("The native solver kept rejecting the time step at t = {}; the step collapsed", time);
                break;
            }
            continue;
        }

        for (const std::unique_ptr<Device> &device : compiled.Devices) {
            device->AcceptStep(x_new);
        }
        std::vector<double> currents_new = collector.ComputeCurrents(x_new);
        collector.AppendSegment(time, x_now, currents_now, time + step, x_new, currents_new);
        currents_now = std::move(currents_new);
        x_older = std::exchange(x_old, std::exchange(x_now, x_new));
        time += step;
        older_step = std::exchange(previous_step, step);
        first_step = false;
        const double next = step * std::clamp(LTE_SAFETY * std::pow(std::max(ratio, 1e-3), -1.0 / 3.0),
                                              STEP_SHRINK_LIMIT, STEP_GROW_LIMIT);
        step = std::clamp(next, MIN_STEP, settings.TimeStep);

        // Hand over the points made since the last chunk, and check whether the run goes on
        const bool is_monitored = monitor.Control != nullptr || static_cast<bool>(monitor.OnChunk);
        if (is_monitored && (std::chrono::steady_clock::now() - last_send > CHUNK_INTERVAL ||
                             collector.PointCount() - sent > CHUNK_POINT_LIMIT)) {
            if (is_live && monitor.Control != nullptr) {
                ApplyLiveChanges(compiled, *monitor.Control, messages);
            }
            if (is_live && !KeepLivePace(start, time, monitor.LivePace, monitor.Control)) {
                stopped = true;
                break;
            }
            PublishChunk(collector.MutableView(), monitor, std::min(1.0, time / settings.StopTime), sent);
            last_send = std::chrono::steady_clock::now();
            if (monitor.Control != nullptr && monitor.Control->IsCancelled()) {
                stopped = true;
                break;
            }
        }
    }

    if (!stopped && failure.empty() &&
        (collector.View().Times.empty() ||
         std::fabs(collector.View().Times.back() - settings.StopTime) > 1e-9 * std::max(1.0, settings.StopTime))) {
        collector.Append(settings.StopTime, x_now, currents_now);
    }
    Transient result = collector.Take();
    result.IsComplete = !stopped && failure.empty();
    PublishChunk(result, monitor, result.IsComplete ? 1.0 : time / settings.StopTime, sent);
    if (!failure.empty()) {
        return std::unexpected(failure);
    }
    messages.push_back({std::format("native: transient made {} output points", result.Times.size()), false});
    return result;
}

} // namespace Core::Native
