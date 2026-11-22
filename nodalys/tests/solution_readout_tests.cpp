/**
 * @file    solution_readout_tests.cpp
 * @brief   Tests of the reading of the component currents out of the solution vector of an analysis.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "analyses/analysis_preamble.h"
#include "analyses/solution_readout.h"
#include "api/circuit_impl.h"

#include <nodalys/nodalys.h>

#include <string>

using Catch::Matchers::WithinAbs;

TEST_CASE("The currents read out of a solution are the ones the operating point reports",
          "[nodalys][solution_readout]") {
    auto parsed = Nodalys::ParseNetlist("divider\nV1 1 0 DC 10\nR1 1 2 1k\nR2 2 0 4k\n.end\n");
    REQUIRE(parsed.has_value());
    auto run = Nodalys::CircuitAccess::CloneForRun(parsed->CircuitInstance);
    REQUIRE(run.has_value());
    Nodalys::CompiledCircuit &compiled = *run;
    const auto x_dc = Nodalys::SolveOperatingPoint(compiled, Nodalys::ConvergenceOptions{}, "unused", nullptr);
    REQUIRE(x_dc.has_value());
    const auto point = Nodalys::ComputeOperatingPoint(parsed->CircuitInstance);
    REQUIRE(point.has_value());

    const std::vector<double> currents = Nodalys::ReadCurrentValues(compiled, *x_dc);

    REQUIRE(currents.size() == point->Currents.size());
    REQUIRE_FALSE(currents.empty());
    for (std::size_t index = 0; index < currents.size(); ++index) {
        CHECK(currents[index] == point->Currents[index].Current);
    }
    // 10 V across 5 kOhm, in the direction that the source delivers
    bool has_source_current = false;
    for (const double current : currents) {
        has_source_current = has_source_current || std::abs(current) > 1.9e-3;
    }
    CHECK(has_source_current);
}
