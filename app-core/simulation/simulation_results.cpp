/**
 * @file    simulation_results.cpp
 * @brief   What a simulation returns: the results of each analysis, and the outcome of a run with its messages.
 */

#include "simulation/simulation_results.h"

#include <cmath>
#include <complex>
#include <optional>
#include <vector>

namespace Core {

namespace {

std::optional<std::complex<double>>
ReadPortMatrix(const std::vector<std::optional<std::vector<std::complex<double>>>> &matrices,
               const std::size_t frequency,
               const int ports,
               const int row,
               const int column) {
    if (frequency >= matrices.size()) {
        return std::nullopt;
    }
    const std::optional<std::vector<std::complex<double>>> &matrix = matrices[frequency];
    if (!matrix) {
        return std::nullopt;
    }
    return (*matrix)[static_cast<std::size_t>((row - 1) * ports + (column - 1))];
}

} // namespace

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
    return Values[(frequency * ports + static_cast<std::size_t>(row - 1)) * ports
                  + static_cast<std::size_t>(column - 1)];
}

/**
 * @brief   Tells whether the backend gave the impedance and admittance matrices.
 * @return  True when there is one entry per frequency of each, absent or not; false for a result without them.
 */
bool SParameters::HasPortMatrices() const {
    return !Frequencies.empty() && ImpedanceMatrices.size() == Frequencies.size()
        && AdmittanceMatrices.size() == Frequencies.size();
}

/**
 * @brief   Reads one entry of the impedance matrix.
 * @param[in] frequency  Index of the frequency in Frequencies.
 * @param[in] row        Port the voltage is read at, from 1.
 * @param[in] column    Port the current enters, from 1.
 * @return  Z(row, column) in ohms; no value when the backend gave no impedance matrices, or the circuit has no finite
 *          one at this frequency.
 */
std::optional<std::complex<double>>
SParameters::GetImpedance(const std::size_t frequency, const int row, const int column) const {
    return ReadPortMatrix(ImpedanceMatrices, frequency, GetPortCount(), row, column);
}

/**
 * @brief   Reads one entry of the admittance matrix.
 * @param[in] frequency  Index of the frequency in Frequencies.
 * @param[in] row        Port the current is read at, from 1.
 * @param[in] column     Port the voltage is applied to, from 1.
 * @return  Y(row, column) in siemens; no value when the backend gave no admittance matrices, or the circuit has no
 *          finite one at this frequency.
 */
std::optional<std::complex<double>>
SParameters::GetAdmittance(const std::size_t frequency, const int row, const int column) const {
    return ReadPortMatrix(AdmittanceMatrices, frequency, GetPortCount(), row, column);
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
 * @return  Dimensionless THD ratio \f$\sqrt{\sum_{k=2}^{H} |X_k|^2} / |X_1|\f$, or 0 when no fundamental is present.
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
