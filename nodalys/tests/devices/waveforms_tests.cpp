/**
 * @file    waveforms_tests.cpp
 * @brief   Unit tests for piecewise-linear evaluation and breakpoint lookup on large tables.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "devices/sources/waveforms.h"
#include <cmath>
#include <limits>
#include <vector>

using Catch::Matchers::WithinAbs;

TEST_CASE("EvaluatePwl interpolates inside the table and clamps outside", "[waveforms]") {
    const Nodalys::PwlParameters pwl{.Points = {{1e-3, -2.0}, {3e-3, 4.0}, {5e-3, 0.0}}};

    CHECK_THAT(Nodalys::EvaluatePwl(pwl, 0.0), WithinAbs(-2.0, 1e-12));
    CHECK_THAT(Nodalys::EvaluatePwl(pwl, 1e-3), WithinAbs(-2.0, 1e-12));
    CHECK_THAT(Nodalys::EvaluatePwl(pwl, 2e-3), WithinAbs(1.0, 1e-12));
    CHECK_THAT(Nodalys::EvaluatePwl(pwl, 3e-3), WithinAbs(4.0, 1e-12));
    CHECK_THAT(Nodalys::EvaluatePwl(pwl, 4e-3), WithinAbs(2.0, 1e-12));
    CHECK_THAT(Nodalys::EvaluatePwl(pwl, 8e-3), WithinAbs(0.0, 1e-12));
}

TEST_CASE("EvaluatePwl and MakePwlBreakpoints handle large sampled waveforms accurately", "[waveforms]") {
    constexpr std::size_t SAMPLE_COUNT = 20'000;
    constexpr double SAMPLE_PERIOD = 1.0 / 44'100.0;
    Nodalys::PwlParameters pwl;
    pwl.Points.reserve(SAMPLE_COUNT);
    for (std::size_t index = 0; index < SAMPLE_COUNT; ++index) {
        const double t = static_cast<double>(index) * SAMPLE_PERIOD;
        pwl.Points.push_back({.Time = t, .Value = std::sin(2.0 * 3.14159265358979323846 * 440.0 * t)});
    }

    const Nodalys::BreakpointCallback breakpoints = Nodalys::MakePwlBreakpoints(pwl);
    const double mid_time = 10'000.0 * SAMPLE_PERIOD;
    CHECK_THAT(breakpoints(mid_time), WithinAbs(10'001.0 * SAMPLE_PERIOD, 1e-12));
    CHECK(std::isinf(breakpoints(pwl.Points.back().Time)));

    // Halfway between samples 12345 and 12346 matches linear interpolation
    const double t0 = pwl.Points[12'345].Time;
    const double t1 = pwl.Points[12'346].Time;
    const double expected = 0.5 * (pwl.Points[12'345].Value + pwl.Points[12'346].Value);
    CHECK_THAT(Nodalys::EvaluatePwl(pwl, 0.5 * (t0 + t1)), WithinAbs(expected, 1e-12));
}
