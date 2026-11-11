/**
 * @file    fft.cpp
 * @brief   Radix-2 Cooley-Tukey Fast Fourier Transform and real harmonic cosine/sine conversions for Harmonic Balance.
 */

#include "linear/fft.h"

#include <bit>
#include <cmath>
#include <numbers>
#include <utility>

namespace nodalys {

/**
 * @brief   Constructs an FFT plan for a power-of-two transform size.
 * @param[in] size  Transform length M; if zero or not a power of two, `IsValid()` returns false.
 */
FftPlan::FftPlan(const std::size_t size) {
    if (!IsPowerOfTwo(size)) {
        return;
    }
    m_Size = size;
    m_BitRev.resize(size);

    const unsigned int bits = static_cast<unsigned int>(std::countr_zero(size));
    for (std::size_t i = 0; i < size; ++i) {
        std::size_t reversed = 0;
        for (unsigned int bit = 0; bit < bits; ++bit) {
            if ((i & (std::size_t{1} << bit)) != 0) {
                reversed |= std::size_t{1} << (bits - 1U - bit);
            }
        }
        m_BitRev[i] = reversed;
    }

    const std::size_t half = size / 2;
    m_Twiddles.resize(half);
    for (std::size_t k = 0; k < half; ++k) {
        const double angle = -2.0 * std::numbers::pi * static_cast<double>(k) / static_cast<double>(size);
        m_Twiddles[k] = std::complex<double>(std::cos(angle), std::sin(angle));
    }
}

/**
 * @brief   Computes the unnormalized in-place forward DFT $X[k] = \sum_{m=0}^{M-1} x[m] e^{-j 2\pi k m / M}$.
 * @param[in,out] data  Vector of length `Size()`; ignored when the plan is invalid or size mismatches.
 */
void FftPlan::Forward(std::vector<std::complex<double>> &data) const {
    if (!IsValid() || data.size() != m_Size) {
        return;
    }
    Transform(data, false);
}

/**
 * @brief   Computes the normalized in-place inverse DFT $x[m] = \frac{1}{M} \sum_{k=0}^{M-1} X[k] e^{+j 2\pi k m / M}$.
 * @param[in,out] data  Vector of length `Size()`; ignored when the plan is invalid or size mismatches.
 */
void FftPlan::Inverse(std::vector<std::complex<double>> &data) const {
    if (!IsValid() || data.size() != m_Size) {
        return;
    }
    Transform(data, true);
    const double inv_size = 1.0 / static_cast<double>(m_Size);
    for (std::complex<double> &value : data) {
        value *= inv_size;
    }
}

/**
 * @brief   Converts real time-domain samples over one period into real cosine/sine harmonic coefficients.
 * @param[in]  samples    Time-domain samples of length `Size()` taken at $t_m = m T_0 / M$.
 * @param[in]  harmonics  Highest harmonic index $H \ge 0$ (must satisfy $2H < M$).
 * @param[out] coeffs     Output vector `[a0, a1, b1, ..., aH, bH]` of size $2H + 1$.
 * @param[out] scratch    Reusable complex buffer resized to `Size()`.
 * @return  False if the plan is invalid, `samples.size() != Size()`, `harmonics < 0`, or $2H \ge M$.
 */
bool FftPlan::RealToHarmonics(const std::vector<double> &samples, const int harmonics, std::vector<double> &coeffs,
                              std::vector<std::complex<double>> &scratch) const {
    if (!IsValid() || samples.size() != m_Size || harmonics < 0 || m_Size <= 2U * static_cast<std::size_t>(harmonics)) {
        return false;
    }

    scratch.resize(m_Size);
    for (std::size_t m = 0; m < m_Size; ++m) {
        scratch[m] = std::complex<double>(samples[m], 0.0);
    }
    Transform(scratch, false);

    const double inv_size = 1.0 / static_cast<double>(m_Size);
    coeffs.resize(static_cast<std::size_t>(2 * harmonics + 1));
    coeffs[0] = scratch[0].real() * inv_size;
    for (int k = 1; k <= harmonics; ++k) {
        const std::complex<double> bin = scratch[static_cast<std::size_t>(k)] * (2.0 * inv_size);
        coeffs[static_cast<std::size_t>(2 * k - 1)] = bin.real();
        coeffs[static_cast<std::size_t>(2 * k)] = -bin.imag();
    }
    return true;
}

/**
 * @brief   Computes the $1/M$-normalized DFT spectrum of real samples for harmonic conversion matrices.
 * @param[in]  samples   Real samples of length `Size()`.
 * @param[out] spectrum  Complex spectrum $\hat{g}_k = \frac{1}{M}\text{FFT}(g)[k]$ of length `Size()`.
 * @return  False if the plan is invalid or `samples.size() != Size()`.
 */
bool FftPlan::RealToNormalizedSpectrum(const std::vector<double> &samples,
                                       std::vector<std::complex<double>> &spectrum) const {
    if (!IsValid() || samples.size() != m_Size) {
        return false;
    }
    spectrum.resize(m_Size);
    for (std::size_t m = 0; m < m_Size; ++m) {
        spectrum[m] = std::complex<double>(samples[m], 0.0);
    }
    Transform(spectrum, false);
    const double inv_size = 1.0 / static_cast<double>(m_Size);
    for (std::complex<double> &bin : spectrum) {
        bin *= inv_size;
    }
    return true;
}

/**
 * @brief   Reconstructs real time-domain samples over one period from cosine/sine harmonic coefficients.
 * @param[in]  coeffs   Harmonic coefficients `[a0, a1, b1, ..., aH, bH]` of odd size $2H + 1$ with $2H < M$.
 * @param[out] samples  Output real samples of length `Size()`.
 * @param[out] scratch  Reusable complex buffer resized to `Size()`.
 * @return  False if the plan is invalid, `coeffs` size is even, or $2H \ge M$.
 */
bool FftPlan::HarmonicsToReal(const std::vector<double> &coeffs, std::vector<double> &samples,
                              std::vector<std::complex<double>> &scratch) const {
    if (!IsValid() || coeffs.empty() || (coeffs.size() % 2U) == 0U) {
        return false;
    }
    const std::size_t harmonics = (coeffs.size() - 1U) / 2U;
    if (m_Size <= 2U * harmonics) {
        return false;
    }

    scratch.assign(m_Size, std::complex<double>{0.0, 0.0});
    scratch[0] = std::complex<double>(coeffs[0], 0.0);
    for (std::size_t k = 1; k <= harmonics; ++k) {
        const double ak = coeffs[2U * k - 1U];
        const double bk = coeffs[2U * k];
        scratch[k] = std::complex<double>(0.5 * ak, -0.5 * bk);
        scratch[m_Size - k] = std::complex<double>(0.5 * ak, 0.5 * bk);
    }

    Transform(scratch, true);

    samples.resize(m_Size);
    for (std::size_t m = 0; m < m_Size; ++m) {
        samples[m] = scratch[m].real();
    }
    return true;
}

void FftPlan::Transform(std::vector<std::complex<double>> &data, const bool inverse) const {
    if (m_Size <= 1U) {
        return;
    }
    for (std::size_t i = 0; i < m_Size; ++i) {
        const std::size_t j = m_BitRev[i];
        if (i < j) {
            std::swap(data[i], data[j]);
        }
    }

    for (std::size_t len = 2; len <= m_Size; len <<= 1U) {
        const std::size_t half = len >> 1U;
        const std::size_t twiddle_step = m_Size / len;
        for (std::size_t start = 0; start < m_Size; start += len) {
            for (std::size_t k = 0; k < half; ++k) {
                std::complex<double> twiddle = m_Twiddles[k * twiddle_step];
                if (inverse) {
                    twiddle = std::conj(twiddle);
                }
                const std::complex<double> odd = twiddle * data[start + k + half];
                const std::complex<double> even = data[start + k];
                data[start + k] = even + odd;
                data[start + k + half] = even - odd;
            }
        }
    }
}

/**
 * @brief   Checks whether an integer is a nonzero power of two.
 * @param[in] n  Value to test.
 * @return  True if `n` equals $2^k$ for some $k \ge 0$.
 */
bool IsPowerOfTwo(const std::size_t n) {
    return n != 0 && (n & (n - 1U)) == 0;
}

/**
 * @brief   Returns the smallest power of two not less than `n`.
 * @param[in] n  Minimum desired size.
 * @return  `1` for `n == 0`, otherwise $2^{\lceil \log_2 n \rceil}$.
 */
std::size_t NextPowerOfTwo(const std::size_t n) {
    return n <= 1U ? 1U : std::bit_ceil(n);
}

/**
 * @brief   Computes the unnormalized in-place forward radix-2 FFT.
 * @param[in,out] data  Input/output sequence whose length must be a power of two.
 * @return  False if `data.size()` is not a power of two.
 */
bool ComputeFft(std::vector<std::complex<double>> &data) {
    const FftPlan plan(data.size());
    if (!plan.IsValid()) {
        return false;
    }
    plan.Forward(data);
    return true;
}

/**
 * @brief   Computes the normalized in-place inverse radix-2 FFT.
 * @param[in,out] data  Input/output spectrum whose length must be a power of two.
 * @return  False if `data.size()` is not a power of two.
 */
bool ComputeIfft(std::vector<std::complex<double>> &data) {
    const FftPlan plan(data.size());
    if (!plan.IsValid()) {
        return false;
    }
    plan.Inverse(data);
    return true;
}

/**
 * @brief   Converts real time-domain samples over one period to cosine/sine harmonic coefficients.
 * @param[in]  samples    Time-domain samples whose length is a power of two greater than `2 * harmonics`.
 * @param[in]  harmonics  Highest harmonic index $H \ge 0$.
 * @param[out] coeffs     Output coefficients `[a0, a1, b1, ..., aH, bH]`.
 * @return  False on invalid sizes or negative harmonic count.
 */
bool RealToHarmonics(const std::vector<double> &samples, const int harmonics, std::vector<double> &coeffs) {
    const FftPlan plan(samples.size());
    std::vector<std::complex<double>> scratch;
    return plan.RealToHarmonics(samples, harmonics, coeffs, scratch);
}

/**
 * @brief   Computes the $1/M$-normalized DFT spectrum of real samples.
 * @param[in]  samples   Real samples whose length is a power of two.
 * @param[out] spectrum  Output normalized spectrum.
 * @return  False if `samples.size()` is not a power of two.
 */
bool RealToNormalizedSpectrum(const std::vector<double> &samples, std::vector<std::complex<double>> &spectrum) {
    const FftPlan plan(samples.size());
    return plan.RealToNormalizedSpectrum(samples, spectrum);
}

/**
 * @brief   Reconstructs real time-domain samples over one period from cosine/sine harmonic coefficients.
 * @param[in]  coeffs        Harmonic coefficients `[a0, a1, b1, ..., aH, bH]`.
 * @param[in]  sample_count  Number of samples (power of two greater than `2 * H`).
 * @param[out] samples       Output real samples.
 * @return  False on invalid sizes.
 */
bool HarmonicsToReal(const std::vector<double> &coeffs, const std::size_t sample_count, std::vector<double> &samples) {
    const FftPlan plan(sample_count);
    std::vector<std::complex<double>> scratch;
    return plan.HarmonicsToReal(coeffs, samples, scratch);
}

} // namespace nodalys
