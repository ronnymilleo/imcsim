/**
 * @file    hb_workspace_tests.cpp
 * @brief   Tests for the workspace of the harmonic balance and its traces of matrix entries.
 */

#include "analyses/hb/hb_workspace.h"

#include <catch2/catch_test_macros.hpp>

#include <utility>
#include <vector>

namespace {

TEST_CASE("The traces keep only the entries that are not zero, one trace over the samples each",
          "[nodalys][hb][workspace]") {
    constexpr std::size_t SAMPLES = 4;
    Nodalys::ActiveMatrixTraces traces;
    traces.Reset(2, SAMPLES);

    // The entry (0, 1) is nonzero at samples 1 and 3 only, and (1, 1) at sample 3
    for (std::size_t m = 0; m < SAMPLES; ++m) {
        Nodalys::MnaSystem system(2, 0);
        if (m == 1 || m == 3) {
            system.AddA(0, 1, static_cast<double>(m));
        }
        if (m == 3) {
            system.AddA(1, 1, 7.0);
        }
        traces.RecordMatrix(system, m);
    }

    REQUIRE(traces.Pairs.size() == 2);
    CHECK(traces.Pairs[0] == std::pair<int, int>(0, 1));
    CHECK(traces.Pairs[1] == std::pair<int, int>(1, 1));
    CHECK(traces.Traces == std::vector<double>{0.0, 1.0, 0.0, 3.0, 0.0, 0.0, 0.0, 7.0});
}

TEST_CASE("Resetting the traces forgets the entries of the last matrix and takes the new sample count",
          "[nodalys][hb][workspace]") {
    Nodalys::ActiveMatrixTraces traces;
    traces.Reset(1, 2);
    Nodalys::MnaSystem system(1, 0);
    system.AddA(0, 0, 5.0);
    traces.RecordMatrix(system, 0);
    REQUIRE(traces.Pairs.size() == 1);

    traces.Reset(1, 3);
    CHECK(traces.Pairs.empty());
    CHECK(traces.Traces.empty());
    traces.RecordMatrix(system, 2);
    CHECK(traces.Traces == std::vector<double>{0.0, 0.0, 5.0});
}

TEST_CASE("The coefficients of an unknown are laid out one unknown after the other", "[nodalys][hb][workspace]") {
    const Nodalys::HbWorkspace ws(3, 2, 16, 1000.0, std::vector<bool>(3, true));
    CHECK(ws.CoeffsPerUnknown == 5);
    CHECK(ws.Index(0, 0) == 0);
    CHECK(ws.Index(1, 0) == 5);
    CHECK(ws.Index(2, 4) == 14);
}

} // namespace
