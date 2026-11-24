/**
 * @file    transient_collector.cpp
 * @brief   Output grid interpolation, corner recording, chunk streaming, and live-change dispatch for transient runs.
 */

#include "analyses/transient/transient_collector.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace Nodalys {

namespace {

constexpr std::chrono::duration<double> CHUNK_INTERVAL{0.03};
constexpr std::size_t CHUNK_POINT_LIMIT = 4096;

void ApplyLiveChanges(CompiledCircuit &compiled, const RunMonitor &monitor, std::vector<DiagnosticMessage> *messages) {
    if (!monitor.TakeLiveChanges) {
        return;
    }
    for (const LiveChange &change : monitor.TakeLiveChanges()) {
        const auto found = compiled.ElementsByName.find(change.Element);
        if (found == compiled.ElementsByName.end()) {
            continue;
        }
        TransientDevice *transient = found->second->AsTransient();
        if ((transient == nullptr || !transient->ApplyLiveChange(change.Quantity, change.Value)) && messages) {
            messages->push_back({std::format("nodalys: cannot apply live change to {}", change.Element), true});
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

} // namespace

/**
 * @brief   Initializes the transient output collector for the circuit nodes and currents.
 * @param[in] compiled     Compiled circuit providing node count and current descriptors.
 * @param[in] output_step  Uniform output grid spacing in seconds.
 */
TransientCollector::TransientCollector(const CompiledCircuit &compiled, const double output_step)
    : m_Compiled(compiled), m_OutputStep(output_step) {
    m_Result.NodeVoltages.resize(static_cast<std::size_t>(compiled.CircuitNodeCount));
    for (const CurrentRef &current : compiled.Currents) {
        m_Result.Currents.push_back({current.Name, {}});
    }
}

/**
 * @brief   Appends one time point with its node voltages and branch currents to the result.
 * @param[in] time      Instant in seconds.
 * @param[in] x         Solution vector at `time`.
 * @param[in] currents  Readout currents at `time`.
 */
void TransientCollector::Append(const double time, const std::vector<double> &x, const std::vector<double> &currents) {
    m_Result.Times.push_back(time);
    for (int node = 0; node < m_Compiled.CircuitNodeCount; ++node) {
        const double voltage = node == 0 ? 0.0 : x[static_cast<std::size_t>(m_Compiled.System->NodeIndex(node))];
        m_Result.NodeVoltages[static_cast<std::size_t>(node)].push_back(voltage);
    }
    for (std::size_t index = 0; index < currents.size(); ++index) {
        m_Result.Currents[index].Values.push_back(currents[index]);
    }
}

/**
 * @brief   Interpolates and appends all uniform output-grid points falling inside `[start.Time, end.Time]`.
 * @param[in] start  Solution at the start of the accepted step.
 * @param[in] end    Solution at the end of the accepted step.
 */
void TransientCollector::AppendSegment(const SolutionPoint &start, const SolutionPoint &end) {
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
        if (target_time > end.Time + TRANSIENT_TIME_TOLERANCE * m_OutputStep) {
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
 * @param[in] time      Corner instant in seconds.
 * @param[in] x         Solution vector at the corner.
 * @param[in] currents  Readout currents at the corner.
 */
void TransientCollector::AppendCorner(const double time,
                                      const std::vector<double> &x,
                                      const std::vector<double> &currents) {
    const std::vector<double> &times = m_Result.Times;
    if (!times.empty()
        && std::fabs(times.back() - time) <= TRANSIENT_TIME_TOLERANCE * std::max(std::fabs(time), m_OutputStep)) {
        return;
    }
    Append(time, x, currents);
}

/**
 * @brief   Appends the final point at `stop_time` when the uniform grid did not land on it.
 * @param[in] stop_time  Configured transient stop time in seconds.
 * @param[in] x          Final solution vector.
 * @param[in] currents   Final readout currents.
 */
void TransientCollector::AppendFinalPointIfNeeded(const double stop_time,
                                                  const std::vector<double> &x,
                                                  const std::vector<double> &currents) {
    if (m_Result.Times.empty()
        || std::fabs(m_Result.Times.back() - stop_time) > TRANSIENT_STOP_POINT_TOLERANCE * std::max(1.0, stop_time)) {
        Append(stop_time, x, currents);
    }
}

/**
 * @brief   Reports progress and streams any unsent output points to the RunMonitor.
 * @param[in]     result     Accumulated transient result.
 * @param[in,out] publisher  Chunk streaming state.
 * @param[in]     progress   Normalized progress fraction in `[0, 1]`.
 */
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
 * @param[in] publisher    Chunk streaming state.
 * @param[in] time         Current simulated time in seconds.
 * @param[in] point_count  Total output points collected so far.
 * @return  True when a chunk should be published now.
 */
bool IsChunkDue(const ChunkPublisher &publisher, const double time, const std::size_t point_count) {
    const bool spans_chunk = publisher.Monitor.ChunkSpan > 0.0 && time >= publisher.ChunkEnd;
    return spans_chunk || std::chrono::steady_clock::now() - publisher.LastSend > CHUNK_INTERVAL
        || point_count - publisher.Sent > CHUNK_POINT_LIMIT;
}

/**
 * @brief   Hands the points to the monitor, lets it hold the run, and applies the changes it asks for.
 * @param[in,out] compiled   Compiled circuit receiving live parameter edits.
 * @param[in,out] publisher  Chunk streaming state.
 * @param[in]     result     Accumulated transient result.
 * @param[in]     time       Current simulated time in seconds.
 * @param[in]     stop_time  Configured transient stop time in seconds.
 * @param[out]    messages   Diagnostic message sink.
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

} // namespace Nodalys
