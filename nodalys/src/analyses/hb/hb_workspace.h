/**
 * @file    hb_workspace.h
 * @brief   The state a harmonic balance run shares between its steps: sizes, linear matrices and scratch buffers.
 */

#ifndef NODALYS_HB_WORKSPACE_H
#define NODALYS_HB_WORKSPACE_H

#include "compiler/compiled_circuit.h"
#include "devices/device.h"
#include "linear/fft.h"
#include "mna/mna_system.h"
#include "nodalys/analyses.h"
#include <complex>
#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace Nodalys {

/**
 * @struct  ActiveMatrixTraces
 * @brief   The time traces of the entries of a matrix that are not zero at some sample.
 * @details The Jacobian of the nonlinear devices is sparse, so only the entries that ever hold a value are kept, each
 *          as one trace over the samples of the period.
 */
struct ActiveMatrixTraces {
    std::vector<std::pair<int, int>> Pairs;
    std::vector<int> SlotByFlatIndex;
    /// Flat, `[pair_index * SampleCount + sample]`.
    std::vector<double> Traces;
    int UnknownCount = 0;
    std::size_t SampleCount = 0;

    // Recording
    void Reset(int unknown_count, std::size_t sample_count);
    void RecordMatrix(const MnaSystem &system, std::size_t sample_index);
};

/**
 * @struct  HbWorkspace
 * @brief   Everything the steps of a harmonic balance share, so a Newton iteration allocates nothing.
 */
struct HbWorkspace {
    /// Asked before each Newton iteration, or empty.
    std::function<bool()> ShouldStop;
    /// Set once ShouldStop() asked for a stop, so the continuation strategies end too.
    bool IsStopped = false;
    /// True when the last evaluation of the residual met a device that has no value at the state, such as a behavioral
    /// source whose expression is out of range; the state it gave cannot be a solution.
    bool IsFaulty = false;
    /// The fault of a device at some evaluation, for the message when the balance does not converge.
    std::string Fault;
    int UnknownCount = 0;
    int Harmonics = 0;
    int CoeffsPerUnknown = 0;
    std::size_t SampleCount = 0;
    double Omega0 = 0.0;
    FftPlan Plan;
    std::vector<bool> IsNodeUnknown;

    MnaSystem GLin;
    MnaSystem CLin;
    std::vector<Device *> NonlinearDevices;
    /// The nodesets, stamped in the pulled pass of the DC bias only.
    std::vector<NodeVoltageDirective> Nodesets;
    /// The excitation B, in `[unknown * CoeffsPerUnknown + coefficient]`.
    std::vector<double> BaseRhsHarmonics;

    // Scratch buffers reused across Newton iterations
    std::vector<std::complex<double>> FftScratch;
    std::vector<double> CoeffsScratch;
    std::vector<double> TimeSeriesScratch;
    std::vector<std::complex<double>> SpectrumScratch;
    /// The state at each sample, `[sample][unknown]`.
    std::vector<std::vector<double>> StateSamples;
    /// The nonlinear currents, `[unknown * SampleCount + sample]`.
    std::vector<double> FnlTraces;
    /// The nonlinear charges, `[unknown * SampleCount + sample]`.
    std::vector<double> QnlTraces;
    ActiveMatrixTraces GTraces;
    ActiveMatrixTraces CTraces;
    MnaSystem SampleG;
    MnaSystem SampleC;

    HbWorkspace(int unknown_count,
                int harmonics,
                std::size_t sample_count,
                double f0,
                std::vector<bool> is_node_unknown);

    /**
     * @brief   Position of one coefficient of one unknown in the packed harmonic vectors.
     * @details Inline because every loop over the coefficients calls it.
     */
    std::size_t Index(const int unknown, const int coeff_slot) const {
        return static_cast<std::size_t>(unknown) * static_cast<std::size_t>(CoeffsPerUnknown)
             + static_cast<std::size_t>(coeff_slot);
    }
};

// Construction
HbWorkspace
MakeHbWorkspace(const CompiledCircuit &compiled, const HarmonicBalanceConfig &config, std::size_t sample_count);

} // namespace Nodalys

#endif // NODALYS_HB_WORKSPACE_H
