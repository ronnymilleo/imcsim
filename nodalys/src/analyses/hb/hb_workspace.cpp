/**
 * @file    hb_workspace.cpp
 * @brief   The state a harmonic balance run shares between its steps: sizes, linear matrices and scratch buffers.
 */

#include "analyses/hb/hb_workspace.h"

#include <numbers>

namespace Nodalys {

/**
 * @brief   Forgets the entries of the last matrix and sizes the traces for the next one.
 * @param[in] unknown_count  Rows and columns of the matrix.
 * @param[in] sample_count   Samples of the period, which is the length of each trace.
 */
void ActiveMatrixTraces::Reset(const int unknown_count, const std::size_t sample_count) {
    UnknownCount = unknown_count;
    SampleCount = sample_count;
    Pairs.clear();
    SlotByFlatIndex.assign(static_cast<std::size_t>(unknown_count) * static_cast<std::size_t>(unknown_count), -1);
    Traces.clear();
}

/**
 * @brief   Writes the non-zero entries of an MNA system matrix into their traces at one sample.
 * @param[in] system        The MNA system stamped at the sample.
 * @param[in] sample_index  Which sample of the period the system belongs to.
 * @note    Reset() must have been called first.
 */
void ActiveMatrixTraces::RecordMatrix(const MnaSystem &system, const std::size_t sample_index) {
    system.ForEachNonZero([this, sample_index](const int r, const int c, const double val) {
        const std::size_t flat =
            static_cast<std::size_t>(r) * static_cast<std::size_t>(UnknownCount) + static_cast<std::size_t>(c);
        int slot = SlotByFlatIndex[flat];
        if (slot < 0) {
            slot = static_cast<int>(Pairs.size());
            SlotByFlatIndex[flat] = slot;
            Pairs.emplace_back(r, c);
            Traces.resize(Pairs.size() * SampleCount, 0.0);
        }
        Traces[static_cast<std::size_t>(slot) * SampleCount + sample_index] = val;
    });
}

/**
 * @brief   Sizes the workspace for a circuit and a harmonic count.
 * @param[in] unknown_count    MNA unknowns of the circuit.
 * @param[in] harmonics        Highest harmonic of the expansion.
 * @param[in] sample_count     Samples of the period, a power of two.
 * @param[in] f0               Fundamental frequency in hertz.
 * @param[in] is_node_unknown  Whether each unknown is a node voltage, as opposed to a branch current.
 */
HbWorkspace::HbWorkspace(const int unknown_count,
                         const int harmonics,
                         const std::size_t sample_count,
                         const double f0,
                         std::vector<bool> is_node_unknown)
    : UnknownCount(unknown_count), Harmonics(harmonics), CoeffsPerUnknown(2 * harmonics + 1), SampleCount(sample_count),
      Omega0(2.0 * std::numbers::pi * f0), Plan(sample_count), IsNodeUnknown(is_node_unknown), GLin(is_node_unknown),
      CLin(is_node_unknown),
      BaseRhsHarmonics(static_cast<std::size_t>(unknown_count) * static_cast<std::size_t>(2 * harmonics + 1), 0.0),
      StateSamples(sample_count, std::vector<double>(static_cast<std::size_t>(unknown_count), 0.0)),
      FnlTraces(static_cast<std::size_t>(unknown_count) * sample_count, 0.0),
      QnlTraces(static_cast<std::size_t>(unknown_count) * sample_count, 0.0), SampleG(is_node_unknown),
      SampleC(std::move(is_node_unknown)) {
}

/**
 * @brief   Builds the workspace of a circuit from the configuration of the analysis.
 * @param[in] compiled      The circuit, whose MNA system tells which unknowns are nodes.
 * @param[in] config        Fundamental frequency, harmonic count and the stop callback.
 * @param[in] sample_count  Samples of the period, already validated.
 * @return  The workspace, with the stop callback of the configuration installed.
 */
HbWorkspace
MakeHbWorkspace(const CompiledCircuit &compiled, const HarmonicBalanceConfig &config, const std::size_t sample_count) {
    const int unknown_count = compiled.System->GetSize();
    std::vector<bool> node_flags(static_cast<std::size_t>(unknown_count), false);
    for (int i = 0; i < unknown_count; ++i) {
        node_flags[static_cast<std::size_t>(i)] = compiled.System->IsNodeUnknown(i);
    }

    HbWorkspace ws(unknown_count, config.Harmonics, sample_count, config.FundamentalFrequency, std::move(node_flags));
    ws.ShouldStop = config.ShouldStop;
    ws.Nodesets = compiled.Nodesets;
    return ws;
}

} // namespace Nodalys
