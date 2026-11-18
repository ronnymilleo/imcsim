/**
 * @file    port_matrix_view_tests.cpp
 * @brief   Tests of what the Output window shows of the impedance and admittance matrices of the ports: availability,
 *          the gaps where a matrix is missing, the readings and the figures of the plots and the table.
 */

#include "plots/port_matrix_view.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cmath>
#include <complex>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace {

using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Complex = std::complex<double>;

/// A two-port at three frequencies with Z and Y, absent at the middle one when it is asked to be.
Core::SParameters MakeResult(const bool is_middle_absent, const bool has_matrices = true) {
    Core::SParameters result;
    result.Frequencies = {1e6, 1e7, 1e8};
    result.ReferenceOhms = {50.0, 50.0};
    for (std::size_t frequency = 0; frequency < 3; ++frequency) {
        result.Values.insert(result.Values.end(),
                             {Complex(0.1, 0.0), Complex(0.2, 0.0), Complex(0.3, 0.0), Complex(0.4, 0.0)});
        if (!has_matrices) {
            continue;
        }
        const double scale = static_cast<double>(frequency + 1);
        if (is_middle_absent && frequency == 1) {
            result.ImpedanceMatrices.emplace_back(std::nullopt);
            result.AdmittanceMatrices.emplace_back(std::nullopt);
        } else {
            result.ImpedanceMatrices.emplace_back(std::vector<Complex>{Complex(10.0 * scale, 5.0),
                                                                       Complex(1.0, 0.0),
                                                                       Complex(2.0, 0.0),
                                                                       Complex(20.0, -5.0)});
            result.AdmittanceMatrices.emplace_back(std::vector<Complex>{Complex(0.01 * scale, 0.0),
                                                                        Complex(0.001, 0.0),
                                                                        Complex(0.002, 0.0),
                                                                        Complex(0.02, 0.0)});
        }
    }
    return result;
}

} // namespace

TEST_CASE("Z and Y are offered only when the result carries them", "[port_matrix_view]") {
    CHECK(GUI::IsQuantityAvailable(MakeResult(false), GUI::SParameterQuantity::Z));
    CHECK(GUI::IsQuantityAvailable(MakeResult(false), GUI::SParameterQuantity::Y));
    const Core::SParameters without = MakeResult(false, false);
    CHECK(GUI::IsQuantityAvailable(without, GUI::SParameterQuantity::S));
    CHECK_FALSE(GUI::IsQuantityAvailable(without, GUI::SParameterQuantity::Z));
    CHECK_FALSE(GUI::IsQuantityAvailable(without, GUI::SParameterQuantity::Y));
    CHECK_THAT(GUI::DescribeUnavailableQuantity(GUI::SParameterQuantity::Z), ContainsSubstring("Nodalys"));
    CHECK_THAT(GUI::DescribeUnavailableQuantity(GUI::SParameterQuantity::Y), ContainsSubstring("admittance"));
}

TEST_CASE("Entries are named by the letter of their matrix", "[port_matrix_view]") {
    CHECK(GUI::GetMatrixEntryName({2, 1}, GUI::SParameterQuantity::Z) == "Z21");
    CHECK(GUI::GetMatrixEntryName({1, 2}, GUI::SParameterQuantity::Y) == "Y12");
    CHECK(GUI::GetMatrixEntryName({1, 10}, GUI::SParameterQuantity::Y) == "Y1,10");
    CHECK(GUI::GetMatrixEntryName({1, 1}, GUI::SParameterQuantity::S) == "S11");
    CHECK(GUI::GetQuantityUnit(GUI::SParameterQuantity::Z) == "Ohm");
    CHECK(GUI::GetQuantityUnit(GUI::SParameterQuantity::Y) == "S");
}

TEST_CASE("A matrix that is absent at a frequency leaves a gap in its curves, never a number", "[port_matrix_view]") {
    const Core::SParameters result = MakeResult(true);
    const std::vector<double> magnitudes = GUI::ListMatrixMagnitudes(result, {1, 1}, GUI::SParameterQuantity::Z);
    REQUIRE(magnitudes.size() == 3);
    CHECK_THAT(magnitudes[0], WithinAbs(std::abs(Complex(10.0, 5.0)), 1e-12));
    CHECK(std::isnan(magnitudes[1]));
    CHECK_THAT(magnitudes[2], WithinAbs(std::abs(Complex(30.0, 5.0)), 1e-12));
    const std::vector<double> angles = GUI::ListMatrixAngles(result, {1, 1}, GUI::SParameterQuantity::Z);
    CHECK(std::isnan(angles[1]));
    CHECK_THAT(angles[0], WithinAbs(26.565, 1e-3));
    // S is never absent, and keeps its decibels
    const std::vector<double> decibels = GUI::ListMatrixMagnitudes(result, {1, 1}, GUI::SParameterQuantity::S);
    CHECK_THAT(decibels[1], WithinAbs(-20.0, 1e-9));
}

TEST_CASE("A zero entry reads as a gap on the logarithmic magnitude axis", "[port_matrix_view]") {
    Core::SParameters result = MakeResult(false);
    result.ImpedanceMatrices[0] =
        std::vector<Complex>{Complex(0.0, 0.0), Complex(1.0, 0.0), Complex(2.0, 0.0), Complex(3.0, 0.0)};
    CHECK(std::isnan(GUI::ListMatrixMagnitudes(result, {1, 1}, GUI::SParameterQuantity::Z)[0]));
    // Its angle is still told
    CHECK_FALSE(std::isnan(GUI::ListMatrixAngles(result, {1, 1}, GUI::SParameterQuantity::Z)[0]));
}

TEST_CASE("The entries are read from the row and column of the matrix", "[port_matrix_view]") {
    const Core::SParameters result = MakeResult(false);
    CHECK(GUI::ReadMatrixValue(result, 0, {1, 2}, GUI::SParameterQuantity::Z) == Complex(1.0, 0.0));
    CHECK(GUI::ReadMatrixValue(result, 0, {2, 1}, GUI::SParameterQuantity::Y) == Complex(0.002, 0.0));
    CHECK(GUI::ReadMatrixValue(result, 0, {2, 1}, GUI::SParameterQuantity::S) == Complex(0.3, 0.0));
    CHECK_FALSE(GUI::ReadMatrixValue(MakeResult(true), 1, {1, 1}, GUI::SParameterQuantity::Y));
}

TEST_CASE("The missing frequencies are counted and worded", "[port_matrix_view]") {
    CHECK(GUI::CountAbsentFrequencies(MakeResult(false), GUI::SParameterQuantity::Z) == 0);
    CHECK(GUI::DescribeAbsentFrequencies(MakeResult(false), GUI::SParameterQuantity::Z).empty());
    const Core::SParameters partly = MakeResult(true);
    CHECK(GUI::CountAbsentFrequencies(partly, GUI::SParameterQuantity::Z) == 1);
    CHECK(GUI::DescribeAbsentFrequencies(partly, GUI::SParameterQuantity::Z) ==
          "Z has no finite value at 1 of 3 frequencies, so its curves have gaps there.");
    // Without matrices nothing is counted, since the quantity is not offered
    CHECK(GUI::CountAbsentFrequencies(MakeResult(false, false), GUI::SParameterQuantity::Z) == 0);
    CHECK(GUI::CountAbsentFrequencies(partly, GUI::SParameterQuantity::S) == 0);
}

TEST_CASE("A matrix that exists nowhere says why, for impedance and for admittance", "[port_matrix_view]") {
    Core::SParameters result = MakeResult(false);
    result.ImpedanceMatrices.assign(3, std::nullopt);
    result.AdmittanceMatrices.assign(3, std::nullopt);
    CHECK_THAT(GUI::DescribeAbsentFrequencies(result, GUI::SParameterQuantity::Z),
               ContainsSubstring("no finite impedance matrix at any frequency"));
    CHECK_THAT(GUI::DescribeAbsentFrequencies(result, GUI::SParameterQuantity::Z), ContainsSubstring("series element"));
    CHECK_THAT(GUI::DescribeAbsentFrequencies(result, GUI::SParameterQuantity::Y),
               ContainsSubstring("no finite admittance matrix at any frequency"));
}

TEST_CASE("The reading of an entry of Z gives its magnitude, angle and parts, or says there is none",
          "[port_matrix_view]") {
    const Core::SParameters result = MakeResult(true);
    const std::vector<std::string> lines = GUI::DescribeMatrixReading(result, 0, {1, 1}, GUI::SParameterQuantity::Z);
    REQUIRE(lines.size() == 3);
    CHECK(lines[0] == "Z11 at 1MHz");
    CHECK(lines[1] == "11.18 Ohm at 26.57 deg");
    CHECK(lines[2] == "10 + j5 Ohm");
    const std::vector<std::string> absent = GUI::DescribeMatrixReading(result, 1, {1, 1}, GUI::SParameterQuantity::Y);
    REQUIRE(absent.size() == 2);
    CHECK(absent[1] == "No finite admittance matrix at this frequency");
}

TEST_CASE("The magnitude and phase figure of Z has a logarithmic axis in ohms and a gap where it is absent",
          "[port_matrix_view]") {
    const Core::SParameters result = MakeResult(true);
    const GUI::ExportFigure figure =
        GUI::BuildMatrixMagnitudePhaseFigure(result, {{1, 1}, {2, 1}}, std::nullopt, GUI::SParameterQuantity::Z);
    REQUIRE(figure.Panels.size() == 2);
    CHECK(figure.XLogarithmic);
    CHECK(figure.Panels[0].YAxes[0].Logarithmic);
    CHECK(figure.Panels[0].YAxes[0].Unit == "Ohm");
    CHECK(figure.Panels[1].YAxes[0].Unit == "deg");
    REQUIRE(figure.Panels[0].Series.size() == 2);
    CHECK(figure.Panels[0].Series[0].Name == "Z11");
    CHECK(std::isnan(figure.Panels[0].Series[0].Values[1]));
    CHECK(std::isnan(figure.Panels[1].Series[1].Values[1]));
    // S keeps the figure it always had
    const GUI::ExportFigure s_figure =
        GUI::BuildMatrixMagnitudePhaseFigure(result, {{1, 1}}, std::nullopt, GUI::SParameterQuantity::S);
    CHECK(s_figure.Panels[0].YAxes[0].Unit == "dB");
    CHECK_FALSE(s_figure.Panels[0].YAxes[0].Logarithmic);
}

TEST_CASE("The table of Z lists the real and imaginary parts and leaves the absent frequency empty",
          "[port_matrix_view]") {
    const Core::SParameters result = MakeResult(true);
    const GUI::ExportFigure figure = GUI::BuildMatrixTableFigure(result, {{1, 1}}, GUI::SParameterQuantity::Z);
    const std::string csv = GUI::RenderCsv(figure);
    CHECK(csv == "Frequency (Hz),Z11 real (Ohm),Z11 imaginary (Ohm)\n"
                 "1e+06,10,5\n"
                 "1e+07,,\n"
                 "1e+08,30,5\n");
    const std::string admittance =
        GUI::RenderCsv(GUI::BuildMatrixTableFigure(result, {{2, 1}}, GUI::SParameterQuantity::Y));
    CHECK_THAT(admittance, ContainsSubstring("Y21 real (S),Y21 imaginary (S)"));
}

TEST_CASE("The decade range of a logarithmic axis pads the positive values in decades", "[port_matrix_view]") {
    // Four decades, from 10 to 100k: a tenth of them is 0.4, below the half decade, so half a decade each side
    const GUI::ExportRange spanning = GUI::FindDecadeRange({{10.0, 1e3}, {1e5}});
    CHECK_THAT(spanning.From, WithinAbs(10.0 / std::sqrt(10.0), 1e-9));
    CHECK_THAT(spanning.To, WithinAbs(1e5 * std::sqrt(10.0), 1e-3));
    // Twelve decades pad by 1.2 decades
    const GUI::ExportRange wide = GUI::FindDecadeRange({{1.0, 1e12}});
    CHECK_THAT(std::log10(wide.From), WithinAbs(-1.2, 1e-9));
    CHECK_THAT(std::log10(wide.To), WithinAbs(13.2, 1e-9));
    // The range never reaches zero or crosses it
    CHECK(wide.From > 0.0);
}

TEST_CASE("The decade range of one constant value is half a decade each side of it", "[port_matrix_view]") {
    const GUI::ExportRange range = GUI::FindDecadeRange({{4700.0, 4700.0, 4700.0}});
    CHECK_THAT(range.From, WithinAbs(4700.0 / std::sqrt(10.0), 1e-6));
    CHECK_THAT(range.To, WithinAbs(4700.0 * std::sqrt(10.0), 1e-6));
}

TEST_CASE("The decade range leaves out gaps, zeros and infinities", "[port_matrix_view]") {
    const double gap = std::nan("");
    const GUI::ExportRange range =
        GUI::FindDecadeRange({{gap, 100.0, 0.0, 1e4, std::numeric_limits<double>::infinity()}});
    CHECK_THAT(std::log10(range.From), WithinAbs(1.5, 1e-9));
    CHECK_THAT(std::log10(range.To), WithinAbs(4.5, 1e-9));
}

TEST_CASE("The decade range with nothing to draw is 1 to 10", "[port_matrix_view]") {
    const double gap = std::nan("");
    for (const std::vector<std::vector<double>> &traces : {std::vector<std::vector<double>>{}, {{gap, gap}}, {{0.0}}}) {
        const GUI::ExportRange range = GUI::FindDecadeRange(traces);
        CHECK(range.From == 1.0);
        CHECK(range.To == 10.0);
    }
}

TEST_CASE("The figure of Z has an axis fitted to its magnitudes, and so has Y", "[port_matrix_view]") {
    const Core::SParameters result = MakeResult(true);
    for (const GUI::SParameterQuantity quantity : {GUI::SParameterQuantity::Z, GUI::SParameterQuantity::Y}) {
        const GUI::ExportFigure figure = GUI::BuildMatrixMagnitudePhaseFigure(result, {{1, 1}}, std::nullopt, quantity);
        const std::optional<GUI::ExportRange> &range = figure.Panels[0].YAxes[0].Range;
        REQUIRE(range.has_value());
        const std::vector<double> &values = figure.Panels[0].Series[0].Values;
        CHECK(range->From > 0.0);
        CHECK(range->From < std::min(values[0], values[2]));
        CHECK(range->To > std::max(values[0], values[2]));
        CHECK(range->To / range->From < 1e3);
    }
}
