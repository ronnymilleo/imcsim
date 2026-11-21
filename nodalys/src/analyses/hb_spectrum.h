/**
 * @file    hb_spectrum.h
 * @brief   Moves harmonic balance states between harmonics, phasors and time samples, and their conversion blocks.
 * @details The unknowns are kept as the coefficients of a cosine and sine series, `[a0, a1, b1, a2, b2, ...]`; a peak
 *          phasor is `a - jb`. The nonlinear devices work on time samples, so the state goes through the FFT both ways.
 */

#ifndef NODALYS_HB_SPECTRUM_H
#define NODALYS_HB_SPECTRUM_H

#include "analyses/hb_workspace.h"
#include "linear/dense_matrix.h"
#include <complex>
#include <cstddef>
#include <span>
#include <vector>

namespace Nodalys {

// Time samples
void UnpackCoefficientsToSamples(HbWorkspace &ws,
                                 const std::vector<double> &coeffs,
                                 std::vector<std::vector<double>> &samples);
void PackSamplesToCoefficients(HbWorkspace &ws,
                               const std::vector<std::vector<double>> &samples,
                               std::vector<double> &coeffs);

// Phasors
std::vector<std::complex<double>> CoefficientsToPhasors(std::span<const double> coeffs);
std::vector<double> PhasorsToCoefficients(std::span<const std::complex<double>> phasors);

// Conversion matrix
void StampConversionBlock(DenseMatrix &jacobian,
                          std::size_t row_offset,
                          std::size_t col_offset,
                          int harmonics,
                          double omega0,
                          const std::vector<std::complex<double>> &spectrum,
                          bool apply_omega);

} // namespace Nodalys

#endif // NODALYS_HB_SPECTRUM_H
