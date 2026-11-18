/**
 * @file    random_draws_tests.cpp
 * @brief   Unit tests for the hand-written random draws of Nodalys, pinned so every platform draws the same numbers.
 */

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "devices/random_draws.h"
#include "devices/transient_noise.h"
#include <array>
#include <cmath>

namespace {

/// Seed of the pinned sequences.
constexpr std::uint64_t PINNED_SEED = 12345;
/// The transcendental functions may differ in the last bit between C libraries, so pinned values get this much room.
constexpr double PINNED_RELATIVE_TOLERANCE = 1e-12;
/// Draws taken for the statistical checks.
constexpr int STATISTICS_DRAWS = 200000;

/// Mean and variance of a sequence of draws.
struct Moments {
    double Mean = 0.0;
    double Variance = 0.0;
};

template <typename Draw> Moments MeasureMoments(const Draw &draw) {
    std::mt19937_64 engine(PINNED_SEED);
    double sum = 0.0;
    double sum_squares = 0.0;
    for (int index = 0; index < STATISTICS_DRAWS; ++index) {
        const double value = draw(engine);
        sum += value;
        sum_squares += value * value;
    }
    const double mean = sum / STATISTICS_DRAWS;
    return {.Mean = mean, .Variance = sum_squares / STATISTICS_DRAWS - mean * mean};
}

Catch::Approx Pinned(const double value) {
    return Catch::Approx(value).epsilon(PINNED_RELATIVE_TOLERANCE).margin(0.0);
}

} // namespace

TEST_CASE("A seed draws the same uniform numbers on every platform", "[random_draws]") {
    std::mt19937_64 engine(PINNED_SEED);
    constexpr std::array EXPECTED = {0.35762972288842587,
                                     0.40044261704406114,
                                     0.68938331700276845,
                                     0.55973557064111557};
    for (const double expected : EXPECTED) {
        CHECK(Nodalys::DrawUniform(engine) == Pinned(expected));
    }
}

TEST_CASE("A seed draws the same normal numbers on every platform", "[random_draws]") {
    std::mt19937_64 engine(PINNED_SEED);
    constexpr std::array EXPECTED = {-0.76269092759341395,
                                     -1.4227288787842618,
                                     0.34342317777493808,
                                     -0.090293886742040627};
    for (const double expected : EXPECTED) {
        CHECK(Nodalys::DrawStandardNormal(engine) == Pinned(expected));
    }
}

TEST_CASE("A seed draws the same exponential numbers on every platform", "[random_draws]") {
    std::mt19937_64 engine(PINNED_SEED);
    constexpr std::array EXPECTED = {0.88518077185254485, 1.0231271821406542, 2.3383913150978328, 1.6407595135356601};
    for (const double expected : EXPECTED) {
        CHECK(Nodalys::DrawExponential(engine, 2.0) == Pinned(expected));
    }
}

TEST_CASE("A seed draws the same Poisson counts on every platform", "[random_draws]") {
    SECTION("A small mean uses Knuth's multiplication") {
        std::mt19937_64 engine(PINNED_SEED);
        constexpr std::array EXPECTED = {5, 0, 3, 0, 4, 0};
        for (const int expected : EXPECTED) {
            CHECK(Nodalys::DrawPoisson(engine, 3.5) == expected);
        }
    }
    SECTION("A mean above one chunk is split into chunks") {
        std::mt19937_64 engine(PINNED_SEED);
        constexpr std::array EXPECTED = {61, 76, 81};
        for (const int expected : EXPECTED) {
            CHECK(Nodalys::DrawPoisson(engine, 75.0) == expected);
        }
    }
}

TEST_CASE("A noise source built on the draws gives the pinned first sample for a seed", "[random_draws]") {
    constexpr double WHITE_AMPLITUDE = 2.0;
    constexpr double TIME_STEP = 1e-3;
    const Nodalys::TransientNoiseSource source =
        Nodalys::MakeNoiseSource({.Offset = 0.0, .WhiteAmplitude = WHITE_AMPLITUDE, .TimeStep = TIME_STEP});
    source.ResetSeed(PINNED_SEED);
    // The first sample after time 0 is the amplitude times the first normal number of the seed
    const double first_sample = source.Evaluate(0.0, {.IsTransient = true, .Time = TIME_STEP});
    CHECK(first_sample == Pinned(WHITE_AMPLITUDE * -0.76269092759341395));
}

TEST_CASE("The uniform draws lie in [0, 1) with mean 1/2 and variance 1/12", "[random_draws]") {
    const Moments moments = MeasureMoments([](std::mt19937_64 &engine) {
        const double value = Nodalys::DrawUniform(engine);
        REQUIRE(value >= 0.0);
        REQUIRE(value < 1.0);
        return value;
    });
    CHECK(moments.Mean == Catch::Approx(0.5).margin(0.005));
    CHECK(moments.Variance == Catch::Approx(1.0 / 12.0).margin(0.002));
}

TEST_CASE("The normal draws have mean 0 and variance 1", "[random_draws]") {
    const Moments moments = MeasureMoments(Nodalys::DrawStandardNormal);
    CHECK(moments.Mean == Catch::Approx(0.0).margin(0.01));
    CHECK(moments.Variance == Catch::Approx(1.0).margin(0.02));
}

TEST_CASE("The exponential draws have the given mean, a variance of its square and are never negative",
          "[random_draws]") {
    constexpr double MEAN = 2.0;
    const Moments moments = MeasureMoments([](std::mt19937_64 &engine) {
        const double value = Nodalys::DrawExponential(engine, MEAN);
        REQUIRE(value >= 0.0);
        return value;
    });
    CHECK(moments.Mean == Catch::Approx(MEAN).epsilon(0.02));
    CHECK(moments.Variance == Catch::Approx(MEAN * MEAN).epsilon(0.05));
}

TEST_CASE("The Poisson draws have a mean and a variance equal to lambda, in every range of lambda", "[random_draws]") {
    constexpr double SMALL_LAMBDA = 3.5;
    constexpr double CHUNKED_LAMBDA = 75.0;
    constexpr double APPROXIMATED_LAMBDA = 4000.0;
    for (const double lambda : {SMALL_LAMBDA, CHUNKED_LAMBDA, APPROXIMATED_LAMBDA}) {
        const Moments moments = MeasureMoments(
            [lambda](std::mt19937_64 &engine) { return static_cast<double>(Nodalys::DrawPoisson(engine, lambda)); });
        CHECK(moments.Mean == Catch::Approx(lambda).epsilon(0.01));
        CHECK(moments.Variance == Catch::Approx(lambda).epsilon(0.05));
    }
}

TEST_CASE("A Poisson draw with a mean of 0 or less is 0", "[random_draws]") {
    std::mt19937_64 engine(PINNED_SEED);
    CHECK(Nodalys::DrawPoisson(engine, 0.0) == 0);
    CHECK(Nodalys::DrawPoisson(engine, -1.0) == 0);
}
