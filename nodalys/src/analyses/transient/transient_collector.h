/**
 * @file    transient_collector.h
 * @brief   Output grid interpolation, corner recording, chunk streaming, and live-change dispatch for transient runs.
 */

#ifndef NODALYS_TRANSIENT_COLLECTOR_H
#define NODALYS_TRANSIENT_COLLECTOR_H

#include "compiler/compiled_circuit.h"
#include "nodalys/analyses.h"
#include "nodalys/results.h"
#include <chrono>
#include <cstddef>
#include <vector>

namespace Nodalys {

/// Relative margin within which a time counts as the stop time or as an output point.
inline constexpr double TRANSIENT_TIME_TOLERANCE = 1e-12;
/// Relative margin within which the last output point counts as the stop time.
inline constexpr double TRANSIENT_STOP_POINT_TOLERANCE = 1e-9;

/**
 * @struct  ChunkPublisher
 * @brief   Tracks when solved transient points were last streamed to the RunMonitor and how many points were sent.
 */
struct ChunkPublisher {
    const RunMonitor &Monitor;
    std::chrono::steady_clock::time_point LastSend;
    double ChunkEnd = 0.0;
    std::size_t Sent = 0;
};

/**
 * @struct  SolutionPoint
 * @brief   The solution of the circuit at one instant, with the currents read from it.
 */
struct SolutionPoint {
    double Time;
    const std::vector<double> &X;
    const std::vector<double> &Currents;
};

/**
 * @class   TransientCollector
 * @brief   Interpolates adaptive transient steps onto the uniform output grid while preserving waveform corners.
 */
class TransientCollector {
public:
    TransientCollector(const CompiledCircuit &compiled, double output_step);

    void Append(double time, const std::vector<double> &x, const std::vector<double> &currents);
    void AppendSegment(const SolutionPoint &start, const SolutionPoint &end);
    void AppendCorner(double time, const std::vector<double> &x, const std::vector<double> &currents);
    void AppendFinalPointIfNeeded(double stop_time, const std::vector<double> &x, const std::vector<double> &currents);

    /**
     * @brief   Returns the number of recorded output time points so far.
     * @return  Recorded point count.
     */
    [[nodiscard]] std::size_t PointCount() const { return m_Result.Times.size(); }

    /**
     * @brief   Returns a read-only view of the accumulated transient result.
     * @return  Const reference to the result.
     */
    [[nodiscard]] const TransientResult &View() const { return m_Result; }

    /**
     * @brief   Moves the accumulated transient result out of the collector.
     * @return  Completed TransientResult.
     */
    [[nodiscard]] TransientResult Take() { return std::move(m_Result); }

private:
    const CompiledCircuit &m_Compiled;
    double m_OutputStep;
    long m_NextGrid = 1;
    TransientResult m_Result;
    std::vector<double> m_Scratch;
    std::vector<double> m_CurrentsScratch;
};

// Chunk streaming and live changes
void PublishChunk(const TransientResult &result, ChunkPublisher &publisher, double progress);
[[nodiscard]] bool IsChunkDue(const ChunkPublisher &publisher, double time, std::size_t point_count);
[[nodiscard]] bool ServeChunk(CompiledCircuit &compiled,
                              ChunkPublisher &publisher,
                              const TransientResult &result,
                              double time,
                              double stop_time,
                              std::vector<DiagnosticMessage> *messages);

} // namespace Nodalys

#endif // NODALYS_TRANSIENT_COLLECTOR_H
