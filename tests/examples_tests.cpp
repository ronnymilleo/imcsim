/**
 * @file    examples_tests.cpp
 * @brief   Tests that every example circuit opens cleanly and simulates with its own settings.
 */

#include "examples.h"
#include "schematic.h"
#include "simulator.h"
#include "test_printers.h"
#include "trace_statistics.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <format>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

// Loads an example into a schematic, as the editor does
void OpenOrFail(const GUI::Example &example, GUI::Schematic &schematic) {
    auto loaded = GUI::LoadExample(example);
    if (!loaded) {
        FAIL(loaded.error());
    }
    CHECK(loaded->Warnings.empty());
    schematic.Replace(std::move(loaded->Elements), std::move(loaded->Wires), std::move(loaded->Settings));
    schematic.RestoreMeasurements(loaded->Measurements);
    // Every saved voltage lands on a node, so none is dropped on the way in
    CHECK(schematic.SaveMeasurements() == loaded->Measurements);
}

// Opens the example saved in a file of the given name
void OpenExample(const std::string_view file_name, GUI::Schematic &schematic) {
    const auto example = std::ranges::find(GUI::GetExamples(), file_name, &GUI::Example::FileName);
    REQUIRE(example != GUI::GetExamples().end());
    OpenOrFail(*example, schematic);
}

std::size_t NodeAt(GUI::Schematic &schematic, const GUI::GridPoint point) {
    const std::optional<int> node = schematic.GetConnectivity().GetNode(point);
    REQUIRE(node);
    return static_cast<std::size_t>(*node);
}

// Largest magnitude of a waveform, either sign
double PeakOf(const std::vector<double> &values) {
    return std::abs(std::ranges::max(values, {}, [](const double value) { return std::abs(value); }));
}

} // namespace

TEST_CASE("Every example opens without warnings, measures existing traces and runs", "[examples]") {
    REQUIRE_FALSE(GUI::GetExamples().empty());
    for (const GUI::Example &example : GUI::GetExamples()) {
        INFO(example.Title);
        GUI::Schematic schematic;
        OpenOrFail(example, schematic);
        CHECK_FALSE(schematic.GetElements().empty());

        const Core::OperatingPointRun operating_point = Core::RunOperatingPoint(schematic.BuildCircuit());
        REQUIRE(operating_point.Result.has_value());
        const GUI::SavedMeasurements measurements = schematic.SaveMeasurements();
        CHECK_FALSE((measurements.Voltages.empty() && measurements.Currents.empty()));
        for (const std::string &name : measurements.Currents) {
            INFO(name);
            CHECK(std::ranges::contains(operating_point.Result->Currents, name, &Core::ComponentCurrent::Name));
        }
        const Core::TransientRun transient =
            Core::RunTransient(schematic.BuildCircuit(), schematic.GetSimulationSettings().Transient);
        CHECK(transient.Result.has_value());
    }
}

TEST_CASE("The BJT example draws one collector curve per base current", "[examples]") {
    const auto example = std::ranges::find(GUI::GetExamples(), std::string_view("bjt_output_characteristics.imcsim"),
                                           &GUI::Example::FileName);
    REQUIRE(example != GUI::GetExamples().end());
    GUI::Schematic schematic;
    OpenOrFail(*example, schematic);

    const GUI::SimulationSettings &settings = schematic.GetSimulationSettings();
    REQUIRE(settings.StepSource);
    const Core::DCSweepRun run =
        Core::RunDCSweep(schematic.BuildCircuit(), {.Swept = settings.SweptRange, .Stepped = settings.SteppedRange});
    REQUIRE(run.Result.has_value());
    CHECK(run.Result->Curves.size() == 5);
}

TEST_CASE("The RC example measures its corner frequency over the whole sweep", "[examples]") {
    const auto example =
        std::ranges::find(GUI::GetExamples(), std::string_view("rc_low_pass_filter.imcsim"), &GUI::Example::FileName);
    REQUIRE(example != GUI::GetExamples().end());
    GUI::Schematic schematic;
    OpenOrFail(*example, schematic);

    const Core::ACSweepRun run = Core::RunACSweep(schematic.BuildCircuit(), schematic.GetSimulationSettings().ACSweep);
    REQUIRE(run.Result.has_value());
    const Core::ACSweep &sweep = *run.Result;
    // Node 2 is the capacitor, the output of the filter
    REQUIRE(sweep.NodeMagnitudesDecibels.size() > 2);
    const auto statistics =
        GUI::ComputeBodeStatistics(sweep.Frequencies, sweep.NodeMagnitudesDecibels[2], sweep.NodePhasesDegrees[2],
                                   sweep.Frequencies.front(), sweep.Frequencies.back());
    REQUIRE(statistics);
    REQUIRE(statistics->UpperCutoff);
    const double corner = 1.0 / (2.0 * std::numbers::pi * 1e3 * 100e-9);
    CHECK_THAT(*statistics->UpperCutoff, Catch::Matchers::WithinRel(corner, 2e-2));
    CHECK_FALSE(statistics->LowerCutoff);
}

TEST_CASE("The hybrid-pi model has the gain of its transistor in the middle band only", "[examples]") {
    GUI::Schematic schematic;
    OpenExample("hybrid_pi_model.imcsim", schematic);
    const Core::ACSweepRun run = Core::RunACSweep(schematic.BuildCircuit(), schematic.GetSimulationSettings().ACSweep);
    REQUIRE(run.Result.has_value());
    const Core::ACSweep &sweep = *run.Result;
    const std::vector<double> &real = sweep.NodeMagnitudesDecibels[NodeAt(schematic, {13, 6})];
    const std::vector<double> &model = sweep.NodeMagnitudesDecibels[NodeAt(schematic, {21, 18})];
    const auto at = [&sweep](const double frequency) {
        return static_cast<std::size_t>(std::ranges::lower_bound(sweep.Frequencies, frequency) -
                                        sweep.Frequencies.begin());
    };
    // gm (RC || ro) = 227, or 47 dB, for both at 1 kHz
    CHECK_THAT(real[at(1e3)], Catch::Matchers::WithinAbs(47.1, 0.2));
    CHECK_THAT(model[at(1e3)], Catch::Matchers::WithinAbs(real[at(1e3)], 0.1));
    // The junction capacitances of the real transistor cut its gain at high frequency; the model has none
    INFO(std::format("At 50 MHz: real {} dB, model {} dB", real[at(50e6)], model[at(50e6)]));
    CHECK(model[at(50e6)] - real[at(50e6)] > 10.0);
}

TEST_CASE("The ideal transformer doubles the voltage and halves the current", "[examples]") {
    GUI::Schematic schematic;
    OpenExample("ideal_transformer.imcsim", schematic);
    const Core::TransientRun run =
        Core::RunTransient(schematic.BuildCircuit(), schematic.GetSimulationSettings().Transient);
    REQUIRE(run.Result.has_value());
    const Core::Transient &transient = *run.Result;
    const double primary = PeakOf(transient.NodeVoltages[NodeAt(schematic, {4, 6})]);
    const double secondary = PeakOf(transient.NodeVoltages[NodeAt(schematic, {16, 6})]);
    CHECK_THAT(primary, Catch::Matchers::WithinRel(5.0, 1e-3));
    CHECK_THAT(secondary, Catch::Matchers::WithinRel(10.0, 1e-3));
    const auto current = [&transient](const std::string &name) {
        const auto trace = std::ranges::find(transient.Currents, name, &Core::ComponentTrace::Name);
        REQUIRE(trace != transient.Currents.end());
        return PeakOf(trace->Values);
    };
    // 10 V on 100 Ohm, and twice that drawn from the primary: the source sees 25 Ohm
    CHECK_THAT(current("R1"), Catch::Matchers::WithinRel(0.1, 1e-3));
    CHECK_THAT(current("F1"), Catch::Matchers::WithinRel(0.2, 1e-3));
}
