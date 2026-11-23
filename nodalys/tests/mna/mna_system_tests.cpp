/**
 * @file    mna_system_tests.cpp
 * @brief   Tests for the storage-agnostic MnaSystem matrix access, accumulation, copying, and AC solve contracts.
 */

#include "linear/dense_matrix.h"
#include "mna/mna_system.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <complex>
#include <tuple>
#include <vector>

namespace {

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

TEST_CASE("MnaSystem exposes entries, non-zero iteration, deep copies, and dense materialization", "[nodalys][mna]") {
    Nodalys::MnaSystem system(2, 1);
    system.AddA(0, 0, 2.0);
    system.AddA(0, 2, 1.0);
    system.AddA(2, 0, -1.0);
    system.AddA(1, 1, 0.5);
    system.AddA(1, 1, -0.5); // cancels to zero

    CHECK(system.Entry(0, 0) == 2.0);
    CHECK(system.Entry(0, 2) == 1.0);
    CHECK(system.Entry(2, 0) == -1.0);
    CHECK(system.Entry(1, 1) == 0.0);
    CHECK(system.Entry(-1, 0) == 0.0);
    CHECK(system.Entry(0, 99) == 0.0);

    std::vector<std::tuple<int, int, double>> entries;
    system.ForEachNonZero(
        [&](const int row, const int col, const double value) { entries.emplace_back(row, col, value); });
    REQUIRE(entries.size() == 3);
    CHECK(entries[0] == std::tuple<int, int, double>{0, 0, 2.0});
    CHECK(entries[1] == std::tuple<int, int, double>{0, 2, 1.0});
    CHECK(entries[2] == std::tuple<int, int, double>{2, 0, -1.0});

    Nodalys::MnaSystem copied(system);
    copied.AddA(0, 0, 10.0);
    CHECK(system.Entry(0, 0) == 2.0);
    CHECK(copied.Entry(0, 0) == 12.0);

    Nodalys::MnaSystem assigned(1, 0);
    assigned = system;
    assigned.Clear();
    CHECK(system.Entry(0, 0) == 2.0);
    CHECK(assigned.Entry(0, 0) == 0.0);

    const Nodalys::DenseMatrix dense = system.ToDenseMatrix();
    REQUIRE(dense.Rows() == 3);
    REQUIRE(dense.Cols() == 3);
    CHECK(dense.At(0, 0) == 2.0);
    CHECK(dense.At(0, 2) == 1.0);
    CHECK(dense.At(2, 0) == -1.0);
    CHECK(dense.At(1, 1) == 0.0);
}

TEST_CASE("MnaSystem accumulates another system matrix and solves complex AC RHS vectors", "[nodalys][mna]") {
    Nodalys::MnaSystem conductance(1, 0);
    conductance.AddA(0, 0, 1.0);

    Nodalys::MnaSystem extra(1, 0);
    extra.AddA(0, 0, 2.0);
    conductance.AddMatrixFrom(extra);
    CHECK(conductance.Entry(0, 0) == 3.0);

    Nodalys::MnaSystem reactive(1, 0);
    reactive.AddA(0, 0, 4.0);

    // (3 + j * 1 * 4) * x = 6 + j * 8  =>  x = 2 + j * 0
    const std::vector<std::complex<double>> rhs = {{6.0, 8.0}};
    std::vector<std::complex<double>> solution;
    REQUIRE(conductance.SolveAcFor(reactive, 1.0, rhs, solution));
    REQUIRE(solution.size() == 1);
    CHECK_THAT(solution[0].real(), WithinRel(2.0, 1e-12));
    CHECK_THAT(solution[0].imag(), WithinAbs(0.0, 1e-12));

    // Size mismatch is rejected cleanly
    const std::vector<std::complex<double>> wrong_rhs = {{1.0, 0.0}, {2.0, 0.0}};
    CHECK_FALSE(conductance.SolveAcFor(reactive, 1.0, wrong_rhs, solution));
}

} // namespace
