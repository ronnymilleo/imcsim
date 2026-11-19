/**
 * @file    hb_bias_tests.cpp
 * @brief   Tests for the gmin ladder that the harmonic balance steps down.
 */

#include "analyses/hb_bias.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

namespace {

using Catch::Matchers::WithinRel;

TEST_CASE("The gmin ladder falls by decades from its start and stops at the gmin of the options",
          "[nodalys][hb][bias]") {
    Nodalys::ConvergenceOptions options;
    options.Gmin = 1e-9;

    CHECK(Nodalys::GminAtStep(0, options) == Nodalys::GMIN_START);
    CHECK_THAT(Nodalys::GminAtStep(2, options), WithinRel(1e-5, 1e-12));
    // Past the floor, every rung is the gmin of the options
    CHECK(Nodalys::GminAtStep(20, options) == options.Gmin);
}

} // namespace
