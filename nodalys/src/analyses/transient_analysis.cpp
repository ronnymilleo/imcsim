/**
 * @file    transient_analysis.cpp
 * @brief   Nonlinear time-domain transient analysis solver with adaptive stepping.
 */

#include "analyses/analyses.h"

#include "devices/devices.h"
#include "devices/transient_noise.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <limits>
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
        std::vector<double> currents;
        currents.reserve(m_Compiled.Currents.size());
        for (const CurrentRef &ref : m_Compiled.Currents) {
            double value = 0.0;
            for (const CurrentContribution &contribution : ref.Contributions) {
                value += contribution.Element->TerminalCurrent(contribution.Terminal, x);
            }
            currents.push_back(value);
        }
        return currents;
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

    void AppendSegment(const double t_start, const std::vector<double> &x_start,
                       const std::vector<double> &currents_start, const double t_end, const std::vector<double> &x_end,
                       const std::vector<double> &currents_end) {
        if (m_OutputStep <= 0.0) {
            Append(t_end, x_end, currents_end);
            return;
        }
        const double segment_duration = t_end - t_start;
        if (segment_duration <= 0.0) {
            return;
        }

        m_Scratch.resize(x_start.size());
        m_CurrentsScratch.resize(currents_start.size());

        while (true) {
            const double target_time = static_cast<double>(m_NextGrid) * m_OutputStep;
            if (target_time > t_end + 1e-12 * m_OutputStep) {
                break;
            }
            const double alpha = std::clamp((target_time - t_start) / segment_duration, 0.0, 1.0);
            for (std::size_t i = 0; i < m_Scratch.size(); ++i) {
                m_Scratch[i] = x_start[i] + alpha * (x_end[i] - x_start[i]);
            }
            for (std::size_t i = 0; i < m_CurrentsScratch.size(); ++i) {
                m_CurrentsScratch[i] = currents_start[i] + alpha * (currents_end[i] - currents_start[i]);
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
        if (!times.empty() && std::fabs(times.back() - time) <= 1e-12 * std::max(std::fabs(time), m_OutputStep)) {
            return;
        }
        Append(time, x, currents);
    }

    std::size_t PointCount() const { return m_Result.Times.size(); }
    const TransientResult &View() const { return m_Result; }
    TransientResult &MutableView() { return m_Result; }
    TransientResult Take() { return std::move(m_Result); }

private:
    const CompiledCircuit &m_Compiled;
    double m_OutputStep;
    long m_NextGrid = 1;
    TransientResult m_Result;
    std::vector<double> m_Scratch;
    std::vector<double> m_CurrentsScratch;
};

void ApplyLiveChanges(CompiledCircuit &compiled, const RunMonitor &monitor, std::vector<DiagnosticMessage> *messages) {
    if (!monitor.TakeLiveChanges) {
        return;
    }
    for (const LiveChange &change : monitor.TakeLiveChanges()) {
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
        if (!applied && messages) {
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

bool PublishChunk(TransientResult &result, const RunMonitor &monitor, const double progress, std::size_t &sent) {
    if (monitor.OnProgress) {
        monitor.OnProgress(progress);
    }
    if (!monitor.PostTransientChunk || result.Times.size() <= sent) {
        return true;
    }
    TransientResult chunk;
    chunk.Times.assign(result.Times.begin() + static_cast<std::ptrdiff_t>(sent), result.Times.end());
    chunk.NodeVoltages.resize(result.NodeVoltages.size());
    for (std::size_t node = 0; node < result.NodeVoltages.size(); ++node) {
        chunk.NodeVoltages[node].assign(result.NodeVoltages[node].begin() + static_cast<std::ptrdiff_t>(sent),
                                        result.NodeVoltages[node].end());
    }
    chunk.Currents.resize(result.Currents.size());
    for (std::size_t i = 0; i < result.Currents.size(); ++i) {
        chunk.Currents[i].Name = result.Currents[i].Name;
        chunk.Currents[i].Values.assign(result.Currents[i].Values.begin() + static_cast<std::ptrdiff_t>(sent),
                                        result.Currents[i].Values.end());
    }
    chunk.IsComplete = result.IsComplete;
    sent = result.Times.size();
    monitor.PostTransientChunk(chunk);
    return true;
}

} // namespace

std::expected<TransientResult, std::string> ComputeTransient(CompiledCircuit &compiled, const TransientConfig &settings,
                                                             const bool has_initial_conditions,
                                                             const RunMonitor &monitor,
                                                             const ConvergenceOptions &options,
                                                             std::vector<DiagnosticMessage> *messages) {

    const int size = compiled.System->GetSize();
    const double output_step = settings.TimeStep > 0.0 ? settings.TimeStep : (settings.StopTime / 1000.0);
    TransientCollector collector(compiled, output_step);

    const std::uint64_t base_seed =
        settings.Seed != 0 ? static_cast<std::uint64_t>(settings.Seed) : std::random_device{}();
    for (std::size_t index = 0; index < compiled.Devices.size(); ++index) {
        compiled.Devices[index]->SetRandomSeed(MixTransientSeed(base_seed, index));
    }

    std::vector<double> x_now(static_cast<std::size_t>(size), 0.0);
    if (has_initial_conditions) {
        const TimeContext initial{
            .IsTransient = true, .UseInitialConditions = true, .Time = 0.0, .TimeStep = output_step};
        const NewtonOutcome outcome = SolveNewton(*compiled.System, compiled.Devices, x_now, initial, options);
        if (!outcome.Converged) {
            return std::unexpected(
                DescribeFailure("Nodalys could not start the transient from the initial conditions", outcome));
        }
        DescribeOutcome(outcome, "initial point", messages);
    } else {
        const NewtonOutcome outcome = SolveNewton(*compiled.System, compiled.Devices, x_now, TimeContext{}, options);
        if (!outcome.Converged) {
            return std::unexpected(
                DescribeFailure("Nodalys found no operating point to start the transient from", outcome));
        }
        DescribeOutcome(outcome, "operating point", messages);
    }
    for (const std::unique_ptr<Device> &device : compiled.Devices) {
        device->AcceptStep(x_now);
    }
    std::vector<double> currents_now = collector.ComputeCurrents(x_now);
    collector.Append(0.0, x_now, currents_now);

    auto last_send = std::chrono::steady_clock::now();
    double chunk_end = monitor.ChunkSpan;
    std::size_t sent = 0;
    double time = 0.0;
    double step = std::min(output_step, settings.StopTime);
    double previous_step = 0.0;
    double older_step = 0.0;
    // Backward Euler starts the run and leaves every corner, where the trapezoidal rule would ring
    bool is_restart = true;
    std::vector<double> x_old = x_now;
    std::vector<double> x_older = x_now;
    bool stopped = false;
    std::string failure;
    double next_corner = FindNextCorner(compiled, time);

    while (time < settings.StopTime - 1e-12 * std::max(1.0, settings.StopTime)) {
        step = std::min(step, settings.StopTime - time);
        const bool reaches_corner = time + step >= next_corner;
        if (reaches_corner) {
            step = next_corner - time;
        }
        const TimeContext context{.IsTransient = true,
                                  .UseInitialConditions = false,
                                  .BackwardEuler = is_restart,
                                  .Method = settings.Method,
                                  .PreviousTimeStep = previous_step,
                                  .Time = time + step,
                                  .TimeStep = step};
        std::vector<double> x_new = x_now;
        const NewtonOutcome outcome = SolveNewton(*compiled.System, compiled.Devices, x_new, context, options);
        if (!outcome.Converged) {
            for (const std::unique_ptr<Device> &device : compiled.Devices) {
                device->RejectStep();
            }
            step *= 0.1;
            if (step < MIN_STEP) {
                failure = DescribeFailure(
                    std::format("Nodalys could not converge at t = {}; the time step collapsed", time + step), outcome);
                break;
            }
            continue;
        }

        // Local truncation error
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
                const double error = TruncationErrorFactor(settings.Method, step, previous_step) * std::fabs(d123);
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
                failure = std::format("Nodalys kept rejecting the time step at t = {}; the step collapsed", time);
                break;
            }
            continue;
        }

        const bool has_switched = std::ranges::any_of(
            compiled.Devices, [](const std::unique_ptr<Device> &device) { return device->HasSwitched(); });
        for (const std::unique_ptr<Device> &device : compiled.Devices) {
            device->AcceptStep(x_new);
        }
        std::vector<double> currents_new = collector.ComputeCurrents(x_new);
        collector.AppendSegment(time, x_now, currents_now, time + step, x_new, currents_new);
        if (reaches_corner) {
            collector.AppendCorner(next_corner, x_new, currents_new);
        }
        currents_now = std::move(currents_new);
        x_older = std::exchange(x_old, std::exchange(x_now, x_new));
        time = reaches_corner ? next_corner : time + step;
        older_step = std::exchange(previous_step, step);
        is_restart = false;
        const double next = step * std::clamp(LTE_SAFETY * std::pow(std::max(ratio, 1e-3), -1.0 / 3.0),
                                              STEP_SHRINK_LIMIT, STEP_GROW_LIMIT);
        step = std::clamp(next, MIN_STEP, output_step);
        // A device whose state decides its corners, such as a fuse about to blow, moves its next one at every point
        next_corner = FindNextCorner(compiled, time);
        if (reaches_corner || has_switched) {
            // The slope changes at a corner, and at a switch that flipped inside the step, so the points before it
            // say nothing about the error after it
            is_restart = true;
            previous_step = 0.0;
            older_step = 0.0;
            x_old = x_now;
            x_older = x_now;
            if (std::isfinite(next_corner)) {
                step = std::max(std::min(step, CORNER_STEP_SHARE * (next_corner - time)), MIN_STEP);
            }
        }

        const bool spans_chunk = monitor.ChunkSpan > 0.0 && time >= chunk_end;
        if (spans_chunk || std::chrono::steady_clock::now() - last_send > CHUNK_INTERVAL ||
            collector.PointCount() - sent > CHUNK_POINT_LIMIT) {
            PublishChunk(collector.MutableView(), monitor, std::min(1.0, time / settings.StopTime), sent);
            // The caller may hold here, to pace a live run or while it is paused, so the changes come after
            const bool goes_on = !monitor.AfterChunk || monitor.AfterChunk(time);
            if (!goes_on || (monitor.ShouldStop && monitor.ShouldStop())) {
                stopped = true;
                break;
            }
            ApplyLiveChanges(compiled, monitor, messages);
            last_send = std::chrono::steady_clock::now();
            chunk_end = time + monitor.ChunkSpan;
        }
    }

    if (!stopped && failure.empty() &&
        (collector.View().Times.empty() ||
         std::fabs(collector.View().Times.back() - settings.StopTime) > 1e-9 * std::max(1.0, settings.StopTime))) {
        collector.Append(settings.StopTime, x_now, currents_now);
    }
    TransientResult result = collector.Take();
    result.IsComplete = !stopped && failure.empty();
    PublishChunk(result, monitor, result.IsComplete ? 1.0 : time / settings.StopTime, sent);
    if (!failure.empty()) {
        return std::unexpected(failure);
    }
    if (messages) {
        messages->push_back({std::format("nodalys: transient made {} output points", result.Times.size()), false});
    }
    return result;
}

} // namespace Nodalys
