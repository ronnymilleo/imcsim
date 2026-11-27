/**
 * @file    ac_sensitivity_tests.cpp
 * @brief   Tests of the AC sensitivity analysis: how the figures of the elements become those of each part, the checks
 *          before a run, the refusal of ngspice and the run on Nodalys against the closed form of an RC filter.
 */

#include "simulation/ac_sensitivity.h"

#include "backends/ngspice/ngspice_backend.h"
#include "circuit/circuit.h"
#include "circuit/components/capacitor.h"
#include "circuit/components/ground.h"
#include "circuit/components/potentiometer.h"
#include "circuit/components/resistor.h"
#include "circuit/components/subcircuit.h"
#include "circuit/components/voltage_source.h"
#include "circuit/subcircuit.h"
#include "nodalys_in_use.h"
#include "simulation/check_sensitivity.h"
#include "simulation/simulation_backend.h"
#include "simulation/simulator.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <memory>
#include <numbers>
#include <optional>
#include <set>
#include <string>

namespace {

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

/// A 1 kOhm and 159.155 nF low-pass, which has its corner at 1 kHz, driven by 1 V of AC at node 1; the output is
/// node 2.
struct LowPass {
    Core::VoltageSource Source;
    Core::Resistor Series;
    Core::Capacitor Shunt;
    Core::Ground Reference;
    Core::Circuit Circuit;

    LowPass() {
        Source.SetName("V1");
        Source.SetSourceType(Core::Source::SourceType::AC);
        Source.SetAC({.Magnitude = 1.0});
        Series.SetName("R1");
        Series.SetValue(1e3);
        Shunt.SetName("C1");
        Shunt.SetValue(1.0 / (2.0 * std::numbers::pi * 1e3 * 1e3));
        Circuit.Add(Source, {1, 0});
        Circuit.Add(Series, {1, 2});
        Circuit.Add(Shunt, {2, 0});
        Circuit.Add(Reference, {0});
    }
};

/**
 * @brief   Figures that are the same decibels and degrees at both of the two frequencies.
 */
Core::ACElementFigures MakeFigures(const double value, const double decibels, const double degrees) {
    return {.Value = value,
            .MagnitudeDecibelsPerPercent = {decibels, decibels},
            .PhaseDegreesPerPercent = {degrees, degrees}};
}

const Core::ACPartSensitivity &FindPart(const Core::ACSensitivity &sensitivity, const std::string &name) {
    const auto found = std::ranges::find(sensitivity.Parts, name, &Core::ACPartSensitivity::Name);
    REQUIRE(found != sensitivity.Parts.end());
    return *found;
}

} // namespace

TEST_CASE("The AC sensitivity lists the parts with a value and leaves out the ones the solver gave no figures for",
          "[ac_sensitivity]") {
    const LowPass filter;
    const Core::ACElementFiguresByName elements = {{"r1", MakeFigures(1e3, -0.04, -0.28)},
                                                   {"c1", MakeFigures(160e-9, -0.04, -0.28)}};

    const Core::ACSensitivity sensitivity =
        Core::BuildACSensitivity(filter.Circuit, {.OutputNode = 2, .InputSource = "V1"}, elements);

    CHECK(sensitivity.OutputNode == 2);
    CHECK(sensitivity.InputSource == "V1");
    // The source has no element in the figures, and the ground has no value, so only the two passives are listed
    REQUIRE(sensitivity.Parts.size() == 2);
    CHECK(sensitivity.Parts[0].Name == "R1");
    CHECK(sensitivity.Parts[0].Unit == "Ohm");
    CHECK(sensitivity.Parts[0].Value == 1e3);
    CHECK(sensitivity.Parts[1].Name == "C1");
    CHECK(sensitivity.Parts[0].MagnitudeDecibelsPerPercent[1] == -0.04);
}

TEST_CASE("A potentiometer reports the sum of its halves, and none where either half has none", "[ac_sensitivity]") {
    Core::Potentiometer pot;
    pot.SetName("RV1");
    pot.SetValue(10e3);
    Core::Circuit circuit;
    circuit.Add(pot, {1, 0, 2});
    Core::ACElementFigures first = MakeFigures(7.5e3, -0.03, -0.2);
    Core::ACElementFigures second = MakeFigures(2.5e3, 0.01, 0.05);
    second.MagnitudeDecibelsPerPercent[1] = std::nullopt;

    const Core::ACSensitivity sensitivity =
        Core::BuildACSensitivity(circuit,
                                 {.OutputNode = 2},
                                 {{"r-rv1a", std::move(first)}, {"r-rv1b", std::move(second)}});

    REQUIRE(sensitivity.Parts.size() == 1);
    const Core::ACPartSensitivity &part = sensitivity.Parts[0];
    CHECK(part.Value == 10e3);
    REQUIRE(part.MagnitudeDecibelsPerPercent[0].has_value());
    CHECK_THAT(*part.MagnitudeDecibelsPerPercent[0], WithinAbs(-0.02, 1e-12));
    CHECK_FALSE(part.MagnitudeDecibelsPerPercent[1].has_value());
    REQUIRE(part.PhaseDegreesPerPercent[1].has_value());
    CHECK_THAT(*part.PhaseDegreesPerPercent[1], WithinAbs(-0.15, 1e-12));
}

TEST_CASE("A block reports the element inside it that moves the response most, named by its path", "[ac_sensitivity]") {
    Core::SubcircuitInstance first("filter", {"A"});
    first.SetName("X1");
    Core::SubcircuitInstance second("filter", {"A"});
    second.SetName("X2");
    Core::SubcircuitInstance empty("nothing", {"A"});
    empty.SetName("X3");
    Core::Circuit circuit;
    circuit.Add(first, {1});
    circuit.Add(second, {1});
    circuit.Add(empty, {1});
    const Core::ACElementFiguresByName elements = {
        {"x1.r1", MakeFigures(1e3, -0.5, 0.0)},
        {"x2.r1", MakeFigures(1e3, -0.1, 0.0)},
        {"x2.c1", MakeFigures(1e-6, 0.3, 0.0)},
        // A nested block is named by the outer instance and the path inside it
        {"x2.x4.l7", MakeFigures(1e-3, 0.8, 0.0)},
        // What imcsim wrote for a part inside the block is not one of its values
        {"x2.x-u1.r2", MakeFigures(1e3, 9.0, 0.0)},
        {"x2.r-u1", MakeFigures(1e3, 9.0, 0.0)}};

    const Core::ACSensitivity sensitivity = Core::BuildACSensitivity(circuit, {.OutputNode = 1}, elements);

    // X3 holds nothing with a value, so it is not listed
    REQUIRE(sensitivity.Parts.size() == 2);
    CHECK(FindPart(sensitivity, "X1").Element == "R1");
    CHECK(FindPart(sensitivity, "X1").Unit == "Ohm");
    CHECK(FindPart(sensitivity, "X2").Element == "X4.L7");
    CHECK(FindPart(sensitivity, "X2").Unit == "H");
    CHECK(FindPart(sensitivity, "X2").Value == 1e-3);
}

TEST_CASE("The AC sensitivity analysis checks its output, its source and its range", "[ac_sensitivity]") {
    const LowPass filter;
    const Core::ACSensitivitySettings valid{.OutputNode = 2, .InputSource = "V1"};

    CHECK_FALSE(Core::CheckACSensitivitySettings(filter.Circuit, valid).has_value());
    CHECK(Core::CheckACSensitivitySettings(filter.Circuit, {.OutputNode = 0, .InputSource = "V1"}).has_value());
    CHECK(Core::CheckACSensitivitySettings(filter.Circuit, {.OutputNode = 42, .InputSource = "V1"}).has_value());
    CHECK(Core::CheckACSensitivitySettings(filter.Circuit, {.OutputNode = 2, .InputSource = ""}).has_value());
    // A resistor is not a source
    CHECK(Core::CheckACSensitivitySettings(filter.Circuit, {.OutputNode = 2, .InputSource = "R1"}).has_value());
    Core::ACSensitivitySettings backwards = valid;
    backwards.StartFrequency = 1e6;
    backwards.StopFrequency = 10.0;
    CHECK(Core::CheckACSensitivitySettings(filter.Circuit, backwards).has_value());
}

TEST_CASE("ngspice does not run the AC sensitivity analysis and says why, so the editor can disable it",
          "[ac_sensitivity]") {
    const Core::NgspiceBackend ngspice;
    const std::optional<std::string> reason =
        Core::FindUnsupported(ngspice, std::set{Core::ComponentType::RESISTOR}, Core::AnalysisKind::AC_SENSITIVITY);
    REQUIRE(reason.has_value());
    CHECK(*reason == "ngspice does not run the AC sensitivity analysis");

    const LowPass filter;
    const Core::ACSensitivityRun run = Core::RunACSensitivity(filter.Circuit, {.OutputNode = 2, .InputSource = "V1"});
    REQUIRE_FALSE(run.Result.has_value());
    CHECK(run.Result.error().contains("does not run"));
}

TEST_CASE("Nodalys gives the AC sensitivity of an RC low-pass at its corner", "[ac_sensitivity][nodalys]") {
    const TestSupport::NodalysInUse nodalys;
    const LowPass filter;
    const Core::ACSensitivitySettings settings{.OutputNode = 2,
                                               .InputSource = "V1",
                                               .StartFrequency = 100.0,
                                               .StopFrequency = 10e3,
                                               .PointsPerDecade = 2};

    const Core::ACSensitivityRun run = Core::RunACSensitivity(filter.Circuit, settings);

    if (!run.Result) {
        FAIL(run.Result.error());
    }
    const Core::ACSensitivity &sensitivity = *run.Result;
    // Two points per decade from 100 Hz to 10 kHz: 100, 316, 1000, 3162 and 10000
    REQUIRE(sensitivity.Frequencies.size() == 5);
    CHECK_THAT(sensitivity.Frequencies[2], WithinRel(1e3, 1e-9));
    CHECK_THAT(std::abs(sensitivity.Output[2]), WithinRel(1.0 / std::numbers::sqrt2, 1e-6));
    // At the corner, 1 % more R or C moves the gain by -0.0434 dB and the phase by -0.286 degrees
    for (const std::string name : {"R1", "C1"}) {
        INFO(name);
        const Core::ACPartSensitivity &part = FindPart(sensitivity, name);
        REQUIRE(part.MagnitudeDecibelsPerPercent[2].has_value());
        CHECK_THAT(*part.MagnitudeDecibelsPerPercent[2], WithinAbs(-0.043429, 1e-5));
        CHECK_THAT(*part.PhaseDegreesPerPercent[2], WithinAbs(-0.28648, 1e-4));
        // Far below the corner the gain is 1 whatever the parts are
        CHECK_THAT(*part.MagnitudeDecibelsPerPercent[0], WithinAbs(0.0, 1e-3));
    }
}
