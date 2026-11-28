/**
 * @file    complex_plane_tests.cpp
 * @brief   Tests for Polar and Nyquist complex-plane geometry, AC/S-parameter loci, and SVG/CSV export.
 */

#include "implot.h"
#include "plots/ac_complex_view.h"
#include "plots/complex_plane.h"
#include "plots/plot_export.h"
#include "plots/port_matrix_view.h"
#include "schematic/element_factory.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <complex>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

using Catch::Matchers::WithinAbs;

/**
 * @struct  ScopedImPlotContext
 * @brief   Owns temporary ImGui and ImPlot contexts so `GetNodeColor` and `GetCurrentColor` can read theme and
 *          colormap tables in headless tests.
 */
struct ScopedImPlotContext {
    ImGuiContext *Gui = ImGui::CreateContext();
    ImPlotContext *Plot = ImPlot::CreateContext();

    ScopedImPlotContext() = default;
    ScopedImPlotContext(const ScopedImPlotContext &) = delete;
    ScopedImPlotContext &operator=(const ScopedImPlotContext &) = delete;

    ~ScopedImPlotContext() noexcept {
        ImPlot::DestroyContext(Plot);
        ImGui::DestroyContext(Gui);
    }
};

/**
 * @brief   Builds a 3-point RC low-pass AC sweep at DC, corner (-3.0103 dB, -45 deg), and high frequency.
 */
Core::ACSweep MakeLowPassACSweep() {
    Core::ACSweep sweep;
    sweep.Frequencies = {10.0, 1e3, 1e5};
    const double corner_db = 20.0 * std::log10(1.0 / std::sqrt(2.0));
    sweep.NodeMagnitudesDecibels = {{}, {0.0, 0.0, 0.0}, {0.0, corner_db, -40.0}};
    sweep.NodePhasesDegrees = {{}, {0.0, 0.0, 0.0}, {0.0, -45.0, -89.4}};
    sweep.CurrentMagnitudesDecibels = {{.Name = "R1", .Values = {-60.0, -63.0103, -60.0}}};
    sweep.CurrentPhasesDegrees = {{.Name = "R1", .Values = {0.0, 45.0, 89.4}}};
    return sweep;
}

/**
 * @brief   Adds a single resistor at the origin so nodes 1 and 2 exist in the schematic connectivity.
 */
void PopulateSchematicWithResistor(GUI::Schematic &schematic) {
    schematic.AddElement(GUI::CreateElement(Core::ComponentType::RESISTOR, {0, 0}, GUI::Rotation::R0));
}

} // namespace

TEST_CASE("A polar grid builds round concentric circles and twelve 30-degree spokes", "[complex_plane]") {
    const GUI::PolarGrid grid = GUI::BuildPolarGrid(0.85, 32);
    CHECK(grid.OuterRadius >= 0.85);
    REQUIRE_FALSE(grid.Circles.empty());
    CHECK_THAT(grid.Circles.back().Radius, WithinAbs(grid.OuterRadius, 1e-12));
    CHECK(grid.Spokes.size() == 12);
    CHECK(grid.Spokes.front().Label == "0°");
    CHECK_THAT(grid.Spokes.front().End.X, WithinAbs(grid.OuterRadius, 1e-12));
    CHECK_THAT(grid.Spokes.front().End.Y, WithinAbs(0.0, 1e-12));

    // A zero or empty magnitude falls back to a unit polar grid
    const GUI::PolarGrid empty_grid = GUI::BuildPolarGrid(0.0, 4);
    CHECK_THAT(empty_grid.OuterRadius, WithinAbs(1.0, 1e-12));
    CHECK(empty_grid.Circles.front().Points.size() >= 9);
}

TEST_CASE("Nyquist extent always frames (-1, 0) and the unit circle, expanding for high gain", "[complex_plane]") {
    const std::vector<std::vector<std::complex<double>>> small = {{{0.01, 0.0}, {0.0, -0.01}}};
    const double small_max = GUI::FindMaxComplexMagnitude(small);
    CHECK_THAT(small_max, WithinAbs(0.01, 1e-12));
    CHECK(GUI::ChooseNyquistExtent(small_max) >= 1.15);

    const double nan = std::numeric_limits<double>::quiet_NaN();
    const std::vector<std::vector<std::complex<double>>> large = {{{nan, nan}, {-3.0, 4.0}}};
    const double large_max = GUI::FindMaxComplexMagnitude(large);
    CHECK_THAT(large_max, WithinAbs(5.0, 1e-12));
    CHECK(GUI::ChooseNyquistExtent(large_max) > 5.0);
}

TEST_CASE("Direction arrows point along increasing frequency and conjugate mirroring reverses order",
          "[complex_plane]") {
    const std::vector<std::complex<double>> locus = {{1.0, 0.0}, {0.5, -0.5}, {0.0, 0.0}};
    const std::vector<GUI::DirectionArrow> arrows = GUI::BuildDirectionArrows(locus, 0.1);
    REQUIRE(arrows.size() == 2);
    // Moving from (1, 0) toward (0.5, -0.5) has negative dx and negative dy, so wings trail behind the tip in X
    CHECK(arrows.front().LeftWing.X > arrows.front().Tip.X);
    CHECK(arrows.front().RightWing.X > arrows.front().Tip.X);

    const std::vector<std::complex<double>> mirrored = GUI::MirrorNegativeFrequencies(locus);
    REQUIRE(mirrored.size() == 3);
    CHECK_THAT(mirrored[0].real(), WithinAbs(0.0, 1e-12));
    CHECK_THAT(mirrored[1].real(), WithinAbs(0.5, 1e-12));
    CHECK_THAT(mirrored[1].imag(), WithinAbs(0.5, 1e-12));
    CHECK_THAT(mirrored[2].real(), WithinAbs(1.0, 1e-12));

    // Nearest point skips NaN entries
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const std::vector<std::complex<double>> gapped = {{nan, nan}, {1.0, 0.0}, {0.0, -1.0}};
    CHECK(GUI::FindNearestComplexPoint(gapped, {0.1, -0.9}) == std::optional<std::size_t>(2));
    CHECK_FALSE(GUI::FindNearestComplexPoint(std::vector<std::complex<double>>{{nan, nan}}, {0.0, 0.0}).has_value());
}

TEST_CASE("AC sweep Nyquist and Polar figures rebuild phasors, steps and export to SVG and CSV", "[complex_plane]") {
    const ScopedImPlotContext context;
    GUI::Schematic schematic;
    PopulateSchematicWithResistor(schematic);
    schematic.GetMeasurements().SetVoltageMeasured(2, true, schematic.GetConnectivity());
    schematic.GetMeasurements().SetCurrentMeasured("R1", true);

    GUI::ResultStore results;
    const Core::ACSweep nominal = MakeLowPassACSweep();
    Core::ACSweep alternate = nominal;
    alternate.NodeMagnitudesDecibels[2] = {-6.0, -9.0, -46.0};
    results.SetACSweepSteps({.Parameter = "R1", .Unit = "Ohm", .Values = {1e3, 2e3}, .Results = {nominal, alternate}});

    const GUI::ACMathTraces no_math;
    const std::vector<GUI::ComplexLocus> loci = GUI::BuildACSweepComplexLoci(nominal, no_math, results, schematic);
    // Two faint background traces from the unselected step, plus two primary traces (node 2 voltage and dashed I(R1))
    REQUIRE(loci.size() == 4);
    CHECK(loci[0].IsFaint);
    CHECK(loci[1].IsFaint);
    CHECK_FALSE(loci[2].IsFaint);
    CHECK_FALSE(loci[2].IsDashed);
    CHECK_THAT(loci[2].Values[1].real(), WithinAbs(0.5, 1e-4));
    CHECK_THAT(loci[2].Values[1].imag(), WithinAbs(-0.5, 1e-4));
    CHECK(loci[3].IsDashed);

    const std::string reading = GUI::DescribeACSweepComplexReading(1e3, loci[2], 1);
    CHECK(reading.contains("1kHz"));
    CHECK(reading.contains("-3.01 dB"));
    CHECK(reading.contains("-45.00 deg"));

    const GUI::ExportFigure nyquist =
        GUI::BuildACSweepComplexFigure(nominal, no_math, results, schematic, GUI::ComplexPlaneKind::NYQUIST, true);
    const auto nyquist_svg = GUI::RenderSvg(nyquist, {.Width = 800, .Height = 600, .Dark = false});
    REQUIRE(nyquist_svg.has_value());
    CHECK(nyquist_svg->contains("<svg"));
    CHECK(nyquist_svg->contains("Nyquist plot"));
    CHECK(nyquist_svg->contains("Real"));
    CHECK(nyquist_svg->contains("Imaginary"));

    const std::string csv = GUI::RenderCsv(nyquist);
    CHECK(csv.contains("Frequency (Hz)"));
    CHECK(csv.contains("I(R1) real (A),I(R1) imaginary (A)"));
}

TEST_CASE("S, Z and Y polar figures render SVG and preserve missing-matrix gaps", "[complex_plane]") {
    Core::SParameters sp;
    sp.Frequencies = {1e6, 1e7};
    sp.ReferenceOhms = {50.0};
    sp.Values = {{0.0, 0.0}, {0.5, -0.5}};
    sp.ImpedanceMatrices = {std::nullopt, std::vector<std::complex<double>>{{50.0, -25.0}}};

    const std::vector<std::complex<double>> z_values =
        GUI::ListMatrixValues(sp, {.Row = 1, .Column = 1}, GUI::SParameterQuantity::Z);
    REQUIRE(z_values.size() == 2);
    CHECK(std::isnan(z_values[0].real()));
    CHECK_THAT(z_values[1].real(), WithinAbs(50.0, 1e-12));
    CHECK_THAT(z_values[1].imag(), WithinAbs(-25.0, 1e-12));

    const GUI::ExportFigure polar =
        GUI::BuildMatrixPolarFigure(sp, {{.Row = 1, .Column = 1}}, GUI::SParameterQuantity::Z);
    const auto polar_svg = GUI::RenderSvg(polar, {.Width = 800, .Height = 600, .Dark = true});
    REQUIRE(polar_svg.has_value());
    CHECK(polar_svg->contains("Z-parameters polar plot"));
    CHECK(polar_svg->contains("Z11"));

    const std::string csv = GUI::RenderCsv(polar);
    CHECK(csv.contains("Z11 real (Ohm),Z11 imaginary (Ohm)"));
}
