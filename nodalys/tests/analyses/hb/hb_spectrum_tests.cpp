/**
 * @file    hb_spectrum_tests.cpp
 * @brief   Tests for the conversions between harmonics, phasors and conversion blocks of the harmonic balance.
 */

#include "analyses/hb/hb_spectrum.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <complex>
#include <vector>

namespace {

using Catch::Matchers::WithinAbs;

TEST_CASE("A cosine and sine series reads as peak phasors with the sine part negated", "[nodalys][hb][spectrum]") {
    // x(t) = 2 + 3 cos(wt) + 4 sin(wt) + 5 cos(2wt) + 6 sin(2wt)
    const std::vector<double> coeffs = {2.0, 3.0, 4.0, 5.0, 6.0};
    const std::vector<std::complex<double>> phasors = Nodalys::CoefficientsToPhasors(coeffs);

    REQUIRE(phasors.size() == 3);
    CHECK(phasors[0] == std::complex<double>(2.0, 0.0));
    CHECK(phasors[1] == std::complex<double>(3.0, -4.0));
    CHECK(phasors[2] == std::complex<double>(5.0, -6.0));
}

TEST_CASE("Phasors and coefficients convert back and forth without loss", "[nodalys][hb][spectrum]") {
    const std::vector<double> coeffs = {-1.5, 0.25, -0.75, 8.0, 0.0, 1e-9, -2e-9};
    const std::vector<std::complex<double>> phasors = Nodalys::CoefficientsToPhasors(coeffs);
    CHECK(Nodalys::PhasorsToCoefficients(phasors) == coeffs);
}

TEST_CASE("A constant entry stamps its value on the diagonal of the conversion block", "[nodalys][hb][spectrum]") {
    constexpr int HARMONICS = 2;
    constexpr std::size_t SIZE = 2 * HARMONICS + 1;
    // The spectrum of a constant g over the period has g at DC and nothing at the harmonics
    std::vector<std::complex<double>> spectrum(2 * HARMONICS + 1, {0.0, 0.0});
    spectrum[0] = {3.0, 0.0};

    Nodalys::DenseMatrix block(SIZE, SIZE);
    Nodalys::StampConversionBlock(block, 0, 0, HARMONICS, 1000.0, spectrum, false);

    for (std::size_t row = 0; row < SIZE; ++row) {
        for (std::size_t col = 0; col < SIZE; ++col) {
            CHECK_THAT(block.At(row, col), WithinAbs(row == col ? 3.0 : 0.0, 1e-15));
        }
    }
}

TEST_CASE("A constant charge coefficient stamps the time derivative of the series", "[nodalys][hb][spectrum]") {
    constexpr int HARMONICS = 1;
    constexpr std::size_t SIZE = 2 * HARMONICS + 1;
    constexpr double OMEGA0 = 100.0;
    std::vector<std::complex<double>> spectrum(2 * HARMONICS + 1, {0.0, 0.0});
    spectrum[0] = {2.0, 0.0};

    Nodalys::DenseMatrix block(SIZE, SIZE);
    Nodalys::StampConversionBlock(block, 0, 0, HARMONICS, OMEGA0, spectrum, true);

    // d/dt of a cos(wt) + b sin(wt) is w b cos(wt) - w a sin(wt), scaled by the constant 2
    CHECK_THAT(block.At(1, 2), WithinAbs(2.0 * OMEGA0, 1e-12));
    CHECK_THAT(block.At(2, 1), WithinAbs(-2.0 * OMEGA0, 1e-12));
    CHECK_THAT(block.At(0, 0), WithinAbs(0.0, 1e-15));
    CHECK_THAT(block.At(1, 1), WithinAbs(0.0, 1e-12));
}

} // namespace
