/**
 * @file    simulation_results.cpp
 * @brief   What a simulation returns: the results of each analysis, and the outcome of a run with its messages.
 */

#include "simulation/simulation_results.h"

#include <cmath>

namespace Core {

/**
 * @brief   Tells how many ports the matrix has.
 * @return  The number of ports, 0 for an empty result.
 */
int SParameters::GetPortCount() const {
    return static_cast<int>(ReferenceOhms.size());
}

/**
 * @brief   Reads one entry of the scattering matrix.
 * @param[in] frequency  Index of the frequency in Frequencies.
 * @param[in] row        Port the wave leaves, from 1.
 * @param[in] column     Port the wave enters, from 1.
 * @return  The complex S(row, column).
 * @note    The indexes must be inside the result.
 */
std::complex<double> SParameters::Get(const std::size_t frequency, const int row, const int column) const {
    const auto ports = static_cast<std::size_t>(GetPortCount());
    return Values[(frequency * ports + static_cast<std::size_t>(row - 1)) * ports +
                  static_cast<std::size_t>(column - 1)];
}

/**
 * @brief   Tells whether the combination has output voltages.
 * @return  True when the simulator found an operating point; false when it did not, as for a latch that depends on
 *          its history.
 */
bool TruthTableRow::HasOperatingPoint() const {
    return !OutputVoltages.empty();
}

/**
 * @brief   Computes total harmonic distortion (THD) from harmonic phasors indexed 0..H.
 * @param[in] phasors  Complex phasors where index 0 is DC and index 1 is the fundamental.
 * @return  Dimensionless THD ratio $\sqrt{\sum_{k=2}^{H} |X_k|^2} / |X_1|$, or 0 when no fundamental is present.
 */
double ComputeHarmonicBalanceThd(const std::span<const std::complex<double>> phasors) {
    if (phasors.size() < 3) {
        return 0.0;
    }
    const double fundamental = std::abs(phasors[1]);
    if (fundamental <= 0.0) {
        return 0.0;
    }
    double harmonic_power = 0.0;
    for (std::size_t k = 2; k < phasors.size(); ++k) {
        harmonic_power += std::norm(phasors[k]);
    }
    return std::sqrt(harmonic_power) / fundamental;
}

} // namespace Core
