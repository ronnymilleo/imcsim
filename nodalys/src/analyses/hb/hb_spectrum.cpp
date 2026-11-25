/**
 * @file    hb_spectrum.cpp
 * @brief   Moves harmonic balance states between harmonics, phasors and time samples, and their conversion blocks.
 */

#include "analyses/hb/hb_spectrum.h"

#include <algorithm>

namespace Nodalys {

/**
 * @brief   Turns the harmonics of every unknown into its time samples over one period.
 * @param[in,out] ws       Workspace whose FFT scratch buffers are used.
 * @param[in]     coeffs   Coefficients of all unknowns, `[unknown * CoeffsPerUnknown + coefficient]`.
 * @param[out]    samples  `[sample][unknown]`, already sized.
 */
void UnpackCoefficientsToSamples(HbWorkspace &ws,
                                 const std::vector<double> &coeffs,
                                 std::vector<std::vector<double>> &samples) {
    ws.CoeffsScratch.resize(static_cast<std::size_t>(ws.CoeffsPerUnknown));
    for (int u = 0; u < ws.UnknownCount; ++u) {
        const std::size_t base = static_cast<std::size_t>(u) * static_cast<std::size_t>(ws.CoeffsPerUnknown);
        std::copy_n(coeffs.data() + base, static_cast<std::size_t>(ws.CoeffsPerUnknown), ws.CoeffsScratch.data());
        (void)ws.Plan.HarmonicsToReal(ws.CoeffsScratch, ws.TimeSeriesScratch, ws.FftScratch);
        for (std::size_t m = 0; m < ws.SampleCount; ++m) {
            samples[m][static_cast<std::size_t>(u)] = ws.TimeSeriesScratch[m];
        }
    }
}

/**
 * @brief   Turns the time samples of every unknown into its harmonics.
 * @param[in,out] ws       Workspace whose FFT scratch buffers are used.
 * @param[in]     samples  `[sample][unknown]`.
 * @param[out]    coeffs   Coefficients of all unknowns, `[unknown * CoeffsPerUnknown + coefficient]`.
 */
void PackSamplesToCoefficients(HbWorkspace &ws,
                               const std::vector<std::vector<double>> &samples,
                               std::vector<double> &coeffs) {
    coeffs.resize(static_cast<std::size_t>(ws.UnknownCount) * static_cast<std::size_t>(ws.CoeffsPerUnknown));
    ws.TimeSeriesScratch.resize(ws.SampleCount);
    for (int u = 0; u < ws.UnknownCount; ++u) {
        for (std::size_t m = 0; m < ws.SampleCount; ++m) {
            ws.TimeSeriesScratch[m] = samples[m][static_cast<std::size_t>(u)];
        }
        (void)ws.Plan.RealToHarmonics(ws.TimeSeriesScratch, ws.Harmonics, ws.CoeffsScratch, ws.FftScratch);
        const std::size_t base = static_cast<std::size_t>(u) * static_cast<std::size_t>(ws.CoeffsPerUnknown);
        std::copy_n(ws.CoeffsScratch.data(), static_cast<std::size_t>(ws.CoeffsPerUnknown), coeffs.data() + base);
    }
}

/**
 * @brief   Reads the coefficients of one unknown as peak phasors.
 * @param[in] coeffs  The `[a0, a1, b1, ..., aH, bH]` of the unknown.
 * @return  The phasors `[a0, a1 - j b1, ..., aH - j bH]`, one more than the harmonics.
 */
std::vector<std::complex<double>> CoefficientsToPhasors(const std::span<const double> coeffs) {
    const std::size_t harmonics = (coeffs.size() - 1) / 2;
    std::vector<std::complex<double>> phasors(harmonics + 1);
    phasors[0] = std::complex<double>(coeffs[0], 0.0);
    for (std::size_t k = 1; k <= harmonics; ++k) {
        phasors[k] = std::complex<double>(coeffs[2 * k - 1], -coeffs[2 * k]);
    }
    return phasors;
}

/**
 * @brief   Writes peak phasors as the coefficients of a cosine and sine series.
 * @param[in] phasors  The phasors `[X0, X1, ..., XH]` of one unknown.
 * @return  The coefficients `[Re X0, Re X1, -Im X1, ..., Re XH, -Im XH]`.
 */
std::vector<double> PhasorsToCoefficients(const std::span<const std::complex<double>> phasors) {
    std::vector<double> coeffs(2 * phasors.size() - 1);
    coeffs[0] = phasors[0].real();
    for (std::size_t k = 1; k < phasors.size(); ++k) {
        coeffs[2 * k - 1] = phasors[k].real();
        coeffs[2 * k] = -phasors[k].imag();
    }
    return coeffs;
}

namespace {

void StampHarmonicConversionCell(DenseMatrix &jacobian,
                                 const std::size_t cos_row,
                                 const std::size_t sin_row,
                                 const std::size_t cos_col,
                                 const std::size_t sin_col,
                                 const std::complex<double> &g_minus,
                                 const std::complex<double> &g_plus,
                                 const double kw,
                                 const bool apply_omega) {
    const double cc = g_minus.real() + g_plus.real();
    const double cs = g_minus.imag() - g_plus.imag();
    const double sc = -(g_minus.imag() + g_plus.imag());
    const double ss = g_minus.real() - g_plus.real();
    if (!apply_omega) {
        jacobian.Add(cos_row, cos_col, cc);
        jacobian.Add(cos_row, sin_col, cs);
        jacobian.Add(sin_row, cos_col, sc);
        jacobian.Add(sin_row, sin_col, ss);
    } else {
        jacobian.Add(cos_row, cos_col, kw * sc);
        jacobian.Add(cos_row, sin_col, kw * ss);
        jacobian.Add(sin_row, cos_col, -kw * cc);
        jacobian.Add(sin_row, sin_col, -kw * cs);
    }
}

} // namespace

/**
 * @brief   Stamps a (2H+1) x (2H+1) harmonic conversion matrix block into the real Jacobian from a
 *          1/M-normalized time-domain entry spectrum \f$\hat{g}_n\f$.
 * @details When `apply_omega` is true, pre-multiplies by the time-derivative operator \f$\Omega\f$ for nonlinear
 *          charges.
 * @param[in,out] jacobian     Real Jacobian of the balance, which receives the block.
 * @param[in]     row_offset   First row of the block, the first coefficient of the unknown the entry belongs to.
 * @param[in]     col_offset   First column of the block, the first coefficient of the unknown it depends on.
 * @param[in]     harmonics    Number of harmonics H above DC.
 * @param[in]     omega0       Angular frequency of the fundamental in radians per second.
 * @param[in]     spectrum     Spectrum of the entry over the period, at least H + 1 values from DC up.
 * @param[in]     apply_omega  Whether the entry is a charge, whose block is multiplied by the derivative operator.
 */
void StampConversionBlock(DenseMatrix &jacobian,
                          const std::size_t row_offset,
                          const std::size_t col_offset,
                          const int harmonics,
                          const double omega0,
                          const std::vector<std::complex<double>> &spectrum,
                          const bool apply_omega) {
    if (!apply_omega) {
        jacobian.Add(row_offset, col_offset, spectrum[0].real());
        for (int l = 1; l <= harmonics; ++l) {
            const std::complex<double> &gl = spectrum[static_cast<std::size_t>(l)];
            jacobian.Add(row_offset, col_offset + static_cast<std::size_t>(2 * l - 1), gl.real());
            jacobian.Add(row_offset, col_offset + static_cast<std::size_t>(2 * l), -gl.imag());
        }
    }

    for (int k = 1; k <= harmonics; ++k) {
        const std::size_t cos_row = row_offset + static_cast<std::size_t>(2 * k - 1);
        const std::size_t sin_row = row_offset + static_cast<std::size_t>(2 * k);
        const double kw = static_cast<double>(k) * omega0;
        const std::complex<double> &gk = spectrum[static_cast<std::size_t>(k)];
        jacobian.Add(cos_row, col_offset, apply_omega ? kw * (-2.0 * gk.imag()) : 2.0 * gk.real());
        jacobian.Add(sin_row, col_offset, apply_omega ? -kw * (2.0 * gk.real()) : -2.0 * gk.imag());

        for (int l = 1; l <= harmonics; ++l) {
            const std::complex<double> g_minus = (k >= l) ? spectrum[static_cast<std::size_t>(k - l)]
                                                          : std::conj(spectrum[static_cast<std::size_t>(l - k)]);
            StampHarmonicConversionCell(jacobian,
                                        cos_row,
                                        sin_row,
                                        col_offset + static_cast<std::size_t>(2 * l - 1),
                                        col_offset + static_cast<std::size_t>(2 * l),
                                        g_minus,
                                        spectrum[static_cast<std::size_t>(k + l)],
                                        kw,
                                        apply_omega);
        }
    }
}

} // namespace Nodalys
