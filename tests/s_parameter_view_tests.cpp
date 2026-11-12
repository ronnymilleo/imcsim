/**
 * @file    s_parameter_view_tests.cpp
 * @brief   Tests of what the Output window shows of an S-parameter result: the entries plotted, the readings under the
 *          cursor and the figures of the plots.
 */

#include "plots/s_parameter_view.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cmath>
#include <complex>
#include <string>
#include <vector>

namespace {

using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Complex = std::complex<double>;

/// A two-port at three frequencies: S11 = 1/3 (a load of 100 ohm on 50), S21 = 0.5, S12 = 0.25, S22 = j / 2.
Core::SParameters MakeResult() {
    Core::SParameters result;
    result.Frequencies = {1e6, 1e7, 1e8};
    result.ReferenceOhms = {50.0, 75.0};
    for (std::size_t frequency = 0; frequency < 3; ++frequency) {
        result.Values.insert(result.Values.end(),
                             {Complex(1.0 / 3.0, 0.0), Complex(0.25, 0.0), Complex(0.5, 0.0), Complex(0.0, 0.5)});
    }
    return result;
}

} // namespace

TEST_CASE("A new result shows S11 and S21 of a two-port", "[s_parameter_view]") {
    GUI::SParameterSelection selection;
    selection.Reset(2);
    CHECK(selection.GetPortCount() == 2);
    CHECK(selection.IsShown(1, 1));
    CHECK(selection.IsShown(2, 1));
    CHECK_FALSE(selection.IsShown(1, 2));
    CHECK_FALSE(selection.IsShown(2, 2));
    CHECK(selection.ListShown() == std::vector<GUI::SParameterEntry>{{1, 1}, {2, 1}});
}

TEST_CASE("The Smith chart starts from the reflections and no transmission", "[s_parameter_view]") {
    GUI::SParameterSelection selection;
    selection.ResetToReflections(3);
    CHECK(selection.ListShown() == std::vector<GUI::SParameterEntry>{{1, 1}, {2, 2}, {3, 3}});
    selection.ResetToReflections(2);
    CHECK_FALSE(selection.IsShown(2, 1));
    CHECK(selection.ListShown().size() == 2);
}

TEST_CASE("Entries are shown and hidden one at a time, and the ones outside the matrix are ignored",
          "[s_parameter_view]") {
    GUI::SParameterSelection selection;
    selection.Reset(3);
    selection.SetShown(3, 3, true);
    selection.SetShown(1, 1, false);
    selection.SetShown(4, 1, true);
    selection.SetShown(0, 1, true);
    CHECK(selection.ListShown() == std::vector<GUI::SParameterEntry>{{2, 1}, {3, 3}});
    CHECK_FALSE(selection.IsShown(4, 1));
    CHECK_FALSE(selection.IsShown(1, 0));
    selection.Reset(1);
    CHECK(selection.ListShown() == std::vector<GUI::SParameterEntry>{{1, 1}});
}

TEST_CASE("Entries are named as datasheets name them, with a comma when a port has two digits", "[s_parameter_view]") {
    CHECK(GUI::GetEntryName({1, 1}) == "S11");
    CHECK(GUI::GetEntryName({2, 1}) == "S21");
    CHECK(GUI::GetEntryName({1, 10}) == "S1,10");
    CHECK(GUI::GetEntryName({12, 3}) == "S12,3");
    CHECK(GUI::GetEntryColor(0).Red == GUI::GetEntryColor(8).Red);
}

TEST_CASE("A reflection reads with its impedance against the reference of its own port", "[s_parameter_view]") {
    const Core::SParameters result = MakeResult();
    const GUI::SParameterReading reading = GUI::ReadEntry(result, 1, {1, 1});
    CHECK(reading.IsReflection);
    CHECK_THAT(reading.FrequencyHertz, WithinAbs(1e7, 1e-3));
    CHECK_THAT(reading.MagnitudeDecibels, WithinAbs(-9.5424, 1e-3));
    CHECK_THAT(reading.AngleDegrees, WithinAbs(0.0, 1e-9));
    // (1 + 1/3) / (1 - 1/3) = 2 times the 50 ohm reference
    REQUIRE(reading.Impedance.has_value());
    CHECK_THAT(reading.Impedance->real(), WithinAbs(100.0, 1e-9));
    CHECK_THAT(*reading.Vswr, WithinAbs(2.0, 1e-9));
    CHECK_THAT(*reading.ReturnLossDecibels, WithinAbs(9.5424, 1e-3));

    // S22 of the second port is against its 75 ohm: (1 + j/2) / (1 - j/2) is a unit magnitude at 53.13 degrees
    const GUI::SParameterReading second = GUI::ReadEntry(result, 0, {2, 2});
    CHECK(second.ReferenceOhms == 75.0);
    CHECK_THAT(second.AngleDegrees, WithinAbs(90.0, 1e-9));
    REQUIRE(second.Impedance.has_value());
    CHECK_THAT(second.Impedance->real(), WithinAbs(75.0 * 0.6, 1e-9));
    CHECK_THAT(second.Impedance->imag(), WithinAbs(75.0 * 0.8, 1e-9));
}

TEST_CASE("A transmission reads only its magnitude and angle", "[s_parameter_view]") {
    const GUI::SParameterReading reading = GUI::ReadEntry(MakeResult(), 2, {2, 1});
    CHECK_FALSE(reading.IsReflection);
    CHECK_THAT(reading.MagnitudeDecibels, WithinAbs(-6.0206, 1e-3));
    CHECK_FALSE(reading.Impedance.has_value());
    CHECK_FALSE(reading.Vswr.has_value());
    CHECK_FALSE(reading.ReturnLossDecibels.has_value());
    const std::vector<std::string> lines = GUI::DescribeReading(reading);
    REQUIRE(lines.size() == 2);
    CHECK_THAT(lines[0], ContainsSubstring("S21 at 100MHz"));
    CHECK_THAT(lines[1], ContainsSubstring("-6.021 dB"));
}

TEST_CASE("The words of a reflection tell the impedance, absolute and normalized, the VSWR and the return loss",
          "[s_parameter_view]") {
    const std::vector<std::string> lines = GUI::DescribeReading(GUI::ReadEntry(MakeResult(), 0, {1, 1}));
    REQUIRE(lines.size() == 6);
    CHECK_THAT(lines[0], ContainsSubstring("S11 at 1MHz"));
    CHECK_THAT(lines[2], ContainsSubstring("Z = 100 + j0 ohm"));
    CHECK_THAT(lines[3], ContainsSubstring("z = 2 + j0 (normalized to 50 ohm)"));
    CHECK_THAT(lines[4], ContainsSubstring("VSWR 2"));
    CHECK_THAT(lines[5], ContainsSubstring("Return loss 9.542 dB"));
}

TEST_CASE("An open circuit and a perfect match read as infinite, not as numbers that mean nothing",
          "[s_parameter_view]") {
    Core::SParameters open;
    open.Frequencies = {1e6};
    open.ReferenceOhms = {50.0, 50.0};
    open.Values = {Complex(1.0, 0.0), 0.0, 0.0, 0.0};
    const std::vector<std::string> lines = GUI::DescribeReading(GUI::ReadEntry(open, 0, {1, 1}));
    REQUIRE(lines.size() == 5);
    CHECK_THAT(lines[2], ContainsSubstring("infinite"));
    CHECK_THAT(lines[3], ContainsSubstring("VSWR infinite"));
    // S22 is a perfect match: nothing is reflected, so the return loss has no end
    const GUI::SParameterReading matched = GUI::ReadEntry(open, 0, {2, 2});
    CHECK_FALSE(matched.ReturnLossDecibels.has_value());
    CHECK_THAT(GUI::DescribeReading(matched).back(), ContainsSubstring("Return loss infinite"));
}

TEST_CASE("An impedance reads with the sign of its reactance", "[s_parameter_view]") {
    CHECK(GUI::FormatImpedance({50.0, 25.0}) == "50 + j25");
    CHECK(GUI::FormatImpedance({12.5, -3.25}) == "12.5 - j3.25");
    CHECK(GUI::FormatImpedance({0.0, 0.0}) == "0 + j0");
}

TEST_CASE("The pointer finds the nearest frequency on a logarithmic axis and the nearest point of the chart",
          "[s_parameter_view]") {
    const std::vector<double> frequencies = {1e6, 1e7, 1e8};
    CHECK(GUI::FindNearestFrequency(frequencies, 1.1e6) == 0);
    // On a logarithmic axis 4e6 is a factor 4 from 1e6 and 2.5 from 1e7
    CHECK(GUI::FindNearestFrequency(frequencies, 4e6) == 1);
    CHECK(GUI::FindNearestFrequency(frequencies, 9e9) == 2);
    CHECK(GUI::FindNearestFrequency({}, 1e6) == 0);

    const std::vector<Complex> values = {{0.0, 0.0}, {0.5, 0.0}, {0.0, 0.5}};
    CHECK(GUI::FindNearestReflection(values, {0.45, 0.05}) == 1);
    CHECK(GUI::FindNearestReflection(values, {-0.1, 0.4}) == 2);
    CHECK(GUI::FindNearestReflection({}, {0.0, 0.0}) == 0);
}

TEST_CASE("The curves of an entry list its magnitude, angle and value at every frequency", "[s_parameter_view]") {
    const Core::SParameters result = MakeResult();
    const std::vector<double> magnitudes = GUI::ListMagnitudesDecibels(result, {2, 1});
    REQUIRE(magnitudes.size() == 3);
    CHECK_THAT(magnitudes[1], WithinAbs(-6.0206, 1e-3));
    CHECK_THAT(GUI::ListAnglesDegrees(result, {2, 2})[2], WithinAbs(90.0, 1e-9));
    CHECK(GUI::ListValues(result, {1, 2})[0] == Complex(0.25, 0.0));
}

TEST_CASE("The magnitude and phase figure has a panel of each, with a series for every entry", "[s_parameter_view]") {
    const Core::SParameters result = MakeResult();
    const GUI::ExportFigure figure =
        GUI::BuildMagnitudePhaseFigure(result, {{1, 1}, {2, 1}}, GUI::ExportRange{.From = 2e6, .To = 5e7});
    CHECK_FALSE(figure.Smith.has_value());
    CHECK(figure.XLogarithmic);
    CHECK(figure.Xs == result.Frequencies);
    REQUIRE(figure.XRange.has_value());
    CHECK(figure.XRange->From == 2e6);
    REQUIRE(figure.Panels.size() == 2);
    CHECK(figure.Panels[0].Title == "Magnitude");
    CHECK(figure.Panels[1].Title == "Phase");
    REQUIRE(figure.Panels[0].Series.size() == 2);
    CHECK(figure.Panels[0].Series[0].Name == "S11");
    CHECK(figure.Panels[0].Series[1].LegendLabel == "S21");
    CHECK_THAT(figure.Panels[0].Series[1].Values[0], WithinAbs(-6.0206, 1e-3));
    CHECK_THAT(figure.Panels[1].Series[0].Values[0], WithinAbs(0.0, 1e-9));
    // Colors follow the entries in both panels
    CHECK(figure.Panels[0].Series[1].Color.Red == figure.Panels[1].Series[1].Color.Red);

    const std::string csv = GUI::RenderCsv(figure);
    CHECK_THAT(csv, ContainsSubstring("Magnitude S11 (dB)"));
    CHECK_THAT(csv, ContainsSubstring("Phase S21 (deg)"));
}

TEST_CASE("The Smith figure holds a curve per entry, and its table lists real and imaginary parts",
          "[s_parameter_view]") {
    const GUI::ExportFigure figure = GUI::BuildSmithFigure(MakeResult(), {{1, 1}, {2, 2}});
    REQUIRE(figure.Smith.has_value());
    REQUIRE(figure.Smith->Traces.size() == 2);
    CHECK(figure.Smith->Traces[1].Name == "S22");
    CHECK(figure.Smith->Traces[1].Values[0] == Complex(0.0, 0.5));
    CHECK(figure.Smith->Traces[0].Frequencies.size() == 3);

    const std::string csv = GUI::RenderCsv(figure);
    CHECK_THAT(csv, ContainsSubstring("Frequency (Hz),S11 real,S11 imaginary,S22 real,S22 imaginary\n"));
    CHECK_THAT(csv, ContainsSubstring("\n1e+06,0.3333333333333333,0,0,0.5\n"));
}
