/**
 * @file    transient_noise_tests.cpp
 * @brief   Unit tests for TRNOISE, TRRANDOM, seed repeatability, and netlist parsing in Nodalys.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "devices/transient_noise.h"
#include <algorithm>
#include <cmath>
#include <nodalys/nodalys.h>
#include <numeric>
#include <variant>
#include <vector>

using Catch::Matchers::WithinAbs;

namespace {

/// Evaluates a noise or random waveform at the given transient time.
double Eval(const Nodalys::TransientNoiseSource &source, const double dc_value, const double time) {
    return source.Evaluate(dc_value, Nodalys::TimeContext{.IsTransient = true, .Time = time});
}

} // namespace

TEST_CASE("TRNOISE white noise starts at Offset, interpolates linearly, and matches RMS amplitude",
          "[transient_noise]") {
    const Nodalys::NoiseParameters params{.Offset = 1.25, .WhiteAmplitude = 0.5, .TimeStep = 1e-3};
    const Nodalys::TransientNoiseSource source = Nodalys::MakeNoiseSource(params);
    source.ResetSeed(42);

    // At t = 0, white perturbation is zero
    CHECK_THAT(Eval(source, params.Offset, 0.0), WithinAbs(1.25, 1e-12));

    // Midpoint between 0 and TimeStep is the average of endpoints
    const double at_step = Eval(source, params.Offset, 1e-3);
    const double at_half = Eval(source, params.Offset, 0.5e-3);
    CHECK_THAT(at_half, WithinAbs(0.5 * (1.25 + at_step), 1e-12));

    // Over many samples, RMS of (value - Offset) approaches WhiteAmplitude
    double sum_squares = 0.0;
    constexpr int SAMPLE_COUNT = 4000;
    for (int index = 1; index <= SAMPLE_COUNT; ++index) {
        const double delta = Eval(source, params.Offset, static_cast<double>(index) * params.TimeStep) - params.Offset;
        sum_squares += delta * delta;
    }
    const double rms = std::sqrt(sum_squares / static_cast<double>(SAMPLE_COUNT));
    CHECK_THAT(rms, WithinAbs(params.WhiteAmplitude, 0.05));
}

TEST_CASE("TRNOISE flicker and telegraph components are deterministic under ResetSeed", "[transient_noise]") {
    const Nodalys::NoiseParameters params{.Offset = 0.5,
                                          .WhiteAmplitude = 0.0,
                                          .TimeStep = 1e-4,
                                          .FlickerExponent = 1.0,
                                          .FlickerAmplitude = 0.2,
                                          .TelegraphAmplitude = 0.8,
                                          .CaptureTime = 3e-4,
                                          .EmissionTime = 3e-4};
    const Nodalys::TransientNoiseSource source = Nodalys::MakeNoiseSource(params);

    source.ResetSeed(99);
    std::vector<double> first_run;
    for (int index = 0; index <= 20; ++index) {
        first_run.push_back(Eval(source, params.Offset, static_cast<double>(index) * params.TimeStep));
    }
    const double first_break = source.NextBreakpoint(0.0);
    REQUIRE(std::isfinite(first_break));

    source.ResetSeed(99);
    for (int index = 0; index <= 20; ++index) {
        CHECK(Eval(source, params.Offset, static_cast<double>(index) * params.TimeStep) ==
              first_run[static_cast<std::size_t>(index)]);
    }
    CHECK(source.NextBreakpoint(0.0) == first_break);
    CHECK_THAT(first_run[0], WithinAbs(0.5, 1e-12));
}

TEST_CASE("TRRANDOM supports all four distributions, Delay, and sample-and-hold intervals", "[transient_noise]") {
    SECTION("Uniform stays within [Second - First, Second + First] and respects Delay") {
        const Nodalys::RandomParameters params{.Distribution = Nodalys::RandomDistribution::UNIFORM,
                                               .TimeStep = 1e-3,
                                               .Delay = 2e-3,
                                               .First = 0.5,
                                               .Second = 2.0};
        const Nodalys::TransientNoiseSource source = Nodalys::MakeRandomSource(params);
        source.ResetSeed(7);

        CHECK_THAT(Eval(source, params.Second, 0.0), WithinAbs(2.0, 1e-12));
        CHECK_THAT(Eval(source, params.Second, 1.5e-3), WithinAbs(2.0, 1e-12));
        CHECK_THAT(source.NextBreakpoint(0.0), WithinAbs(2e-3, 1e-12));

        // Sample-and-hold over [2ms, 3ms)
        const double held = Eval(source, params.Second, 2.1e-3);
        CHECK_THAT(Eval(source, params.Second, 2.9e-3), WithinAbs(held, 1e-12));
        for (int index = 0; index < 100; ++index) {
            const double val = Eval(source, params.Second, 2e-3 + static_cast<double>(index) * 1e-3);
            CHECK(val >= 1.5);
            CHECK(val <= 2.5);
        }
    }

    SECTION("Gaussian varies around Second") {
        const Nodalys::RandomParameters params{.Distribution = Nodalys::RandomDistribution::GAUSSIAN,
                                               .TimeStep = 1e-3,
                                               .Delay = 0.0,
                                               .First = 1.0,
                                               .Second = -1.0};
        const Nodalys::TransientNoiseSource source = Nodalys::MakeRandomSource(params);
        source.ResetSeed(11);

        CHECK_THAT(Eval(source, params.Second, 0.0), WithinAbs(-1.0, 1e-12));
        const double v1 = Eval(source, params.Second, 0.5e-3);
        const double v2 = Eval(source, params.Second, 1.5e-3);
        CHECK(v1 != v2);
    }

    SECTION("Exponential stays at or above Second") {
        const Nodalys::RandomParameters params{.Distribution = Nodalys::RandomDistribution::EXPONENTIAL,
                                               .TimeStep = 1e-3,
                                               .Delay = 0.0,
                                               .First = 0.5,
                                               .Second = 1.0};
        const Nodalys::TransientNoiseSource source = Nodalys::MakeRandomSource(params);
        source.ResetSeed(13);

        for (int index = 1; index <= 50; ++index) {
            CHECK(Eval(source, params.Second, static_cast<double>(index) * 1e-3) >= 1.0);
        }
    }

    SECTION("Poisson produces non-negative integer counts shifted by Second") {
        const Nodalys::RandomParameters params{.Distribution = Nodalys::RandomDistribution::POISSON,
                                               .TimeStep = 1e-3,
                                               .Delay = 0.0,
                                               .First = 3.0,
                                               .Second = 1.5};
        const Nodalys::TransientNoiseSource source = Nodalys::MakeRandomSource(params);
        source.ResetSeed(17);

        for (int index = 1; index <= 50; ++index) {
            const double offset = Eval(source, params.Second, static_cast<double>(index) * 1e-3) - 1.5;
            CHECK(offset >= 0.0);
            CHECK_THAT(offset, WithinAbs(std::round(offset), 1e-12));
        }
    }
}

TEST_CASE("Seeded transient noise and random sources repeat identically and hold DC outside transient",
          "[transient_noise]") {
    Nodalys::CircuitBuilder builder;
    const Nodalys::NodeId n1 = builder.AddNode();
    const Nodalys::NodeId n2 = builder.AddNode();
    builder.AddVnoise("V1", n1, Nodalys::GROUND_NODE,
                      {.Offset = 2.0, .WhiteAmplitude = 0.4, .TimeStep = 1e-5, .FlickerAmplitude = 0.1});
    builder.AddIrandom(
        "I1", Nodalys::GROUND_NODE, n2,
        {.Distribution = Nodalys::RandomDistribution::UNIFORM, .TimeStep = 1e-5, .First = 1e-3, .Second = 2e-3});
    builder.AddResistor("R1", n1, n2, 1e3);
    builder.AddResistor("R2", n2, Nodalys::GROUND_NODE, 1e3);
    const auto built = builder.Build();
    REQUIRE(built.has_value());
    const Nodalys::Circuit &circuit = *built;

    // Operating point holds DC levels (V1 = 2.0 V, I1 = 2 mA)
    const auto op = Nodalys::ComputeOperatingPoint(circuit);
    REQUIRE(op.has_value());
    CHECK_THAT(op->NodeVoltages[static_cast<std::size_t>(n1)], WithinAbs(2.0, 1e-9));
    CHECK_THAT(op->NodeVoltages[static_cast<std::size_t>(n2)], WithinAbs(2.0, 1e-9));

    const Nodalys::TransientConfig seeded_a{.StopTime = 1e-4, .TimeStep = 1e-5, .MaxStep = 1e-5, .Seed = 12345};
    const Nodalys::TransientConfig seeded_b{.StopTime = 1e-4, .TimeStep = 1e-5, .MaxStep = 1e-5, .Seed = 67890};
    const Nodalys::TransientConfig unseeded{.StopTime = 1e-4, .TimeStep = 1e-5, .MaxStep = 1e-5, .Seed = 0};

    const auto run1 = Nodalys::ComputeTransient(circuit, seeded_a);
    const auto run2 = Nodalys::ComputeTransient(circuit, seeded_a);
    const auto run_other = Nodalys::ComputeTransient(circuit, seeded_b);
    const auto run_unseeded = Nodalys::ComputeTransient(circuit, unseeded);

    REQUIRE(run1.has_value());
    REQUIRE(run2.has_value());
    REQUIRE(run_other.has_value());
    REQUIRE(run_unseeded.has_value());
    CHECK(run1->NodeVoltages == run2->NodeVoltages);
    CHECK(run1->NodeVoltages != run_other->NodeVoltages);
}

TEST_CASE("Netlist parser handles TRNOISE, TRRANDOM, and .OPTIONS SEED", "[transient_noise]") {
    const std::string netlist = R"(
* Noise and random source test
.OPTIONS SEED=77
V1 1 0 TRNOISE(0.2 0.5 1u 1.0 0.1 0.3 2u 3u)
I1 1 0 TRRANDOM(2 1u 0 0.5 1.0)
R1 1 0 1k
.TRAN 1u 20u
.END
)";
    const auto parsed = Nodalys::ParseNetlist(netlist);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->Analyses.size() == 1);
    const auto *config = std::get_if<Nodalys::TransientConfig>(&parsed->Analyses[0].Config);
    REQUIRE(config != nullptr);
    CHECK(config->Seed == 77);

    const auto result = Nodalys::ComputeTransient(parsed->CircuitInstance, *config);
    REQUIRE(result.has_value());
    CHECK(result->Times.size() > 5);
}
