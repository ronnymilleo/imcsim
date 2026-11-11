/**
 * @file    fft_tests.cpp
 * @brief   Unit tests for Nodalys radix-2 FFT and real harmonic cosine/sine conversions.
 */

#include "linear/fft.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

namespace {

using Catch::Matchers::WithinAbs;

std::vector<std::complex<double>> NaiveDft(const std::vector<std::complex<double>> &input) {
    const std::size_t n = input.size();
    std::vector<std::complex<double>> out(n, {0.0, 0.0});
    for (std::size_t k = 0; k < n; ++k) {
        for (std::size_t m = 0; m < n; ++m) {
            const double angle = -2.0 * std::numbers::pi * static_cast<double>(k * m) / static_cast<double>(n);
            out[k] += input[m] * std::complex<double>(std::cos(angle), std::sin(angle));
        }
    }
    return out;
}

TEST_CASE("Power-of-two helpers handle edge cases", "[linear][fft]") {
    CHECK_FALSE(nodalys::IsPowerOfTwo(0));
    CHECK(nodalys::IsPowerOfTwo(1));
    CHECK(nodalys::IsPowerOfTwo(2));
    CHECK_FALSE(nodalys::IsPowerOfTwo(3));
    CHECK(nodalys::IsPowerOfTwo(64));
    CHECK_FALSE(nodalys::IsPowerOfTwo(65));

    CHECK(nodalys::NextPowerOfTwo(0) == 1);
    CHECK(nodalys::NextPowerOfTwo(1) == 1);
    CHECK(nodalys::NextPowerOfTwo(17) == 32);
    CHECK(nodalys::NextPowerOfTwo(64) == 64);
}

TEST_CASE("Radix-2 FFT matches naive DFT and round-trips via IFFT", "[linear][fft]") {
    for (const std::size_t n :
         {std::size_t{2}, std::size_t{4}, std::size_t{16}, std::size_t{64}, std::size_t{256}, std::size_t{1024}}) {
        std::vector<std::complex<double>> signal(n);
        for (std::size_t m = 0; m < n; ++m) {
            const double t = static_cast<double>(m) / static_cast<double>(n);
            signal[m] = std::complex<double>(std::sin(2.0 * std::numbers::pi * 3.0 * t) + 0.5 * std::cos(t),
                                             -0.3 * std::cos(2.0 * std::numbers::pi * 5.0 * t));
        }

        const std::vector<std::complex<double>> expected = NaiveDft(signal);
        std::vector<std::complex<double>> spectrum = signal;
        REQUIRE(nodalys::ComputeFft(spectrum));

        const double tol = 1e-12 * static_cast<double>(n);
        for (std::size_t k = 0; k < n; ++k) {
            CHECK_THAT(spectrum[k].real(), WithinAbs(expected[k].real(), tol));
            CHECK_THAT(spectrum[k].imag(), WithinAbs(expected[k].imag(), tol));
        }

        REQUIRE(nodalys::ComputeIfft(spectrum));
        for (std::size_t m = 0; m < n; ++m) {
            CHECK_THAT(spectrum[m].real(), WithinAbs(signal[m].real(), 1e-12));
            CHECK_THAT(spectrum[m].imag(), WithinAbs(signal[m].imag(), 1e-12));
        }
    }
}

TEST_CASE("Non-power-of-two inputs are rejected", "[linear][fft]") {
    std::vector<std::complex<double>> empty;
    std::vector<std::complex<double>> six(6, {1.0, 0.0});
    CHECK_FALSE(nodalys::ComputeFft(empty));
    CHECK_FALSE(nodalys::ComputeIfft(six));

    std::vector<double> samples(8, 0.0);
    std::vector<double> coeffs;
    // M = 8 cannot resolve H = 4 without aliasing at Nyquist (requires M > 2 * H)
    CHECK_FALSE(nodalys::RealToHarmonics(samples, 4, coeffs));
    CHECK_FALSE(nodalys::RealToHarmonics(samples, -1, coeffs));
}

TEST_CASE("Real cosine/sine harmonic transforms preserve exact trigonometric signals", "[linear][fft]") {
    constexpr std::size_t M = 64;
    constexpr int H = 8;
    // x(t) = 1.25 + 2.0*cos(w*t) - 1.5*sin(w*t) + 0.7*cos(3*w*t) + 0.4*sin(7*w*t)
    const std::vector<double> expected_coeffs = {
        1.25,       // DC
        2.0,  -1.5, // k = 1
        0.0,  0.0,  // k = 2
        0.7,  0.0,  // k = 3
        0.0,  0.0,  // k = 4
        0.0,  0.0,  // k = 5
        0.0,  0.0,  // k = 6
        0.0,  0.4,  // k = 7
        0.0,  0.0,  // k = 8
    };

    std::vector<double> samples;
    REQUIRE(nodalys::HarmonicsToReal(expected_coeffs, M, samples));
    REQUIRE(samples.size() == M);

    for (std::size_t m = 0; m < M; ++m) {
        const double theta = 2.0 * std::numbers::pi * static_cast<double>(m) / static_cast<double>(M);
        const double direct = 1.25 + 2.0 * std::cos(theta) - 1.5 * std::sin(theta) + 0.7 * std::cos(3.0 * theta) +
                              0.4 * std::sin(7.0 * theta);
        CHECK_THAT(samples[m], WithinAbs(direct, 1e-12));
    }

    std::vector<double> recovered;
    REQUIRE(nodalys::RealToHarmonics(samples, H, recovered));
    REQUIRE(recovered.size() == expected_coeffs.size());
    for (std::size_t i = 0; i < expected_coeffs.size(); ++i) {
        CHECK_THAT(recovered[i], WithinAbs(expected_coeffs[i], 1e-12));
    }
}

TEST_CASE("Parseval energy identity holds for real harmonic conversions", "[linear][fft]") {
    constexpr std::size_t M = 128;
    constexpr int H = 5;
    const std::vector<double> coeffs = {0.8, 1.2, -0.6, 0.3, 0.4, -0.2, 0.1, 0.0, 0.5, -0.15, 0.25};

    std::vector<double> samples;
    REQUIRE(nodalys::HarmonicsToReal(coeffs, M, samples));

    double time_mean_square = 0.0;
    for (const double s : samples) {
        time_mean_square += s * s;
    }
    time_mean_square /= static_cast<double>(M);

    double freq_mean_square = coeffs[0] * coeffs[0];
    for (int k = 1; k <= H; ++k) {
        const double ak = coeffs[static_cast<std::size_t>(2 * k - 1)];
        const double bk = coeffs[static_cast<std::size_t>(2 * k)];
        freq_mean_square += 0.5 * (ak * ak + bk * bk);
    }

    CHECK_THAT(time_mean_square, WithinAbs(freq_mean_square, 1e-12));
}

} // namespace
