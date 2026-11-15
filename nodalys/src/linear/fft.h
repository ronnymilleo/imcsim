/**
 * @file    fft.h
 * @brief   Radix-2 Cooley-Tukey Fast Fourier Transform and real harmonic cosine/sine conversions for Harmonic Balance.
 */

#ifndef NODALYS_FFT_H
#define NODALYS_FFT_H

#include <complex>
#include <cstddef>
#include <vector>

namespace Nodalys {

/**
 * @class   FftPlan
 * @brief   Precomputed radix-2 FFT plan with bit-reversal table and twiddle factors for a fixed power-of-two size.
 * @details Thread-safe and allocation-free when callers pass a reusable complex scratch buffer.
 */
class FftPlan {
public:
    explicit FftPlan(std::size_t size);

    bool IsValid() const { return m_Size != 0; }
    std::size_t GetSize() const { return m_Size; }

    void Forward(std::vector<std::complex<double>> &data) const;
    void Inverse(std::vector<std::complex<double>> &data) const;

    [[nodiscard]] bool RealToHarmonics(const std::vector<double> &samples, int harmonics, std::vector<double> &coeffs,
                                       std::vector<std::complex<double>> &scratch) const;

    [[nodiscard]] bool RealToNormalizedSpectrum(const std::vector<double> &samples,
                                                std::vector<std::complex<double>> &spectrum) const;

    [[nodiscard]] bool HarmonicsToReal(const std::vector<double> &coeffs, std::vector<double> &samples,
                                       std::vector<std::complex<double>> &scratch) const;

private:
    void Transform(std::vector<std::complex<double>> &data, bool inverse) const;

    std::size_t m_Size = 0;
    std::vector<std::size_t> m_BitRev;
    std::vector<std::complex<double>> m_Twiddles;
};

// Size Helpers
[[nodiscard]] bool IsPowerOfTwo(std::size_t n);
[[nodiscard]] std::size_t NextPowerOfTwo(std::size_t n);

// Convenience Transforms
[[nodiscard]] bool ComputeFft(std::vector<std::complex<double>> &data);
[[nodiscard]] bool ComputeIfft(std::vector<std::complex<double>> &data);

[[nodiscard]] bool RealToHarmonics(const std::vector<double> &samples, int harmonics, std::vector<double> &coeffs);
[[nodiscard]] bool RealToNormalizedSpectrum(const std::vector<double> &samples,
                                            std::vector<std::complex<double>> &spectrum);
[[nodiscard]] bool HarmonicsToReal(const std::vector<double> &coeffs, std::size_t sample_count,
                                   std::vector<double> &samples);

} // namespace Nodalys

#endif // NODALYS_FFT_H
