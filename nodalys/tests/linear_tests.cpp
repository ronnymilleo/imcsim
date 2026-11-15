/**
 * @file    linear_tests.cpp
 * @brief   Unit tests for Nodalys linear algebra and QZ generalized eigenvalue solver.
 */

#include "linear/dense_matrix.h"
#include "linear/qz.h"
#include <algorithm>
#include <array>
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

TEST_CASE("A small B entry beside a large conductance is a finite eigenvalue", "[linear][qz]") {
    // The Rosenbrock pencil of a common-source stage: x = (v1, v2, v3, iV, u) with Rs = 1k, RL = 10k, Cgs = 10 pF,
    // Cgd = 1 pF and gm = 2 mS. Its transfer function (Cgd s - gm) has one zero, at gm / Cgd = 2e9 rad/s, in the
    // right half-plane; its only B entry that reaches the algebraic part is 1e-12 beside entries of order 1.
    constexpr double SOURCE_RESISTANCE = 1e3;
    constexpr double LOAD_RESISTANCE = 1e4;
    constexpr double GATE_SOURCE_CAPACITANCE = 10e-12;
    constexpr double GATE_DRAIN_CAPACITANCE = 1e-12;
    constexpr double TRANSCONDUCTANCE = 2e-3;
    Nodalys::DenseMatrix a(5, 5);
    Nodalys::DenseMatrix b(5, 5);
    a.At(0, 0) = 1.0 / SOURCE_RESISTANCE;
    a.At(0, 1) = -1.0 / SOURCE_RESISTANCE;
    a.At(0, 3) = 1.0;
    a.At(1, 0) = -1.0 / SOURCE_RESISTANCE;
    a.At(1, 1) = 1.0 / SOURCE_RESISTANCE;
    b.At(1, 1) = -(GATE_SOURCE_CAPACITANCE + GATE_DRAIN_CAPACITANCE);
    b.At(1, 2) = GATE_DRAIN_CAPACITANCE;
    a.At(2, 1) = TRANSCONDUCTANCE;
    a.At(2, 2) = 1.0 / LOAD_RESISTANCE;
    b.At(2, 1) = GATE_DRAIN_CAPACITANCE;
    b.At(2, 2) = -GATE_DRAIN_CAPACITANCE;
    a.At(3, 0) = 1.0;
    a.At(3, 4) = -1.0;
    a.At(4, 2) = 1.0;

    const auto zeros = Nodalys::ComputeGeneralizedEigenvalues(a, b);
    REQUIRE(zeros.size() == 1);
    CHECK_THAT(zeros[0].real(), WithinRel(TRANSCONDUCTANCE / GATE_DRAIN_CAPACITANCE, 1e-9));
    CHECK_THAT(zeros[0].imag(), WithinAbs(0.0, 1.0));
}

TEST_CASE("Eigenvalues twelve decades apart are all found, whichever way the pencil is ordered", "[linear][qz]") {
    // Four decoupled first-order modes, as in a circuit with a slow RC and a fast junction, each with a different
    // size of its equations, coupled by a triangular term so the pencil is not diagonal
    constexpr std::array<double, 4> ROOTS{3.0, 4.5e3, 2.0e7, 6.0e12};
    for (const std::array<std::size_t, 4> order :
         {std::array<std::size_t, 4>{0, 1, 2, 3}, std::array<std::size_t, 4>{3, 2, 1, 0},
          std::array<std::size_t, 4>{1, 3, 0, 2}}) {
        Nodalys::DenseMatrix a(4, 4);
        Nodalys::DenseMatrix b(4, 4);
        for (std::size_t row = 0; row < 4; ++row) {
            const double root = ROOTS[order[row]];
            a.At(row, row) = root * 1e-9;
            b.At(row, row) = 1e-9;
            for (std::size_t column = row + 1; column < 4; ++column) {
                a.At(row, column) = 0.25e-9;
                b.At(row, column) = 0.1e-9;
            }
        }
        auto roots = Nodalys::ComputeGeneralizedEigenvalues(a, b);
        REQUIRE(roots.size() == 4);
        std::ranges::sort(roots, {}, [](const auto &root) { return root.real(); });
        for (std::size_t index = 0; index < 4; ++index) {
            CHECK_THAT(roots[index].real(), WithinRel(ROOTS[index], 1e-9));
        }
    }
}

TEST_CASE("Rows with no dynamics are removed without moving the roots of the rest", "[linear][qz]") {
    // Node 1 has 1 uF and 2 mS to ground and 1 mS to node 2, which a voltage source holds at 0 V for the small signal:
    // two algebraic equations (the source branch and its node) around one state, whose pole is -3 mS / 1 uF
    Nodalys::DenseMatrix a(3, 3);
    Nodalys::DenseMatrix b(3, 3);
    a.At(0, 0) = 3e-3;
    a.At(0, 1) = -1e-3;
    b.At(0, 0) = -1e-6;
    a.At(1, 0) = -1e-3;
    a.At(1, 1) = 1e-3;
    a.At(1, 2) = 1.0;
    a.At(2, 1) = 1.0;
    const auto roots = Nodalys::ComputeGeneralizedEigenvalues(a, b);
    REQUIRE(roots.size() == 1);
    CHECK_THAT(roots[0].real(), WithinRel(-3000.0, 1e-9));
    CHECK_THAT(roots[0].imag(), WithinAbs(0.0, 1e-9));
}

} // namespace
