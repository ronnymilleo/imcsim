/**
 * @file    worst_case_tests.cpp
 * @brief   Tests of analytical worst-case bounds from DC and AC sensitivities and part tolerances.
 */

#include "simulation/worst_case.h"

#include "circuit/circuit.h"
#include "circuit/components/capacitor.h"
#include "circuit/components/ground.h"
#include "circuit/components/resistor.h"
#include "circuit/components/voltage_source.h"
#include "nodalys_in_use.h"
#include "simulation/simulator.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <numbers>
#include <optional>
#include <string>

namespace {

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

/**
 * @struct  TolerancedDivider
 * @brief   A 10 V resistive divider of two 10 kOhm, 5% resistors to ground.
 */
struct TolerancedDivider {
    Core::VoltageSource Source;
    Core::Resistor Top;
    Core::Resistor Bottom;
    Core::Ground Reference;
    Core::Circuit Circuit;

    TolerancedDivider() {
        Source.SetName("V1");
        Source.SetValue(10.0);
        Top.SetName("R1");
        Top.SetValue(10e3);
        Top.SetTolerance(5.0);
        Bottom.SetName("R2");
        Bottom.SetValue(10e3);
        Bottom.SetTolerance(5.0);
        Circuit.Add(Source, {1, 0});
        Circuit.Add(Top, {1, 2});
        Circuit.Add(Bottom, {2, 0});
        Circuit.Add(Reference, {0});
    }
};

/**
 * @struct  TolerancedLowPass
 * @brief   An RC low-pass filter with 1 kHz corner, 10% resistor, 5% capacitor, and an AC source whose tolerance is
 *          ignored.
 */
struct TolerancedLowPass {
    Core::VoltageSource Source;
    Core::Resistor Series;
    Core::Capacitor Shunt;
    Core::Ground Reference;
    Core::Circuit Circuit;

    TolerancedLowPass() {
        Source.SetName("V1");
        Source.SetSourceType(Core::Source::SourceType::AC);
        Source.SetAC({.Magnitude = 1.0});
        // Non-DC source tolerance must be ignored just like in Monte Carlo
        Source.SetTolerance(5.0);
        Series.SetName("R1");
        Series.SetValue(1e3);
        Series.SetTolerance(10.0);
        Shunt.SetName("C1");
        Shunt.SetValue(1.0 / (2.0 * std::numbers::pi * 1e3 * 1e3));
        Shunt.SetTolerance(5.0);
        Circuit.Add(Source, {1, 0});
        Circuit.Add(Series, {1, 2});
        Circuit.Add(Shunt, {2, 0});
        Circuit.Add(Reference, {0});
    }
};

const Core::PartSensitivity &FindDCPart(const Core::Sensitivity &sensitivity, const std::string &name) {
    const auto found = std::ranges::find(sensitivity.Parts, name, &Core::PartSensitivity::Name);
    REQUIRE(found != sensitivity.Parts.end());
    return *found;
}

const Core::ACPartSensitivity &FindACPart(const Core::ACSensitivity &sensitivity, const std::string &name) {
    const auto found = std::ranges::find(sensitivity.Parts, name, &Core::ACPartSensitivity::Name);
    REQUIRE(found != sensitivity.Parts.end());
    return *found;
}

void CheckDividerWorstCase(const Core::SensitivityRun &run) {
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    const Core::Sensitivity &sensitivity = *run.Result;
    // Two equal 10k 5% resistors from 10 V: nominal 5 V, each resistor moves output +/- 25 mV/% and 125 mV at 5%
    const Core::PartSensitivity &top = FindDCPart(sensitivity, "R1");
    const Core::PartSensitivity &bottom = FindDCPart(sensitivity, "R2");
    const Core::PartSensitivity &source = FindDCPart(sensitivity, "V1");
    REQUIRE(top.TolerancePercent == 5.0);
    REQUIRE(bottom.TolerancePercent == 5.0);
    CHECK_FALSE(source.TolerancePercent.has_value());

    REQUIRE(Core::GetPartWorstCaseChange(top).has_value());
    CHECK_THAT(*Core::GetPartWorstCaseChange(top), WithinRel(0.125, 1e-4));
    REQUIRE(Core::GetPartWorstCaseChange(bottom).has_value());
    CHECK_THAT(*Core::GetPartWorstCaseChange(bottom), WithinRel(0.125, 1e-4));
    CHECK_FALSE(Core::GetPartWorstCaseChange(source).has_value());

    const Core::DCWorstCase worst_case = Core::ComputeWorstCase(sensitivity);
    CHECK(worst_case.TolerancedPartCount == 2);
    CHECK_THAT(worst_case.ExtremeVolts, WithinRel(0.250, 1e-4));
    CHECK_THAT(worst_case.RssVolts, WithinRel(0.125 * std::numbers::sqrt2, 1e-4));
}

} // namespace

TEST_CASE("DC worst case bounds a resistive divider on both ngspice and Nodalys", "[worst_case][simulator]") {
    const TolerancedDivider divider;
    CheckDividerWorstCase(Core::RunSensitivity(divider.Circuit, {.OutputNode = 2}));
    const TestSupport::NodalysInUse nodalys;
    CheckDividerWorstCase(Core::RunSensitivity(divider.Circuit, {.OutputNode = 2}));
}

TEST_CASE("AC worst case bounds an RC low-pass at its corner on Nodalys", "[worst_case][nodalys]") {
    const TestSupport::NodalysInUse nodalys;
    const TolerancedLowPass filter;
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
    REQUIRE(sensitivity.Frequencies.size() == 5);

    const Core::ACPartSensitivity &resistor = FindACPart(sensitivity, "R1");
    const Core::ACPartSensitivity &capacitor = FindACPart(sensitivity, "C1");
    CHECK(resistor.TolerancePercent == 10.0);
    CHECK(capacitor.TolerancePercent == 5.0);
    // The non-DC source was given a 5 % tolerance, which must not propagate
    CHECK(std::ranges::none_of(sensitivity.Parts, [](const Core::ACPartSensitivity &part) {
        return part.Name == "V1" && part.TolerancePercent.has_value();
    }));

    // At 1 kHz (index 2), each 1 % change moves gain by -0.043429 dB and phase by -0.28648 deg
    const double unit_gain_db = 20.0 / std::numbers::ln10 * 0.005;
    const double unit_phase_deg = 180.0 / std::numbers::pi * 0.005;
    REQUIRE(Core::GetACPartGainWorstCase(resistor, 2).has_value());
    CHECK_THAT(*Core::GetACPartGainWorstCase(resistor, 2), WithinRel(10.0 * unit_gain_db, 1e-4));
    REQUIRE(Core::GetACPartPhaseWorstCase(capacitor, 2).has_value());
    CHECK_THAT(*Core::GetACPartPhaseWorstCase(capacitor, 2), WithinRel(5.0 * unit_phase_deg, 1e-4));

    const Core::ACWorstCasePoint corner = Core::ComputeACWorstCaseAt(sensitivity, 2);
    CHECK(corner.TolerancedPartCount == 2);
    REQUIRE(corner.ExtremeGainDecibels.has_value());
    REQUIRE(corner.RssGainDecibels.has_value());
    REQUIRE(corner.ExtremePhaseDegrees.has_value());
    REQUIRE(corner.RssPhaseDegrees.has_value());
    CHECK_THAT(*corner.ExtremeGainDecibels, WithinRel(15.0 * unit_gain_db, 1e-4));
    CHECK_THAT(*corner.RssGainDecibels, WithinRel(std::hypot(10.0, 5.0) * unit_gain_db, 1e-4));
    CHECK_THAT(*corner.ExtremePhaseDegrees, WithinRel(15.0 * unit_phase_deg, 1e-4));
    CHECK_THAT(*corner.RssPhaseDegrees, WithinRel(std::hypot(10.0, 5.0) * unit_phase_deg, 1e-4));

    const Core::ACWorstCaseSweep sweep = Core::ComputeACWorstCaseSweep(sensitivity);
    CHECK(sweep.TolerancedPartCount == 2);
    REQUIRE(sweep.Frequencies.size() == 5);
    CHECK(sweep.ExtremeGainDecibels[2] == corner.ExtremeGainDecibels);
    CHECK(sweep.RssPhaseDegrees[2] == corner.RssPhaseDegrees);
}

TEST_CASE("AC worst case yields no bounds at frequencies where the output is zero", "[worst_case]") {
    Core::ACSensitivity sensitivity;
    sensitivity.Frequencies = {100.0, 1e3};
    sensitivity.Output = {0.0, 0.5};
    sensitivity.Parts = {{.Name = "R1",
                          .Unit = "Ohm",
                          .Value = 1e3,
                          .MagnitudeDecibelsPerPercent = {std::nullopt, -0.05},
                          .PhaseDegreesPerPercent = {std::nullopt, -0.3},
                          .TolerancePercent = 5.0}};

    const Core::ACWorstCasePoint zero_point = Core::ComputeACWorstCaseAt(sensitivity, 0);
    CHECK(zero_point.TolerancedPartCount == 1);
    CHECK_FALSE(zero_point.ExtremeGainDecibels.has_value());
    CHECK_FALSE(zero_point.RssGainDecibels.has_value());
    CHECK_FALSE(zero_point.ExtremePhaseDegrees.has_value());
    CHECK_FALSE(zero_point.RssPhaseDegrees.has_value());

    const Core::ACWorstCasePoint active_point = Core::ComputeACWorstCaseAt(sensitivity, 1);
    REQUIRE(active_point.ExtremeGainDecibels.has_value());
    CHECK_THAT(*active_point.ExtremeGainDecibels, WithinAbs(0.25, 1e-12));
    REQUIRE(active_point.ExtremePhaseDegrees.has_value());
    CHECK_THAT(*active_point.ExtremePhaseDegrees, WithinAbs(1.5, 1e-12));
}

TEST_CASE("DC worst case handles zero tolerances, non-DC sources, zero sensitivities and mixed parts",
          "[worst_case][simulator]") {
    Core::VoltageSource ac_source;
    ac_source.SetName("V1");
    ac_source.SetSourceType(Core::Source::SourceType::AC);
    ac_source.SetAC({.Offset = 5.0, .Magnitude = 1.0});
    ac_source.SetTolerance(10.0);

    Core::VoltageSource dc_source;
    dc_source.SetName("V2");
    dc_source.SetValue(2.0);
    dc_source.SetTolerance(1.0);

    Core::Resistor top;
    top.SetName("R1");
    top.SetValue(1e3);
    top.SetTolerance(5.0);

    Core::Resistor bottom;
    bottom.SetName("R2");
    bottom.SetValue(1e3);

    Core::Capacitor shunt;
    shunt.SetName("C1");
    shunt.SetValue(1e-6);
    shunt.SetTolerance(10.0);

    const Core::Ground reference;
    Core::Circuit circuit;
    circuit.Add(ac_source, {1, 0});
    circuit.Add(top, {1, 2});
    circuit.Add(bottom, {2, 3});
    circuit.Add(dc_source, {3, 0});
    circuit.Add(shunt, {2, 0});
    circuit.Add(reference, {0});

    // Without tolerances on any sensitivity entry, counts and bounds are zero
    const Core::Sensitivity empty_sens;
    const Core::DCWorstCase empty_wc = Core::ComputeWorstCase(empty_sens);
    CHECK(empty_wc.TolerancedPartCount == 0);
    CHECK(empty_wc.ExtremeVolts == 0.0);
    CHECK(empty_wc.RssVolts == 0.0);

    const Core::SensitivityRun run = Core::RunSensitivity(circuit, {.OutputNode = 2});
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    const Core::Sensitivity &sensitivity = *run.Result;
    // Non-DC source tolerance is ignored, untoleranced R2 has no tolerance, C1 has zero sensitivity at DC
    CHECK_FALSE(FindDCPart(sensitivity, "V1").TolerancePercent.has_value());
    CHECK_FALSE(FindDCPart(sensitivity, "R2").TolerancePercent.has_value());
    CHECK(FindDCPart(sensitivity, "C1").TolerancePercent == 10.0);
    REQUIRE(Core::GetPartWorstCaseChange(FindDCPart(sensitivity, "C1")).has_value());
    CHECK_THAT(*Core::GetPartWorstCaseChange(FindDCPart(sensitivity, "C1")), WithinAbs(0.0, 1e-12));

    // Node 2 is (5 + 2) / 2 = 3.5 V: R1 moves it by (2 - 5) * 1k / 4k = -0.75 V per 100%, V2 moves it by 0.5 * 2 = 1 V
    // per 100%
    const double delta_r1 = 0.05 * 0.75;
    const double delta_v2 = 0.01 * 1.0;
    const Core::DCWorstCase worst_case = Core::ComputeWorstCase(sensitivity);
    CHECK(worst_case.TolerancedPartCount == 3);
    CHECK_THAT(worst_case.ExtremeVolts, WithinRel(delta_r1 + delta_v2, 1e-4));
    CHECK_THAT(worst_case.RssVolts, WithinRel(std::hypot(delta_r1, delta_v2), 1e-4));
}
