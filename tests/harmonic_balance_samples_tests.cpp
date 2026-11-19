/**
 * @file    harmonic_balance_samples_tests.cpp
 * @brief   Tests for the time samples per period a harmonic balance may be set to.
 */

#include "analysis/harmonic_balance_samples.h"

#include "simulation/analysis_settings.h"
#include <catch2/catch_test_macros.hpp>
#include <vector>

TEST_CASE("A sample count set by hand rises above twice the harmonics, and Auto stays Auto",
          "[harmonic_balance_samples]") {
    CHECK(GUI::NormalizeSampleCount(0, 64) == 0);
    CHECK(GUI::NormalizeSampleCount(64, 16) == 64);
    // Twice 16 harmonics is 32, which is not above the limit, so the next power of two takes its place
    CHECK(GUI::NormalizeSampleCount(32, 16) == 64);
    CHECK(GUI::NormalizeSampleCount(32, 64) == 256);
    CHECK(GUI::NormalizeSampleCount(8, 4) == GUI::MIN_EXPLICIT_SAMPLES);
}

TEST_CASE("The sample counts on offer are the powers of two above twice the harmonics", "[harmonic_balance_samples]") {
    const std::vector<int> few_harmonics = GUI::ListSampleCounts(4);
    REQUIRE_FALSE(few_harmonics.empty());
    CHECK(few_harmonics.front() == GUI::MIN_EXPLICIT_SAMPLES);
    CHECK(few_harmonics.back() == Core::MAX_HARMONIC_BALANCE_SAMPLES);

    const std::vector<int> many_harmonics = GUI::ListSampleCounts(64);
    CHECK(many_harmonics.front() == 256);
    CHECK(many_harmonics.size() == few_harmonics.size() - 3);
}
