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
#include <utility>
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

TEST_CASE("Roots of a stiff pencil stay put when its entries change by a few roundings", "[linear][qz][refine]") {
    // The zero pencil of a transistor amplifier (bias network, coupling and bypass capacitors, junctions), 6 states
    // after the algebraic ones are gone, with roots from 1e-21 to 1e10 rad/s. Another platform rounds the stamps and
    // the Newton steps differently, which moves each entry by a few units in the last place; the roots must not move.
    constexpr std::size_t SIZE = 6;
    constexpr std::array<std::array<double, SIZE>, SIZE> A_ENTRIES = {
        {{{0, 0, 0, 0, -0.80000000000000004, 0}},
         {{0.5001063829792235, 0, 0, -0.5, 0, 0}},
         {{0, 1.5231465995293683, 0, -0.00024858189082107966, -0.37266235892893151, 0}},
         {{0, 0, 1.638400016384, 0, 0, 0}},
         {{-0.5, -0.046314636545804581, 0, 0.50000776818458825, 0.011574775044407066, 0}},
         {{0, -0.0090782300316215991, 0, -1.7496909849396222e-85, 0.80226955751590545, 0}}}};
    constexpr std::array<std::array<double, SIZE>, SIZE> B_ENTRIES = {
        {{{-0, -0, -0, -0, -0, 0.16384000000000001}},
         {{-5.0000000000000004e-06, -0, 5.0000000000000004e-06, -0, -0, 0}},
         {{-0, -0.0032000006739468685, -0, 7.487286551487825e-14, 1.6844928065084274e-10, 0}},
         {{0.16384000000000001, -0, -0.16384000000000001, -0, -0, 0}},
         {{-0, -0, -0, -9.6455181164520113e-13, 4.8227590582260056e-13, 0}},
         {{-0, 6.7394686833440075e-10, -0, 3.0790785107131559e-11, -1.8388210963716594e-10, 0}}}};
    constexpr double ROUNDING = 2.220446049250313e-16;
    const auto build = [&](const double ulps, const unsigned seed) {
        std::pair<Nodalys::DenseMatrix, Nodalys::DenseMatrix> pencil{Nodalys::DenseMatrix(SIZE, SIZE),
                                                                     Nodalys::DenseMatrix(SIZE, SIZE)};
        unsigned state = seed * 2654435761u + 1u;
        for (std::size_t row = 0; row < SIZE; ++row) {
            for (std::size_t column = 0; column < SIZE; ++column) {
                state = state * 1664525u + 1013904223u;
                const double unit = static_cast<double>(state >> 8) / 8388608.0 - 1.0;
                pencil.first.At(row, column) = A_ENTRIES[row][column] * (1.0 + unit * ulps * ROUNDING);
                pencil.second.At(row, column) = B_ENTRIES[row][column] * (1.0 + unit * ulps * ROUNDING);
            }
        }
        return pencil;
    };
    const auto sorted = [](std::vector<std::complex<double>> roots) {
        std::ranges::sort(roots, {}, [](const auto &root) { return root.real(); });
        return roots;
    };
    const auto [base_a, base_b] = build(0.0, 0);
    const std::vector<std::complex<double>> expected = sorted(Nodalys::ComputeGeneralizedEigenvalues(base_a, base_b));
    REQUIRE(expected.size() >= 4);
    const double largest = std::abs(expected.front());
    for (const double ulps : {4.0, 32.0}) {
        for (unsigned seed = 1; seed <= 12; ++seed) {
            const auto [a, b] = build(ulps, seed);
            const std::vector<std::complex<double>> roots = sorted(Nodalys::ComputeGeneralizedEigenvalues(a, b));
            INFO("ulps " << ulps << " seed " << seed);
            REQUIRE(roots.size() == expected.size());
            for (std::size_t index = 0; index < roots.size(); ++index) {
                // A root at the origin has no size to be relative to, so it is held to a share of the largest root
                const double allowed = 1e-6 * std::max(std::abs(expected[index]), 1e-9 * largest);
                CHECK(std::abs(roots[index] - expected[index]) <= allowed);
            }
        }
    }
}

} // namespace
