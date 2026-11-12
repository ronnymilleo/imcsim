/**
 * @file    harmonic_balance_view_tests.cpp
 * @brief   Tests for harmonic balance display scaling, periodic tiling, tables and exports.
 */

#include "plots/harmonic_balance_view.h"

#include "circuit/components/resistor.h"
#include "circuit/components/source.h"
#include "schematic/element_factory.h"
#include "schematic/probing.h"
#include "schematic/schematic.h"
#include "ui_elements/ui_wire.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cmath>
#include <complex>
#include <numbers>
#include <utility>
#include <vector>

namespace {

using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Complex = std::complex<double>;

/// Builds a two-node circuit V1(1-0) -- R1(1-0) and returns the schematic with the measured non-ground node index.
std::pair<GUI::Schematic, int> BuildMeasuredDividerSchematic() {
    GUI::Schematic schematic;
    std::vector<std::unique_ptr<GUI::UIElement>> elements;
    elements.push_back(GUI::CreateElement(Core::ComponentType::VOLTAGE_SOURCE, {0, 0}, GUI::Rotation::R0));
    elements.back()->GetComponent().SetName("V1");
    elements.push_back(GUI::CreateElement(Core::ComponentType::RESISTOR, {10, 0}, GUI::Rotation::R90));
    elements.back()->GetComponent().SetName("R1");
    elements.push_back(GUI::CreateElement(Core::ComponentType::GROUND, {0, 6}, GUI::Rotation::R0));

    std::vector<GUI::UIWire> wires;
    const auto v1_pins = elements[0]->GetTerminals();
    const auto r1_pins = elements[1]->GetTerminals();
    const auto gnd_pins = elements[2]->GetTerminals();
    // Rectangle wiring between vertical V1 at x=0 and vertical R1 at x=10, grounding the bottom rail at (0, 6).
    const GUI::GridPoint v1_top = v1_pins[0].Y < v1_pins[1].Y ? v1_pins[0] : v1_pins[1];
    const GUI::GridPoint v1_bot = v1_pins[0].Y < v1_pins[1].Y ? v1_pins[1] : v1_pins[0];
    const GUI::GridPoint r1_top = r1_pins[0].Y < r1_pins[1].Y ? r1_pins[0] : r1_pins[1];
    const GUI::GridPoint r1_bot = r1_pins[0].Y < r1_pins[1].Y ? r1_pins[1] : r1_pins[0];
    wires.emplace_back(v1_top, r1_top);
    wires.emplace_back(v1_bot, r1_bot);
    wires.emplace_back(v1_bot, gnd_pins[0]);

    schematic.Replace(std::move(elements), std::move(wires), {});
    const std::optional<int> node_opt = schematic.GetConnectivity().GetNode(v1_top);
    REQUIRE(node_opt.has_value());
    REQUIRE(*node_opt > 0);
    const int node_out = *node_opt;
    schematic.SetVoltageMeasured(node_out, true);
    schematic.SetCurrentMeasured("R1", true);
    return {std::move(schematic), node_out};
}

Core::HarmonicBalance MakeSampleResult(const int out_node) {
    Core::HarmonicBalance hb;
    hb.FundamentalFrequency = 1e3;
    hb.Frequencies = {0.0, 1e3, 2e3, 3e3};
    hb.NodePhasors.resize(static_cast<std::size_t>(out_node) + 1);
    hb.NodePhasors[static_cast<std::size_t>(out_node)] = {Complex(-0.5, 0.0), Complex(2.0, 0.0), Complex(0.0, 0.6),
                                                          Complex(0.0, -0.8)};
    hb.CurrentPhasors = {{.Name = "R1", .Phasors = {Complex(0.01, 0.0), Complex(0.0, -0.02), {}, Complex(0.002, 0.0)}}};
    hb.Times = {0.0, 0.25e-3, 0.5e-3, 0.75e-3};
    hb.NodeWaveforms.resize(static_cast<std::size_t>(out_node) + 1);
    hb.NodeWaveforms[static_cast<std::size_t>(out_node)] = {1.5, -1.9, -0.9, 1.3};
    hb.CurrentWaveforms = {{.Name = "R1", .Values = {0.012, 0.03, 0.008, -0.01}}};
    hb.Iterations = 4;
    hb.Strategy = "direct";
    return hb;
}

} // namespace

TEST_CASE("Harmonic magnitudes scale AC bins for Peak vs RMS while keeping DC unscaled", "[harmonic_balance_view]") {
    const Complex dc(-2.5, 0.0);
    const Complex ac(3.0, -4.0); // |X| = 5.0
    CHECK_THAT(GUI::ScaleHarmonicMagnitude(dc, 0, GUI::HarmonicAmplitudeUnit::PEAK), WithinAbs(2.5, 1e-12));
    CHECK_THAT(GUI::ScaleHarmonicMagnitude(dc, 0, GUI::HarmonicAmplitudeUnit::RMS), WithinAbs(2.5, 1e-12));
    CHECK_THAT(GUI::ScaleHarmonicMagnitude(ac, 1, GUI::HarmonicAmplitudeUnit::PEAK), WithinAbs(5.0, 1e-12));
    CHECK_THAT(GUI::ScaleHarmonicMagnitude(ac, 1, GUI::HarmonicAmplitudeUnit::RMS),
               WithinAbs(5.0 / std::numbers::sqrt2, 1e-12));

    const std::vector<Complex> spectrum = {Complex(1.0, 0.0), Complex(1.0, 0.0), Complex(0.1, 0.0), Complex(0.0, 0.0)};
    const std::vector<double> lin_rms =
        GUI::ListSpectrumValues(spectrum, GUI::HarmonicAmplitudeUnit::RMS, GUI::HarmonicScale::LINEAR);
    REQUIRE(lin_rms.size() == 4);
    CHECK_THAT(lin_rms[0], WithinAbs(1.0, 1e-12));
    CHECK_THAT(lin_rms[1], WithinAbs(1.0 / std::numbers::sqrt2, 1e-12));

    const std::vector<double> dbv_rms =
        GUI::ListSpectrumValues(spectrum, GUI::HarmonicAmplitudeUnit::RMS, GUI::HarmonicScale::DECIBEL);
    CHECK_THAT(dbv_rms[1], WithinAbs(-20.0 * std::log10(std::numbers::sqrt2), 1e-9));
    CHECK_THAT(dbv_rms[3], WithinAbs(GUI::HARMONIC_DECIBEL_FLOOR, 1e-9));

    const std::vector<double> dbc =
        GUI::ListSpectrumValues(spectrum, GUI::HarmonicAmplitudeUnit::RMS, GUI::HarmonicScale::DBC);
    CHECK_THAT(dbc[1], WithinAbs(0.0, 1e-9));
    CHECK_THAT(dbc[2], WithinAbs(-20.0, 1e-9));
    CHECK_THAT(dbc[3], WithinAbs(GUI::HARMONIC_DECIBEL_FLOOR, 1e-9));
}

TEST_CASE("Periodic waveform times and samples repeat cleanly across cycles", "[harmonic_balance_view]") {
    const std::vector<double> one_period = {0.0, 0.25e-3, 0.5e-3, 0.75e-3};
    const std::vector<double> values = {1.0, 0.0, -1.0, 0.0};

    const std::vector<double> tiled_t = GUI::TilePeriodicTimes(one_period, 1e3, 2);
    const std::vector<double> tiled_v = GUI::TilePeriodicValues(values, 2);
    REQUIRE(tiled_t.size() == 9);
    REQUIRE(tiled_v.size() == 9);
    CHECK_THAT(tiled_t[4], WithinAbs(1.0e-3, 1e-12));
    CHECK_THAT(tiled_t.back(), WithinAbs(2.0e-3, 1e-12));
    CHECK_THAT(tiled_v.back(), WithinAbs(1.0, 1e-12));

    // Out-of-bounds cycle counts clamp to [1, MAX_HARMONIC_WAVEFORM_CYCLES]
    CHECK(GUI::TilePeriodicTimes(one_period, 1e3, 0).size() == 5);
    CHECK(GUI::TilePeriodicTimes(one_period, 1e3, 10).size() == 4 * GUI::MAX_HARMONIC_WAVEFORM_CYCLES + 1);
    CHECK(GUI::TilePeriodicTimes({}, 1e3, 1).empty());
    CHECK(GUI::TilePeriodicTimes(one_period, 0.0, 1).empty());
}

TEST_CASE("Harmonic summaries compute DC, THD percent, dBc, phase and CSV export", "[harmonic_balance_view]") {
    auto [schematic, measured_node] = BuildMeasuredDividerSchematic();
    const Core::HarmonicBalance result = MakeSampleResult(measured_node);
    const std::vector<GUI::HarmonicTraceSummary> summaries =
        GUI::BuildHarmonicSummaries(result, schematic, GUI::HarmonicAmplitudeUnit::PEAK);
    REQUIRE(summaries.size() == 2);

    const GUI::HarmonicTraceSummary &v_summary = summaries[0];
    CHECK_FALSE(v_summary.IsCurrent);
    CHECK_THAT(v_summary.DcValue, WithinAbs(-0.5, 1e-12));
    // sqrt(0.6^2 + 0.8^2) / 2.0 = 1.0 / 2.0 = 50%
    CHECK_THAT(v_summary.ThdPercent, WithinAbs(50.0, 1e-9));
    REQUIRE(v_summary.Rows.size() == 4);
    CHECK_THAT(v_summary.Rows[1].PhaseDegrees, WithinAbs(0.0, 1e-9));
    CHECK_THAT(v_summary.Rows[2].PhaseDegrees, WithinAbs(90.0, 1e-9));
    CHECK_THAT(v_summary.Rows[3].PhaseDegrees, WithinAbs(-90.0, 1e-9));

    const std::string csv_rms = GUI::BuildHarmonicBalanceCsv(result, schematic, GUI::HarmonicAmplitudeUnit::RMS);
    CHECK_THAT(csv_rms, ContainsSubstring("Magnitude (RMS)"));
    CHECK_THAT(csv_rms, ContainsSubstring("I(R1)"));
    CHECK_THAT(csv_rms, ContainsSubstring("Time (s)"));

    const GUI::ExportFigure figure = GUI::BuildHarmonicBalanceFigure(result, schematic, 2, std::nullopt);
    REQUIRE(figure.Panels.size() == 1);
    CHECK(figure.Xs.size() == 9);
    REQUIRE(figure.Panels[0].YAxes.size() == 2);
    REQUIRE(figure.Panels[0].Series.size() == 2);
    CHECK_FALSE(figure.Panels[0].Series[0].Dashed);
    CHECK(figure.Panels[0].Series[1].Dashed);
}

TEST_CASE("BuildHarmonicWaveforms prepares immutable multi-trace waveforms preserving color indices",
          "[harmonic_balance_view]") {
    auto [schematic, measured_node] = BuildMeasuredDividerSchematic();
    Core::HarmonicBalance result;
    result.FundamentalFrequency = 1e3;
    result.Frequencies = {0.0, 1e3};
    result.Times = {0.0, 0.25e-3, 0.5e-3, 0.75e-3};

    // Add extra voltage and current slots so total measured traces >= 3 and source indices are non-sequential
    const int extra_node = measured_node + 1;
    schematic.SetVoltageMeasured(extra_node, true);
    schematic.SetCurrentMeasured("V1", true);
    schematic.SetCurrentMeasured("R1", true);

    result.NodeWaveforms.resize(static_cast<std::size_t>(extra_node) + 2);
    result.NodeWaveforms[static_cast<std::size_t>(measured_node)] = {1.0, 2.0, -1.0, -2.0};
    result.NodeWaveforms[static_cast<std::size_t>(extra_node)] = {0.5, 0.25, -0.5, -0.25};
    // Mismatched-length waveform on a measured node would fail alignment if not filtered
    schematic.SetVoltageMeasured(extra_node + 1, true);
    result.NodeWaveforms[static_cast<std::size_t>(extra_node) + 1] = {9.0, 9.0};

    // Index 0 unmeasured ("D1"), index 1 measured ("V1"), index 2 measured ("R1")
    result.CurrentWaveforms = {
        {.Name = "D1", .Values = {0.0, 0.0, 0.0, 0.0}},
        {.Name = "V1", .Values = {-0.1, -0.2, 0.1, 0.2}},
        {.Name = "R1", .Values = {0.1, 0.2, -0.1, -0.2}},
    };

    const GUI::HarmonicWaveformSet waveforms = GUI::BuildHarmonicWaveforms(result, schematic, 2);
    REQUIRE(waveforms.Times.size() == 9);
    REQUIRE(waveforms.Traces.size() == 4);

    // Voltages first, then currents, keeping original indices for stable plot colors
    CHECK_FALSE(waveforms.Traces[0].IsCurrent);
    CHECK(waveforms.Traces[0].SourceIndex == static_cast<std::size_t>(measured_node));
    CHECK_FALSE(waveforms.Traces[1].IsCurrent);
    CHECK(waveforms.Traces[1].SourceIndex == static_cast<std::size_t>(extra_node));
    CHECK(waveforms.Traces[2].IsCurrent);
    CHECK(waveforms.Traces[2].Label == "I(V1)");
    CHECK(waveforms.Traces[2].SourceIndex == 1);
    CHECK(waveforms.Traces[3].IsCurrent);
    CHECK(waveforms.Traces[3].Label == "I(R1)");
    CHECK(waveforms.Traces[3].SourceIndex == 2);

    std::vector<const std::vector<double> *> trace_ptrs;
    for (const GUI::HarmonicWaveformTrace &trace : waveforms.Traces) {
        REQUIRE(trace.Values.size() == waveforms.Times.size());
        trace_ptrs.push_back(&trace.Values);
    }
    CHECK_THAT((*trace_ptrs[0]).back(), WithinAbs(1.0, 1e-12));
    CHECK_THAT((*trace_ptrs[2])[1], WithinAbs(-0.2, 1e-12));
    CHECK_THAT((*trace_ptrs[3]).back(), WithinAbs(0.1, 1e-12));
}
