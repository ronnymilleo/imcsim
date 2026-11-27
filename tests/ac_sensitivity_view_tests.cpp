/**
 * @file    ac_sensitivity_view_tests.cpp
 * @brief   Tests of what the editor and the Output window show of an AC sensitivity: the frequency to read, the
 *          influence of each part, heat levels, table rows, curves, texts and the export figure.
 */

#include "results/ac_sensitivity_view.h"

#include "test_printers.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <numbers>

namespace {

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

constexpr double DECIBELS_PER_NEPER = 20.0 / std::numbers::ln10;

/// A part with the same figures at the three frequencies of MakeSensitivity().
Core::ACPartSensitivity
MakePart(const std::string &name, const std::optional<double> &decibels, const std::optional<double> &degrees) {
    return {.Name = name,
            .Element = {},
            .Unit = "Ohm",
            .Value = 4.7e3,
            .MagnitudeDecibelsPerPercent = {decibels, decibels, decibels},
            .PhaseDegreesPerPercent = {degrees, degrees, degrees}};
}

/// A low-pass with its corner between the second and third frequency, a resistor that moves the gain, a capacitor
/// that only moves the phase and a source that moves nothing.
Core::ACSensitivity MakeSensitivity() {
    Core::ACSensitivity sensitivity;
    sensitivity.OutputNode = 2;
    sensitivity.InputSource = "V1";
    sensitivity.Frequencies = {10.0, 100.0, 1000.0};
    sensitivity.Output = {1.0, 0.9, 0.5};
    sensitivity.Parts = {MakePart("V1", 0.0, 0.0), MakePart("C1", 0.0, -2.0), MakePart("R1", -0.5, -0.1)};
    sensitivity.Parts[0].Type = Core::ComponentType::VOLTAGE_SOURCE;
    sensitivity.Parts[0].Unit = "V";
    sensitivity.Parts[2].MagnitudeDecibelsPerPercent[2] = std::nullopt;
    sensitivity.Parts[2].PhaseDegreesPerPercent[2] = std::nullopt;
    return sensitivity;
}

} // namespace

TEST_CASE("The table starts at the -3 dB point of the response", "[ac_sensitivity_view]") {
    Core::ACSensitivity low_pass;
    low_pass.Frequencies = {10.0, 100.0, 1e3, 1e4};
    low_pass.Output = {1.0, 0.9, 0.6, 0.1};
    // 0.707 lies between 0.9 and 0.6, nearer to 0.6
    CHECK(GUI::FindCornerFrequencyIndex(low_pass) == 2);

    // A high-pass peaks at the top of the sweep, so the point is found going down
    Core::ACSensitivity high_pass;
    high_pass.Frequencies = {10.0, 100.0, 1e3, 1e4};
    high_pass.Output = {0.1, 0.6, 0.9, 1.0};
    CHECK(GUI::FindCornerFrequencyIndex(high_pass) == 1);

    // A response that never falls 3 dB reads at its peak, and an empty result at its start
    Core::ACSensitivity flat;
    flat.Frequencies = {10.0, 100.0};
    flat.Output = {1.0, 0.95};
    CHECK(GUI::FindCornerFrequencyIndex(flat) == 0);
    CHECK(GUI::FindCornerFrequencyIndex({}) == 0);
}

TEST_CASE("A frequency a user drags the marker to snaps to the nearest one on the logarithmic scale",
          "[ac_sensitivity_view]") {
    const Core::ACSensitivity sensitivity = MakeSensitivity();
    // 300 Hz is nearer to 100 Hz than to 1 kHz in decades, though not in hertz
    CHECK(GUI::FindNearestFrequencyIndex(sensitivity, 300.0) == 1);
    CHECK(GUI::FindNearestFrequencyIndex(sensitivity, 5.0) == 0);
    CHECK(GUI::FindNearestFrequencyIndex(sensitivity, 1e9) == 2);
    CHECK(GUI::FindNearestFrequencyIndex(sensitivity, -1.0) == 0);
}

TEST_CASE("The influence of a part puts its gain and its phase in one number", "[ac_sensitivity_view]") {
    const Core::ACSensitivity sensitivity = MakeSensitivity();
    // C1 moves only the phase: 2 degrees is 0.0349 rad
    CHECK_THAT(GUI::GetInfluence(sensitivity.Parts[1], 0), WithinRel(2.0 * std::numbers::pi / 180.0, 1e-12));
    // R1 moves both, as the length of the vector of its two changes
    const double nepers = 0.5 / DECIBELS_PER_NEPER;
    const double radians = 0.1 * std::numbers::pi / 180.0;
    CHECK_THAT(GUI::GetInfluence(sensitivity.Parts[2], 0), WithinRel(std::hypot(nepers, radians), 1e-12));
    // Where there is no figure, and past the end, there is none
    CHECK(GUI::GetInfluence(sensitivity.Parts[2], 2) == 0.0);
    CHECK(GUI::GetInfluence(sensitivity.Parts[2], 7) == 0.0);
    CHECK(GUI::GetInfluence(sensitivity.Parts[0], 0) == 0.0);
    CHECK_THAT(GUI::GetLargestInfluence(sensitivity, 0), WithinRel(std::hypot(nepers, radians), 1e-12));
}

TEST_CASE("The plot starts with the parts that matter most anywhere in the sweep", "[ac_sensitivity_view]") {
    const Core::ACSensitivity sensitivity = MakeSensitivity();
    // R1 (0.5 dB) before C1 (0.035 rad against 0.058 neper of R1), and the source, which moves nothing, never
    const std::vector<std::size_t> picked = GUI::FindMostInfluential(sensitivity, 5);
    REQUIRE(picked.size() == 2);
    CHECK(picked[0] == 2);
    CHECK(picked[1] == 1);
    CHECK(GUI::FindMostInfluential(sensitivity, 1) == std::vector<std::size_t>{2});
    CHECK(GUI::PRESELECTED_PARTS == 5);
}

TEST_CASE("The heat level follows the influence over three decades at the chosen frequency", "[ac_sensitivity_view]") {
    Core::ACSensitivity sensitivity = MakeSensitivity();
    sensitivity.Parts[1].PhaseDegreesPerPercent = {0.0, 0.0, 0.0};
    // At the first frequency R1 is the largest and C1 has no influence at all
    CHECK(GUI::FindACSensitivityLevel(sensitivity, "R1", 0) == 1.0f);
    CHECK(GUI::FindACSensitivityLevel(sensitivity, "C1", 0) == 0.0f);
    CHECK(GUI::FindACSensitivityLevel(sensitivity, "V1", 0) == 0.0f);
    // Where R1 has no figure, nothing is the largest, so everything sits at the bottom
    CHECK(GUI::FindACSensitivityLevel(sensitivity, "R1", 2) == 0.0f);
    // A part a decade under the largest sits at two thirds of the scale
    sensitivity.Parts[1].MagnitudeDecibelsPerPercent = {-0.05, -0.05, -0.05};
    sensitivity.Parts[2].PhaseDegreesPerPercent = {0.0, 0.0, 0.0};
    REQUIRE(GUI::FindACSensitivityLevel(sensitivity, "C1", 0).has_value());
    CHECK_THAT(*GUI::FindACSensitivityLevel(sensitivity, "C1", 0), WithinAbs(2.0 / 3.0, 1e-6));
    // A part the result does not list keeps the color of the theme
    CHECK_FALSE(GUI::FindACSensitivityLevel(sensitivity, "D1", 0).has_value());
}

TEST_CASE("The table ranks the parts at the frequency and writes each figure", "[ac_sensitivity_view]") {
    const Core::ACSensitivity sensitivity = MakeSensitivity();
    const std::vector<GUI::ACSensitivityRow> rows = GUI::MakeACSensitivityRows(sensitivity, 0);

    REQUIRE(rows.size() == 3);
    CHECK(rows[0].Part == "R1");
    CHECK(rows[0].PartName == "R1");
    CHECK(rows[0].Value == "4.7kOhm");
    CHECK(rows[0].Magnitude == "-0.5 dB");
    CHECK(rows[0].Phase == "-0.1 deg");
    CHECK(rows[0].Tolerance == "-");
    CHECK(rows[0].Share == 1.0);
    CHECK(rows[1].Part == "C1");
    CHECK(rows[1].Magnitude == "+0 dB");
    CHECK(rows[1].Phase == "-2 deg");
    CHECK(rows[1].Tolerance == "-");
    CHECK(rows[1].Share < 1.0);
    // The source moves nothing, so it ranks last with no share
    CHECK(rows[2].Part == "V1");
    CHECK(rows[2].Share == 0.0);
    CHECK_FALSE(GUI::FormatACWorstCaseSummary(sensitivity, 0).has_value());

    // Where the output was too small a part has no figures and ranks with the rest at zero
    const std::vector<GUI::ACSensitivityRow> corner = GUI::MakeACSensitivityRows(sensitivity, 2);
    REQUIRE(corner.size() == 3);
    CHECK(corner[0].PartName == "C1");
    CHECK(corner[2].Part == "R1");
    CHECK(corner[2].Magnitude == "-");
    CHECK(corner[2].Phase == "-");
}

TEST_CASE("A supply rail is named in the table as the schematic names it", "[ac_sensitivity_view]") {
    Core::ACSensitivity sensitivity = MakeSensitivity();
    sensitivity.Parts[0].Type = Core::ComponentType::VCC;
    sensitivity.Parts[0].Value = 12.0;
    const std::vector<GUI::ACSensitivityRow> rows = GUI::MakeACSensitivityRows(sensitivity, 0);
    REQUIRE(rows.size() == 3);
    CHECK(rows[2].Part == "VCC 12V");
    CHECK(rows[2].PartName == "V1");
}

TEST_CASE("The curves have a gap where the output was too small", "[ac_sensitivity_view]") {
    const std::vector<GUI::ACSensitivityCurve> curves = GUI::MakeACSensitivityCurves(MakeSensitivity());
    REQUIRE(curves.size() == 3);
    REQUIRE(curves[2].Magnitude.size() == 3);
    CHECK(curves[2].Magnitude[0] == -0.5);
    CHECK(std::isnan(curves[2].Magnitude[2]));
    CHECK(std::isnan(curves[2].Phase[2]));
    CHECK(curves[1].Phase[1] == -2.0);
}

TEST_CASE("The texts keep three significant digits and sign the change", "[ac_sensitivity_view]") {
    CHECK(GUI::FormatDecibelsPerPercent(-0.043429) == "-0.0434 dB");
    CHECK(GUI::FormatDegreesPerPercent(0.28648) == "+0.286 deg");
    CHECK(GUI::FormatDecibelsPerPercent(-0.0) == "+0 dB");
    CHECK(GUI::FormatDecibelsPerPercent(std::nullopt) == "-");
    CHECK(GUI::FormatDegreesPerPercent(std::nullopt) == "-");
}

TEST_CASE("The AC sensitivity table and summary show worst-case gain and phase bounds for toleranced parts",
          "[ac_sensitivity_view]") {
    Core::ACSensitivity sensitivity = MakeSensitivity();
    sensitivity.Parts[1].TolerancePercent = 5.0;
    sensitivity.Parts[2].TolerancePercent = 10.0;

    const std::vector<GUI::ACSensitivityRow> rows = GUI::MakeACSensitivityRows(sensitivity, 0);
    REQUIRE(rows.size() == 3);
    // R1 ranks first at 10 Hz: 10% x |-0.5| dB/% = 5 dB, 10% x |-0.1| deg/% = 1 deg
    CHECK(rows[0].PartName == "R1");
    CHECK(rows[0].Tolerance == "±5 dB / ±1 deg (10%)");
    CHECK(rows[1].PartName == "C1");
    CHECK(rows[1].Tolerance == "±0 dB / ±10 deg (5%)");
    CHECK(rows[2].Tolerance == "-");

    // At 1 kHz (index 2), R1 has no figure so its tolerance column reads "-"
    const std::vector<GUI::ACSensitivityRow> high_freq = GUI::MakeACSensitivityRows(sensitivity, 2);
    REQUIRE(high_freq.size() == 3);
    CHECK(high_freq[2].PartName == "R1");
    CHECK(high_freq[2].Tolerance == "-");

    const std::optional<std::string> summary = GUI::FormatACWorstCaseSummary(sensitivity, 0);
    REQUIRE(summary.has_value());
    CHECK(summary->contains("Worst case at 10Hz (2 toleranced parts):"));
    CHECK(summary->contains("gain ±5 dB (extreme), ±5 dB (RSS)"));
    CHECK(summary->contains("phase ±11 deg (extreme), ±10 deg (RSS)"));

    // At a frequency where no toleranced part has any valid figure, the summary reports that output is too small
    sensitivity.Parts[1].MagnitudeDecibelsPerPercent[2] = std::nullopt;
    sensitivity.Parts[1].PhaseDegreesPerPercent[2] = std::nullopt;
    const std::optional<std::string> small_summary = GUI::FormatACWorstCaseSummary(sensitivity, 2);
    REQUIRE(small_summary.has_value());
    CHECK(small_summary.value() == "Worst case at 1kHz (2 toleranced parts): output too small");
}
