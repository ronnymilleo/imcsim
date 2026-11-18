/**
 * @file    frequency_sweep_tests.cpp
 * @brief   Tests of the frequency grids of the AC, noise, AC sensitivity and S-parameter sweeps: each sweep type in
 *          the `.ac` and `.noise` directives, what they refuse, and that the decade grids of the four analyses are
 *          the ones they were before one function made them.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <nodalys/nodalys.h>

#include <format>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

using Catch::Matchers::WithinRel;

/// A low-pass whose only source drives the AC, noise and AC sensitivity analyses.
const std::string RC_PARTS = "V1 in 0 DC 0 AC 1\n"
                             "R1 in out 1k\n"
                             "C1 out 0 159.15494309189535n\n";

/// A one-port load for the S-parameters.
const std::string PORT_PARTS = "Vsport1 1 0 dc 0 portnum 1 z0 50\n"
                               "R1 1 0 50\n";

Nodalys::ParsedNetlist Parse(const std::string &parts, const std::string &directive) {
    auto parsed = Nodalys::ParseNetlist("sweep\n" + parts + directive + "\n.end\n");
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->Analyses.size() == 1);
    return std::move(*parsed);
}

std::vector<double> AcGrid(const std::string &directive) {
    const Nodalys::ParsedNetlist parsed = Parse(RC_PARTS, directive);
    const auto result =
        Nodalys::ComputeACSweep(parsed.CircuitInstance, std::get<Nodalys::AcSweepConfig>(parsed.Analyses[0].Config));
    REQUIRE(result.has_value());
    return result->Frequencies;
}

std::vector<double> NoiseGrid(const std::string &directive) {
    const Nodalys::ParsedNetlist parsed = Parse(RC_PARTS, directive);
    const auto result =
        Nodalys::ComputeNoise(parsed.CircuitInstance, std::get<Nodalys::NoiseConfig>(parsed.Analyses[0].Config));
    REQUIRE(result.has_value());
    return result->Frequencies;
}

std::vector<double> AcSensitivityGrid(const std::string &directive) {
    const Nodalys::ParsedNetlist parsed = Parse(RC_PARTS, directive);
    const auto result =
        Nodalys::ComputeAcSensitivity(parsed.CircuitInstance,
                                      std::get<Nodalys::AcSensitivityConfig>(parsed.Analyses[0].Config));
    REQUIRE(result.has_value());
    return result->Frequencies;
}

std::vector<double> SParameterGrid(const std::string &directive) {
    const Nodalys::ParsedNetlist parsed = Parse(PORT_PARTS, directive);
    const auto result = Nodalys::ComputeSParameters(parsed.CircuitInstance,
                                                    std::get<Nodalys::SParameterConfig>(parsed.Analyses[0].Config));
    REQUIRE(result.has_value());
    return result->Frequencies;
}

void CheckGrid(const std::vector<double> &actual, const std::vector<double> &expected) {
    REQUIRE(actual.size() == expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index) {
        CHECK_THAT(actual[index], WithinRel(expected[index], 1e-12));
    }
}

std::string FindRefusal(const std::string &directive) {
    const auto parsed = Nodalys::ParseNetlist("sweep\n" + RC_PARTS + directive + "\n.end\n");
    REQUIRE_FALSE(parsed.has_value());
    return parsed.error();
}

/// Four points per decade from 100 Hz to 5 kHz, which stops short of 5 kHz: the last point is not above the stop.
const std::vector<double> DECADE_GRID_UNEVEN_END =
    {100.0, 177.82794100389228, 316.22776601683796, 562.34132519034910, 1000.0, 1778.2794100389228, 3162.2776601683795};

/// Three points per decade from 100 Hz to 10 kHz, both ends on the grid.
const std::vector<double> DECADE_GRID_BOTH_ENDS =
    {100.0, 215.44346900318837, 464.15888336127790, 1000.0, 2154.4346900318837, 4641.5888336127790, 10000.0};

} // namespace

TEST_CASE("The decade grid of each analysis is the one it had before they shared a function",
          "[nodalys][frequency_sweep]") {
    // Values computed by the code each analysis had of its own, before the change
    CheckGrid(AcGrid(".ac dec 4 100 5k"), DECADE_GRID_UNEVEN_END);
    CheckGrid(AcGrid(".ac dec 3 100 10k"), DECADE_GRID_BOTH_ENDS);
    CheckGrid(NoiseGrid(".noise v(out) V1 dec 4 100 5k"), DECADE_GRID_UNEVEN_END);
    CheckGrid(NoiseGrid(".noise v(out) V1 dec 3 100 10k"), DECADE_GRID_BOTH_ENDS);
    CheckGrid(AcSensitivityGrid(".sens v(out) ac dec 4 100 5k"), DECADE_GRID_UNEVEN_END);
    CheckGrid(AcSensitivityGrid(".sens v(out) ac dec 3 100 10k"), DECADE_GRID_BOTH_ENDS);
    CheckGrid(SParameterGrid(".sp dec 4 100 5k"), DECADE_GRID_UNEVEN_END);
    CheckGrid(SParameterGrid(".sp dec 3 100 10k"), DECADE_GRID_BOTH_ENDS);
}

TEST_CASE("A sweep with no type is a decade sweep, and so is one made in code", "[nodalys][frequency_sweep]") {
    CheckGrid(AcGrid(".ac 3 100 10k"), DECADE_GRID_BOTH_ENDS);
    CheckGrid(NoiseGrid(".noise v(out) V1 3 100 10k"), DECADE_GRID_BOTH_ENDS);

    const Nodalys::ParsedNetlist parsed = Parse(RC_PARTS, ".ac 3 100 10k");
    CHECK(std::get<Nodalys::AcSweepConfig>(parsed.Analyses[0].Config).SweepType == Nodalys::FrequencySweepType::DECADE);
    CHECK(Nodalys::AcSweepConfig{}.SweepType == Nodalys::FrequencySweepType::DECADE);
    CHECK(Nodalys::NoiseConfig{}.SweepType == Nodalys::FrequencySweepType::DECADE);
}

TEST_CASE("The .ac directive sweeps by decade, octave or in line as it says", "[nodalys][frequency_sweep]") {
    SECTION("dec gives the points per decade, with both ends") {
        CheckGrid(AcGrid(".ac dec 1 100 1k"), {100.0, 1000.0});
        CheckGrid(AcGrid(".AC DEC 2 100 1k"), {100.0, 316.22776601683796, 1000.0});
    }
    SECTION("oct gives the points per octave") {
        // 100 * 2^(k/2) stays at or under 1 kHz up to 800 Hz
        CheckGrid(AcGrid(".ac oct 2 100 1k"),
                  {100.0, 141.42135623730951, 200.0, 282.84271247461902, 400.0, 565.68542494923802, 800.0});
        CheckGrid(AcGrid(".ac oct 1 100 800"), {100.0, 200.0, 400.0, 800.0});
    }
    SECTION("lin gives the points in all, from the start to the stop") {
        CheckGrid(AcGrid(".ac lin 5 100 1k"), {100.0, 325.0, 550.0, 775.0, 1000.0});
        CheckGrid(AcGrid(".ac lin 1 100 1k"), {100.0});
        CheckGrid(AcGrid(".ac lin 3 500 500"), {500.0});
    }
}

TEST_CASE("The .noise directive sweeps by decade, octave or in line as it says", "[nodalys][frequency_sweep]") {
    CheckGrid(NoiseGrid(".noise v(out) V1 dec 2 100 1k"), {100.0, 316.22776601683796, 1000.0});
    CheckGrid(NoiseGrid(".noise v(out) V1 oct 1 100 800"), {100.0, 200.0, 400.0, 800.0});
    CheckGrid(NoiseGrid(".noise v(out) V1 lin 5 100 1k"), {100.0, 325.0, 550.0, 775.0, 1000.0});
}

TEST_CASE("The .ac and .noise directives refuse a bad sweep and quote the line", "[nodalys][frequency_sweep]") {
    for (const std::string prefix : {".ac", ".noise v(out) V1"}) {
        const std::string directive = prefix.starts_with(".ac") ? ".ac" : ".noise";
        const auto refuse = [&prefix](const std::string &sweep) {
            return FindRefusal(std::format("{} {}", prefix, sweep));
        };

        const std::string zero_points = refuse("dec 0 100 1k");
        CHECK(zero_points.contains("number of points"));
        CHECK(zero_points.contains(directive));
        CHECK(zero_points.contains(std::format("'{} dec 0 100 1k'", prefix)));
        CHECK(refuse("dec 2.5 100 1k").contains("number of points"));
        CHECK(refuse("lin -3 100 1k").contains("number of points"));
        CHECK(refuse("dec abc 100 1k").contains("number of points"));
        CHECK(refuse("dec 10 0 1k").contains("frequency range"));
        CHECK(refuse("lin 5 0 1k").contains("frequency range"));
        CHECK(refuse("dec 10 1k 100").contains("frequency range"));
        CHECK(refuse("dec 10 abc 1k").contains("frequency range"));
        CHECK(refuse("dec 10 100 abc").contains("frequency range"));
        CHECK(refuse("dec 10 100").contains("Malformed"));
    }
}
