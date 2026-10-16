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

TEST_CASE("The load pulls the divider output below its unloaded value", "[examples]") {
    GUI::Schematic schematic;
    OpenExample("voltage_divider.imcsim", schematic);
    const Core::OperatingPointRun run = Core::RunOperatingPoint(schematic.BuildCircuit());
    REQUIRE(run.Result.has_value());
    // 10k from 10 V over 10k in parallel with 10k: 10 * 5k / 15k
    CHECK_THAT(run.Result->NodeVoltages[NodeAt(schematic, {6, 10})], Catch::Matchers::WithinRel(10.0 / 3.0, 1e-3));
    const auto current = [&run](const std::string &name) {
        const auto found = std::ranges::find(run.Result->Currents, name, &Core::ComponentCurrent::Name);
        REQUIRE(found != run.Result->Currents.end());
        return found->Current;
    };
    CHECK_THAT(current("R1"), Catch::Matchers::WithinRel(2.0 / 3.0 * 1e-3, 1e-3));
    CHECK_THAT(current("R3"), Catch::Matchers::WithinRel(1.0 / 3.0 * 1e-3, 1e-3));
}

TEST_CASE("The Zener holds the output near its breakdown voltage once the supply passes it", "[examples]") {
    GUI::Schematic schematic;
    OpenExample("zener_regulator.imcsim", schematic);
    const GUI::SimulationSettings &settings = schematic.GetSimulationSettings();
    const Core::DCSweepRun run = Core::RunDCSweep(schematic.BuildCircuit(), {.Swept = settings.SweptRange});
    REQUIRE(run.Result.has_value());
    const Core::DCSweep &sweep = *run.Result;
    REQUIRE(sweep.Curves.size() == 1);
    const std::vector<double> &output = sweep.Curves.front().NodeVoltages[NodeAt(schematic, {12, 6})];
    const auto at = [&sweep](const double supply) {
        return static_cast<std::size_t>(std::ranges::lower_bound(sweep.SweptValues, supply - 1e-6) -
                                        sweep.SweptValues.begin());
    };
    // Below breakdown the 330 Ohm and the 1k load divide the supply
    CHECK_THAT(output[at(3.0)], Catch::Matchers::WithinRel(3.0 * 1000.0 / 1330.0, 2e-2));
    CHECK_THAT(output[at(12.0)], Catch::Matchers::WithinAbs(5.1, 0.4));
    CHECK_THAT(output[at(20.0)], Catch::Matchers::WithinAbs(5.1, 0.4));
    CHECK(output[at(20.0)] - output[at(12.0)] < 0.3);
}

TEST_CASE("The Schottky conducts first, then the 1N4007, then the 1N4148", "[examples]") {
    GUI::Schematic schematic;
    OpenExample("diode_curves.imcsim", schematic);
    const GUI::SimulationSettings &settings = schematic.GetSimulationSettings();
    const Core::DCSweepRun run = Core::RunDCSweep(schematic.BuildCircuit(), {.Swept = settings.SweptRange});
    REQUIRE(run.Result.has_value());
    const Core::DCSweep &sweep = *run.Result;
    const auto current_at = [&sweep](const std::string &name, const double volts) {
        const auto trace = std::ranges::find(sweep.Curves.front().Currents, name, &Core::ComponentTrace::Name);
        REQUIRE(trace != sweep.Curves.front().Currents.end());
        const auto index = static_cast<std::size_t>(std::ranges::lower_bound(sweep.SweptValues, volts - 1e-6) -
                                                    sweep.SweptValues.begin());
        return trace->Values[index];
    };
    for (const double volts : {0.3, 0.5}) {
        INFO(std::format("At {} V: 1N4148 {} A, 1N4007 {} A, 1N5819 {} A", volts, current_at("D1", volts),
                         current_at("D2", volts), current_at("D3", volts)));
        CHECK(current_at("D3", volts) > current_at("D1", volts));
        CHECK(current_at("D2", volts) > current_at("D1", volts));
    }
}

TEST_CASE("The series RLC peaks at its resonance with the bandwidth its quality factor gives", "[examples]") {
    GUI::Schematic schematic;
    OpenExample("series_rlc.imcsim", schematic);
    const Core::ACSweepRun run = Core::RunACSweep(schematic.BuildCircuit(), schematic.GetSimulationSettings().ACSweep);
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    const Core::ACSweep &sweep = *run.Result;
    const std::size_t output = NodeAt(schematic, {17, 4});
    const auto statistics = GUI::ComputeBodeStatistics(sweep.Frequencies, sweep.NodeMagnitudesDecibels[output],
                                                       sweep.NodePhasesDegrees[output], sweep.Frequencies.front(),
                                                       sweep.Frequencies.back());
    REQUIRE(statistics);
    const double resonance = 1.0 / (2.0 * std::numbers::pi * std::sqrt(10e-3 * 100e-9));
    CHECK_THAT(statistics->PeakFrequency, Catch::Matchers::WithinRel(resonance, 3e-2));
    CHECK_THAT(statistics->PeakGain, Catch::Matchers::WithinAbs(0.0, 0.1));
    REQUIRE(statistics->LowerCutoff);
    REQUIRE(statistics->UpperCutoff);
    // Bandwidth R / (2 pi L)
    const double bandwidth = 100.0 / (2.0 * std::numbers::pi * 10e-3);
    CHECK_THAT(*statistics->UpperCutoff - *statistics->LowerCutoff, Catch::Matchers::WithinRel(bandwidth, 5e-2));
}

TEST_CASE("The NMOS pulls its drain low only while the gate is high", "[examples]") {
    GUI::Schematic schematic;
    OpenExample("nmos_switch.imcsim", schematic);
    const Core::TransientRun run =
        Core::RunTransient(schematic.BuildCircuit(), schematic.GetSimulationSettings().Transient);
    REQUIRE(run.Result.has_value());
    const Core::Transient &transient = *run.Result;
    const std::vector<double> &gate = transient.NodeVoltages[NodeAt(schematic, {2, 12})];
    const std::vector<double> &drain = transient.NodeVoltages[NodeAt(schematic, {12, 9})];
    double on_drain = 0.0;
    double off_drain = 12.0;
    for (std::size_t i = 0; i < gate.size(); ++i) {
        // Away from the edges, where the gate capacitance still moves
        if (gate[i] > 4.9 && transient.Times[i] > 0.1e-3) {
            on_drain = std::max(on_drain, drain[i]);
        } else if (gate[i] < 0.1 && transient.Times[i] > 0.1e-3) {
            off_drain = std::min(off_drain, drain[i]);
        }
    }
    INFO(std::format("Drain {} V while on, at least {} V while off", on_drain, off_drain));
    CHECK(on_drain < 1.0);
    CHECK(off_drain > 11.9);
}

TEST_CASE("The MOSFET drain current grows with the gate voltage", "[examples]") {
    GUI::Schematic schematic;
    OpenExample("mosfet_output_characteristics.imcsim", schematic);
    const GUI::SimulationSettings &settings = schematic.GetSimulationSettings();
    REQUIRE(settings.StepSource);
    const Core::DCSweepRun run =
        Core::RunDCSweep(schematic.BuildCircuit(), {.Swept = settings.SweptRange, .Stepped = settings.SteppedRange});
    REQUIRE(run.Result.has_value());
    const Core::DCSweep &sweep = *run.Result;
    REQUIRE(sweep.Curves.size() == 5);
    double previous = 0.0;
    for (const Core::DCSweepCurve &curve : sweep.Curves) {
        const auto trace = std::ranges::find(curve.Currents, "M1.D", &Core::ComponentTrace::Name);
        REQUIRE(trace != curve.Currents.end());
        const double saturated = trace->Values.back();
        INFO(std::format("Gate {} V: {} A at the end of the sweep", curve.StepValue, saturated));
        CHECK(saturated > previous);
        previous = saturated;
    }
}

TEST_CASE("The class B stage has a dead zone around zero that the class AB stage does not", "[examples]") {
    GUI::Schematic schematic;
    OpenExample("class_ab_amplifier.imcsim", schematic);
    const Core::TransientRun run =
        Core::RunTransient(schematic.BuildCircuit(), schematic.GetSimulationSettings().Transient);
    REQUIRE(run.Result.has_value());
    const Core::Transient &transient = *run.Result;
    const std::vector<double> &input = transient.NodeVoltages[NodeAt(schematic, {2, 18})];
    const std::vector<double> &class_b = transient.NodeVoltages[NodeAt(schematic, {15, 18})];
    const std::vector<double> &class_ab = transient.NodeVoltages[NodeAt(schematic, {39, 18})];
    double dead_zone_b = 0.0;
    double tracking_error_ab = 0.0;
    for (std::size_t i = 0; i < input.size(); ++i) {
        // Skip the first millisecond, while the coupling capacitor settles
        if (transient.Times[i] < 1e-3) {
            continue;
        }
        if (std::abs(input[i]) < 0.3) {
            dead_zone_b = std::max(dead_zone_b, std::abs(class_b[i]));
        }
        tracking_error_ab = std::max(tracking_error_ab, std::abs(class_ab[i] - 0.9 * input[i]));
    }
    // The labels name the nodes the plots show, and IN joins the input to the class AB stage with no wire
    const auto net_name = [&schematic](const GUI::GridPoint point) {
        return schematic.GetConnectivity().GetNetName(*schematic.GetConnectivity().GetNode(point));
    };
    CHECK(net_name({2, 18}) == "IN");
    CHECK(net_name({26, 17}) == "IN");
    CHECK(net_name({15, 18}) == "OUT_B");
    CHECK(net_name({39, 18}) == "OUT_AB");
    // No transistor of the class B stage conducts below about 0.6 V; the class AB one follows the input
    CHECK(dead_zone_b < 0.01);
    CHECK(tracking_error_ab < 0.2);
    CHECK(PeakOf(class_ab) > 1.2);
    CHECK(PeakOf(class_b) < 0.8);
}

namespace {

// Gain in dB of a node at the sweep point nearest to a frequency
double GainAt(const Core::ACSweep &sweep, const std::size_t node, const double frequency) {
    const auto index = static_cast<std::size_t>(std::ranges::lower_bound(sweep.Frequencies, frequency * 0.999) -
                                                sweep.Frequencies.begin());
    return sweep.NodeMagnitudesDecibels[node][index];
}

// Runs the AC sweep of an example with its own settings
Core::ACSweep SweepOf(GUI::Schematic &schematic) {
    const Core::ACSweepRun run = Core::RunACSweep(schematic.BuildCircuit(), schematic.GetSimulationSettings().ACSweep);
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    return *run.Result;
}

GUI::BodeStatistics BodeOf(const Core::ACSweep &sweep, const std::size_t node) {
    const auto statistics =
        GUI::ComputeBodeStatistics(sweep.Frequencies, sweep.NodeMagnitudesDecibels[node], sweep.NodePhasesDegrees[node],
                                   sweep.Frequencies.front(), sweep.Frequencies.back());
    REQUIRE(statistics);
    return *statistics;
}

} // namespace

TEST_CASE("The non-inverting amplifier has a gain of 11 that falls at the gain-bandwidth over 11", "[examples]") {
    GUI::Schematic schematic;
    OpenExample("non_inverting_amplifier.imcsim", schematic);
    const Core::ACSweep sweep = SweepOf(schematic);
    const std::size_t output = NodeAt(schematic, {16, 1});
    CHECK_THAT(GainAt(sweep, output, 1e3), Catch::Matchers::WithinAbs(20.0 * std::log10(11.0), 0.3));
    const GUI::BodeStatistics bode = BodeOf(sweep, output);
    REQUIRE(bode.UpperCutoff);
    CHECK_THAT(*bode.UpperCutoff, Catch::Matchers::WithinRel(1e6 / 11.0, 0.1));
}

TEST_CASE("The integrator falls 20 dB per decade and crosses 0 dB at 1 over 2 pi R C", "[examples]") {
    GUI::Schematic schematic;
    OpenExample("op_amp_integrator.imcsim", schematic);
    const Core::ACSweep sweep = SweepOf(schematic);
    const std::size_t output = NodeAt(schematic, {16, 1});
    CHECK_THAT(GainAt(sweep, output, 1e3) - GainAt(sweep, output, 1e4), Catch::Matchers::WithinAbs(20.0, 1.0));
    const GUI::BodeStatistics bode = BodeOf(sweep, output);
    REQUIRE(bode.UnityGainFrequency);
    CHECK_THAT(*bode.UnityGainFrequency,
               Catch::Matchers::WithinRel(1.0 / (2.0 * std::numbers::pi * 10e3 * 10e-9), 0.1));
}

TEST_CASE("The Schmitt trigger flips only when the input crosses its thresholds", "[examples]") {
    GUI::Schematic schematic;
    OpenExample("schmitt_trigger.imcsim", schematic);
    const Core::TransientRun run =
        Core::RunTransient(schematic.BuildCircuit(), schematic.GetSimulationSettings().Transient);
    REQUIRE(run.Result.has_value());
    const Core::Transient &transient = *run.Result;
    const std::vector<double> &input = transient.NodeVoltages[NodeAt(schematic, {2, 7})];
    const std::vector<double> &output = transient.NodeVoltages[NodeAt(schematic, {16, 15})];
    // The output saturates near the supplies, and the thresholds are that swing over 1 + 10k / 1k
    const double threshold = PeakOf(output) / 11.0;
    int flips = 0;
    for (std::size_t i = 1; i < output.size(); ++i) {
        if (output[i - 1] * output[i] >= 0.0) {
            continue;
        }
        ++flips;
        INFO(std::format("Flip at {} s with the input at {} V", transient.Times[i], input[i]));
        // The input is on the inverting side, so the output falls only after it passes the upper threshold and
        // rises only after it passes the lower one. The output takes a moment to slew through zero, so the input
        // is a little past the threshold by then
        if (output[i] < 0.0) {
            CHECK(input[i] > threshold - 0.1);
        } else {
            CHECK(input[i] < -threshold + 0.1);
        }
    }
    // Three cycles of 1 kHz give six crossings of a threshold. The circuit is bistable at rest, so ngspice may start
    // the output on either supply, and then the first crossing flips nothing: that varies with the platform
    CHECK(flips >= 4);
    CHECK(flips <= 6);
    CHECK(PeakOf(output) > 13.0);
}

TEST_CASE("The Sallen-Key filter has its corner near 1.1 kHz and falls 40 dB per decade", "[examples]") {
    GUI::Schematic schematic;
    OpenExample("sallen_key_low_pass.imcsim", schematic);
    const Core::ACSweep sweep = SweepOf(schematic);
    const std::size_t output = NodeAt(schematic, {14, 21});
    // fc = 1 / (2 pi R sqrt(C1 C2)) = 1.07 kHz, and Q = 0.74 moves the -3 dB point up to 1.12 kHz
    const GUI::BodeStatistics bode = BodeOf(sweep, output);
    REQUIRE(bode.UpperCutoff);
    CHECK_THAT(*bode.UpperCutoff, Catch::Matchers::WithinRel(1.12e3, 0.05));
    CHECK_THAT(GainAt(sweep, output, 1e4) - GainAt(sweep, output, 1e5), Catch::Matchers::WithinAbs(40.0, 1.0));
}

TEST_CASE("The common-emitter amplifier has a mid-band gain of about 43 dB between its two corners", "[examples]") {
    GUI::Schematic schematic;
    OpenExample("common_emitter_amplifier.imcsim", schematic);
    const Core::OperatingPointRun operating_point = Core::RunOperatingPoint(schematic.BuildCircuit());
    REQUIRE(operating_point.Result.has_value());
    const auto collector = std::ranges::find(operating_point.Result->Currents, "Q1.C", &Core::ComponentCurrent::Name);
    REQUIRE(collector != operating_point.Result->Currents.end());
    CHECK_THAT(collector->Current, Catch::Matchers::WithinAbs(1.2e-3, 0.2e-3));

    const Core::ACSweep sweep = SweepOf(schematic);
    const std::size_t output = NodeAt(schematic, {23, 14});
    CHECK_THAT(GainAt(sweep, output, 1e3), Catch::Matchers::WithinAbs(43.0, 1.0));
    const GUI::BodeStatistics bode = BodeOf(sweep, output);
    REQUIRE(bode.LowerCutoff);
    REQUIRE(bode.UpperCutoff);
    CHECK(*bode.LowerCutoff > 30.0);
    CHECK(*bode.LowerCutoff < 150.0);
    CHECK(*bode.UpperCutoff > 5e6);
}

TEST_CASE("The current mirror copies the reference current while Q2 is out of saturation", "[examples]") {
    GUI::Schematic schematic;
    OpenExample("current_mirror.imcsim", schematic);
    const GUI::SimulationSettings &settings = schematic.GetSimulationSettings();
    REQUIRE(settings.StepSource);
    const Core::DCSweepRun run =
        Core::RunDCSweep(schematic.BuildCircuit(), {.Swept = settings.SweptRange, .Stepped = settings.SteppedRange});
    REQUIRE(run.Result.has_value());
    const Core::DCSweep &sweep = *run.Result;
    REQUIRE(sweep.Curves.size() == 5);
    const auto index_of = [&sweep](const double volts) {
        return static_cast<std::size_t>(std::ranges::lower_bound(sweep.SweptValues, volts - 1e-6) -
                                        sweep.SweptValues.begin());
    };
    for (const Core::DCSweepCurve &curve : sweep.Curves) {
        const auto trace = std::ranges::find(curve.Currents, "Q2.C", &Core::ComponentTrace::Name);
        REQUIRE(trace != curve.Currents.end());
        // The reference branch is the supply less one Vbe over 10k
        const double reference = (curve.StepValue - 0.7) / 10e3;
        INFO(std::format("Supply {} V: reference {} A, mirrored {} A at 5 V and {} A at 0.3 V", curve.StepValue,
                         reference, trace->Values[index_of(5.0)], trace->Values[index_of(0.3)]));
        CHECK_THAT(trace->Values[index_of(5.0)], Catch::Matchers::WithinRel(reference, 0.15));
        // No current with 0 V across Q2, and the full copy once it is out of saturation
        CHECK(trace->Values[index_of(0.0)] < 0.1 * reference);
        CHECK(trace->Values[index_of(0.3)] > 0.9 * reference);
        CHECK(trace->Values[index_of(10.0)] > trace->Values[index_of(2.0)]);
    }
}

TEST_CASE("Every example is in a group, and the examples of a group are listed together", "[examples]") {
    const std::vector<const char *> groups = GUI::GetExampleGroups();
    REQUIRE_FALSE(groups.empty());
    // Each group once
    for (std::size_t i = 0; i < groups.size(); ++i) {
        for (std::size_t j = i + 1; j < groups.size(); ++j) {
            CHECK(std::string_view(groups[i]) != std::string_view(groups[j]));
        }
    }
    // Walking the list never comes back to an earlier group, so the menu shows each in one run
    std::size_t current = 0;
    for (const GUI::Example &example : GUI::GetExamples()) {
        INFO(example.Title);
        const auto group = std::ranges::find_if(
            groups, [&example](const char *name) { return std::string_view(name) == example.Group; });
        REQUIRE(group != groups.end());
        const auto index = static_cast<std::size_t>(group - groups.begin());
        CHECK(index >= current);
        current = index;
    }
}
