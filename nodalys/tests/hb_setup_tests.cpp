/**
 * @file    hb_setup_tests.cpp
 * @brief   Tests for the checks a harmonic balance makes of a circuit before it solves, seen through the analysis.
 */

#include <catch2/catch_test_macros.hpp>

#include <nodalys/nodalys.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <variant>

namespace {

std::expected<Nodalys::HarmonicBalanceResult, std::string> RunHarmonicBalance(const std::string_view netlist) {
    const auto parsed = Nodalys::ParseNetlist(netlist);
    REQUIRE(parsed.has_value());
    const auto &config = std::get<Nodalys::HarmonicBalanceConfig>(parsed->Analyses[0].Config);
    return Nodalys::ComputeHarmonicBalance(parsed->CircuitInstance, config);
}

bool HasWarningAbout(const Nodalys::HarmonicBalanceResult &result, const std::string_view text) {
    return std::ranges::any_of(result.Warnings,
                               [&](const std::string &warning) { return warning.find(text) != std::string::npos; });
}

TEST_CASE("A pulse whose edge is faster than the time grid draws one warning", "[nodalys][hb][setup]") {
    const auto result = RunHarmonicBalance("* fast edge\n"
                                           "V1 1 0 PULSE(0 5 0 1n 1n 400u 1m)\n"
                                           "V2 3 0 PULSE(0 5 0 1n 1n 400u 1m)\n"
                                           "R1 1 2 1k\n"
                                           "R2 2 0 1k\n"
                                           "R3 3 2 1k\n"
                                           ".hb 1k 6 samples=32\n"
                                           ".end\n");
    REQUIRE(result.has_value());
    CHECK(std::ranges::count_if(
              result->Warnings,
              [](const std::string &warning) { return warning.find("faster than the time grid") != std::string::npos; })
          == 1);
}

TEST_CASE("A pulse whose edge the time grid resolves draws no warning", "[nodalys][hb][setup]") {
    const auto result = RunHarmonicBalance("* slow edge\n"
                                           "V1 1 0 PULSE(0 5 0 100u 100u 300u 1m)\n"
                                           "R1 1 0 1k\n"
                                           ".hb 1k 6 samples=64\n"
                                           ".end\n");
    REQUIRE(result.has_value());
    CHECK_FALSE(HasWarningAbout(*result, "faster than the time grid"));
}

TEST_CASE("A source that is not a multiple of the fundamental is refused whichever kind it is",
          "[nodalys][hb][setup]") {
    const auto voltage = RunHarmonicBalance("* v\nV1 1 0 SIN(0 1 1.5k)\nR1 1 0 1k\n.hb 1k 4 samples=32\n.end\n");
    REQUIRE_FALSE(voltage.has_value());
    CHECK(voltage.error().find("not an integer multiple") != std::string::npos);

    const auto current = RunHarmonicBalance("* i\nI1 0 1 SIN(0 1m 1.5k)\nR1 1 0 1k\n.hb 1k 4 samples=32\n.end\n");
    REQUIRE_FALSE(current.has_value());
    CHECK(current.error().find("not an integer multiple") != std::string::npos);
}

} // namespace
