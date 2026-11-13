/**
 * @file    linear_tests.cpp
 * @brief   Unit tests for Nodalys linear algebra and QZ generalized eigenvalue solver.
 */

#include "linear/dense_matrix.h"
#include "linear/qz.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <complex>
#include <nodalys/nodalys.h>
#include <vector>

namespace {

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

TEST_CASE("Generalized eigenvalues of a 1x1 pencil", "[linear][qz]") {
    Nodalys::DenseMatrix a(1, 1);
    Nodalys::DenseMatrix b(1, 1);

    SECTION("Finite eigenvalue") {
        a.At(0, 0) = -1000.0;
        b.At(0, 0) = 1.0;
        const auto eigs = Nodalys::ComputeGeneralizedEigenvalues(a, b);
        REQUIRE(eigs.size() == 1);
        CHECK_THAT(eigs[0].real(), WithinRel(-1000.0, 1e-9));
        CHECK_THAT(eigs[0].imag(), WithinAbs(0.0, 1e-12));
    }

    SECTION("Infinite eigenvalue is deflated") {
        a.At(0, 0) = 5.0;
        b.At(0, 0) = 0.0;
        const auto eigs = Nodalys::ComputeGeneralizedEigenvalues(a, b);
        CHECK(eigs.empty());
    }
}

TEST_CASE("Generalized eigenvalues of a 2x2 real symmetric pencil", "[linear][qz]") {
    Nodalys::DenseMatrix a(2, 2);
    Nodalys::DenseMatrix b(2, 2);

    a.At(0, 0) = 2.0;
    a.At(0, 1) = 1.0;
    a.At(1, 0) = 1.0;
    a.At(1, 1) = 3.0;

    b.At(0, 0) = 1.0;
    b.At(0, 1) = 0.0;
    b.At(1, 0) = 0.0;
    b.At(1, 1) = 1.0;

    // det(A - lambda I) = lambda^2 - 5 lambda + 5 = 0 -> (5 +/- sqrt(5)) / 2
    const double expected1 = (5.0 + std::sqrt(5.0)) / 2.0;
    const double expected2 = (5.0 - std::sqrt(5.0)) / 2.0;

    auto eigs = Nodalys::ComputeGeneralizedEigenvalues(a, b);
    REQUIRE(eigs.size() == 2);
    std::ranges::sort(eigs, [](const auto &lhs, const auto &rhs) { return lhs.real() > rhs.real(); });

    CHECK_THAT(eigs[0].real(), WithinRel(expected1, 1e-9));
    CHECK_THAT(eigs[0].imag(), WithinAbs(0.0, 1e-12));
    CHECK_THAT(eigs[1].real(), WithinRel(expected2, 1e-9));
    CHECK_THAT(eigs[1].imag(), WithinAbs(0.0, 1e-12));
}

TEST_CASE("Generalized eigenvalues of a 2x2 complex conjugate pair", "[linear][qz]") {
    Nodalys::DenseMatrix a(2, 2);
    Nodalys::DenseMatrix b(2, 2);

    a.At(0, 0) = 0.0;
    a.At(0, 1) = -2.0;
    a.At(1, 0) = 2.0;
    a.At(1, 1) = 0.0;

    b.At(0, 0) = 1.0;
    b.At(0, 1) = 0.0;
    b.At(1, 0) = 0.0;
    b.At(1, 1) = 1.0;

    // det(A - lambda I) = lambda^2 + 4 = 0 -> lambda = +/- 2j
    auto eigs = Nodalys::ComputeGeneralizedEigenvalues(a, b);
    REQUIRE(eigs.size() == 2);

    CHECK_THAT(eigs[0].real(), WithinAbs(0.0, 1e-12));
    CHECK_THAT(std::abs(eigs[0].imag()), WithinRel(2.0, 1e-9));
    CHECK_THAT(eigs[1].real(), WithinAbs(0.0, 1e-12));
    CHECK_THAT(std::abs(eigs[1].imag()), WithinRel(2.0, 1e-9));
    CHECK_THAT(eigs[0].imag() + eigs[1].imag(), WithinAbs(0.0, 1e-12));
}

TEST_CASE("Generalized eigenvalues of a 2x2 singular B pencil", "[linear][qz]") {
    Nodalys::DenseMatrix a(2, 2);
    Nodalys::DenseMatrix b(2, 2);

    a.At(0, 0) = 1.0;
    a.At(0, 1) = 2.0;
    a.At(1, 0) = 3.0;
    a.At(1, 1) = 4.0;

    b.At(0, 0) = 1.0;
    b.At(0, 1) = 0.0;
    b.At(1, 0) = 0.0;
    b.At(1, 1) = 0.0;

    // det(A - lambda B) = 4(1 - lambda) - 6 = -4 lambda - 2 = 0 -> lambda = -0.5
    const auto eigs = Nodalys::ComputeGeneralizedEigenvalues(a, b);
    REQUIRE(eigs.size() == 1);
    CHECK_THAT(eigs[0].real(), WithinRel(-0.5, 1e-9));
    CHECK_THAT(eigs[0].imag(), WithinAbs(0.0, 1e-12));
}

TEST_CASE("Generalized eigenvalues of a 3x3 symmetric pencil", "[linear][qz]") {
    Nodalys::DenseMatrix a(3, 3);
    Nodalys::DenseMatrix b(3, 3);

    a.At(0, 0) = 2.0;
    a.At(0, 1) = 1.0;
    a.At(0, 2) = 0.0;
    a.At(1, 0) = 1.0;
    a.At(1, 1) = 3.0;
    a.At(1, 2) = 1.0;
    a.At(2, 0) = 0.0;
    a.At(2, 1) = 1.0;
    a.At(2, 2) = 2.0;

    for (std::size_t i = 0; i < 3; ++i) {
        b.At(i, i) = 1.0;
    }

    // Roots are 4, 2, 1
    auto eigs = Nodalys::ComputeGeneralizedEigenvalues(a, b);
    REQUIRE(eigs.size() == 3);
    std::ranges::sort(eigs, [](const auto &lhs, const auto &rhs) { return lhs.real() > rhs.real(); });

    CHECK_THAT(eigs[0].real(), WithinRel(4.0, 1e-9));
    CHECK_THAT(eigs[0].imag(), WithinAbs(0.0, 1e-12));
    CHECK_THAT(eigs[1].real(), WithinRel(2.0, 1e-9));
    CHECK_THAT(eigs[1].imag(), WithinAbs(0.0, 1e-12));
    CHECK_THAT(eigs[2].real(), WithinRel(1.0, 1e-9));
    CHECK_THAT(eigs[2].imag(), WithinAbs(0.0, 1e-12));
}

TEST_CASE("Generalized eigenvalues of a 3x3 singular B pencil", "[linear][qz]") {
    Nodalys::DenseMatrix a(3, 3);
    Nodalys::DenseMatrix b(3, 3);

    a.At(0, 0) = 2.0;
    a.At(0, 1) = 1.0;
    a.At(0, 2) = 0.0;
    a.At(1, 0) = 1.0;
    a.At(1, 1) = 3.0;
    a.At(1, 2) = 1.0;
    a.At(2, 0) = 0.0;
    a.At(2, 1) = 1.0;
    a.At(2, 2) = 2.0;

    b.At(0, 0) = 1.0;
    b.At(1, 1) = 1.0;
    b.At(2, 2) = 0.0; // Singular algebraic row

    // det(A - lambda B) = 2 lambda^2 - 9 lambda + 8 = 0 -> (9 +/- sqrt(17)) / 4
    const double expected1 = (9.0 + std::sqrt(17.0)) / 4.0;
    const double expected2 = (9.0 - std::sqrt(17.0)) / 4.0;

    auto eigs = Nodalys::ComputeGeneralizedEigenvalues(a, b);
    REQUIRE(eigs.size() == 2);
    std::ranges::sort(eigs, [](const auto &lhs, const auto &rhs) { return lhs.real() > rhs.real(); });

    CHECK_THAT(eigs[0].real(), WithinRel(expected1, 1e-9));
    CHECK_THAT(eigs[0].imag(), WithinAbs(0.0, 1e-12));
    CHECK_THAT(eigs[1].real(), WithinRel(expected2, 1e-9));
    CHECK_THAT(eigs[1].imag(), WithinAbs(0.0, 1e-12));
}

TEST_CASE("RLC low-pass pole-zero analysis returns the two poles and no spurious infinite zeros", "[pz][linear]") {
    Nodalys::CircuitBuilder builder;
    const Nodalys::NodeId n1 = builder.AddNode();
    const Nodalys::NodeId n2 = builder.AddNode();
    const Nodalys::NodeId n3 = builder.AddNode();
    builder.AddVdc("V1", n1, Nodalys::GROUND_NODE, 0.0, 1.0);
    builder.AddResistor("R1", n1, n2, 10.0);
    builder.AddInductor("L1", n2, n3, 1e-3);
    builder.AddCapacitor("C1", n3, Nodalys::GROUND_NODE, 1e-6);
    const auto circuit = builder.Build();
    REQUIRE(circuit.has_value());

    const auto pz = Nodalys::ComputePoleZero(*circuit, {.OutputNode = n3, .InputSource = "V1"});
    REQUIRE(pz.has_value());
    CHECK(pz->Poles.size() == 2);
    CHECK(pz->Zeros.empty());
}

} // namespace
