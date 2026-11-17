/**
 * @file    ai_copy_tests.cpp
 * @brief   Tests for the text that puts the circuit and the summary of its results in a conversation with an AI.
 */

#include "plots/ai_copy.h"

#include "plots/ai_result_summary.h"
#include "results/result_store.h"
#include "schematic/examples.h"
#include "schematic/schematic.h"
#include "test_printers.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cmath>
#include <numbers>
#include <string>
#include <vector>

namespace {

constexpr const char *ENGINE = "TestEngine";

/**
 * @brief   Opens the RC low-pass example, whose source drives node 1 and whose resistor joins nodes 1 and 2; it
 *          measures both.
 */
void OpenRCFilter(GUI::Schematic &schematic) {
    const auto example = std::ranges::find(GUI::GetExamples(), "rc_low_pass_filter.imcsim", &GUI::Example::FileName);
    REQUIRE(example != GUI::GetExamples().end());
    auto loaded = GUI::LoadExample(*example);
    REQUIRE(loaded.has_value());
    schematic.Replace(std::move(loaded->Elements), std::move(loaded->Wires), std::move(loaded->Settings));
    schematic.RestoreMeasurements(loaded->Measurements);
}

void Select(GUI::Schematic &schematic, const GUI::Analysis analysis) {
    GUI::SimulationSettings settings = schematic.GetSimulationSettings();
    settings.Selected = analysis;
    schematic.SetSimulationSettings(settings);
}

/// A transient of three nodes: ground, a 1 kHz sine of 5 V and a constant 2 V.
Core::Transient MakeTransient(const std::size_t points) {
    Core::Transient transient;
    transient.NodeVoltages.assign(3, {});
    for (std::size_t point = 0; point < points; ++point) {
        const double time = 10e-3 * static_cast<double>(point) / static_cast<double>(points - 1);
        transient.Times.push_back(time);
        transient.NodeVoltages[0].push_back(0.0);
        transient.NodeVoltages[1].push_back(5.0 * std::sin(2.0 * std::numbers::pi * 1e3 * time));
        transient.NodeVoltages[2].push_back(2.0);
    }
    return transient;
}

} // namespace

TEST_CASE("Without a result the text holds the circuit and the settings and says it has not been run", "[ai_copy]") {
    GUI::Schematic schematic;
    OpenRCFilter(schematic);
    Select(schematic, GUI::Analysis::TRANSIENT);
    const GUI::ResultStore results;

    const std::string text = GUI::BuildAICopyText(schematic, results, ENGINE);

    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("```spice"));
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring(schematic.BuildSpiceNetlist()));
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("TestEngine"));
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("## Parts"));
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("| R1 | Resistor |"));
    // A sine source reads as its symbol does, by its waveform, and ground has no row
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("| Vin1 | VoltageSource | 1V 1kHz |"));
    CHECK_THAT(text, !Catch::Matchers::ContainsSubstring("| Ground |"));
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("## Analysis: Transient"));
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("Stop time"));
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("has not been run"));
}

TEST_CASE("An operating point is written as a table of node voltages and part currents", "[ai_copy]") {
    GUI::Schematic schematic;
    OpenRCFilter(schematic);
    Select(schematic, GUI::Analysis::OPERATING_POINT);
    GUI::ResultStore results;
    results.SetOperatingPoint(Core::OperatingPoint{.NodeVoltages = {0.0, 5.0, 2.5},
                                                   .Currents = {{.Name = "R1", .Current = 1e-3}},
                                                   .Warnings = {"R1 dissipates a lot"}});

    const std::string text = GUI::BuildAICopyText(schematic, results, ENGINE);

    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("| 0 (ground) | 0V |"));
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("5.000V"));
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("2.500V"));
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("| R1 | 1.000mA |"));
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("- R1 dissipates a lot"));
    CHECK_THAT(text, !Catch::Matchers::ContainsSubstring("has not been run"));
}

TEST_CASE("A transient is summarized by the statistics of each node, not by its points", "[ai_copy]") {
    GUI::Schematic schematic;
    OpenRCFilter(schematic);
    Select(schematic, GUI::Analysis::TRANSIENT);
    GUI::ResultStore results;
    results.SetTransient(MakeTransient(1001));

    const std::string text = GUI::BuildAICopyText(schematic, results, ENGINE);

    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("1001 points"));
    CHECK_THAT(text,
               Catch::Matchers::ContainsSubstring("| Signal | Min | Max | Mean | RMS | Pk-Pk | Freq | First | Last |"));
    // The constant node reads 2 V in every column that has a level
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("| V(2) | 2.000V | 2.000V | 2.000V |"));
    // The sine peaks at 5 V and repeats at 1 kHz
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("| V(1) | -5.000V | 5.000V |"));
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("kHz"));
}

TEST_CASE("The text stays short however long the transient is", "[ai_copy]") {
    GUI::Schematic schematic;
    OpenRCFilter(schematic);
    Select(schematic, GUI::Analysis::TRANSIENT);
    GUI::ResultStore short_results;
    short_results.SetTransient(MakeTransient(100));
    GUI::ResultStore long_results;
    long_results.SetTransient(MakeTransient(200000));

    const std::string short_text = GUI::BuildAICopyText(schematic, short_results, ENGINE);
    const std::string long_text = GUI::BuildAICopyText(schematic, long_results, ENGINE);

    CHECK(long_text.size() < 10000);
    // Only the number of points differs
    CHECK(long_text.size() <= short_text.size() + 20);
}

TEST_CASE("Nodes beyond the traces a summary reads are counted, not listed", "[ai_copy]") {
    GUI::Schematic schematic;
    OpenRCFilter(schematic);
    Core::Transient transient = MakeTransient(50);
    transient.NodeVoltages.resize(30, transient.NodeVoltages[2]);
    // The example measures nodes 1 and 2, so the measured ones are read and the rest left out
    const GUI::SummaryContext context = GUI::MakeSummaryContext(schematic, transient.NodeVoltages.size());
    CHECK(context.Nodes.size() == 2);
    CHECK(context.OmittedNodes == 27);

    const std::string text = GUI::SummarizeTransient(transient, context);
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("27 more nodes are not listed"));
}

TEST_CASE("A stopped transient says so", "[ai_copy]") {
    GUI::Schematic schematic;
    OpenRCFilter(schematic);
    Core::Transient transient = MakeTransient(20);
    transient.IsComplete = false;
    transient.Warnings = {"Slow convergence"};

    const std::string text = GUI::SummarizeTransient(transient, GUI::MakeSummaryContext(schematic, 3));

    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("stopped before its end"));
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("- Slow convergence"));
}

TEST_CASE("An AC sweep reads the Bode figures of each node", "[ai_copy]") {
    GUI::Schematic schematic;
    OpenRCFilter(schematic);
    Core::ACSweep sweep;
    sweep.NodeMagnitudesDecibels.assign(3, {});
    sweep.NodePhasesDegrees.assign(3, {});
    for (int step = 0; step <= 60; ++step) {
        const double frequency = std::pow(10.0, step / 10.0);
        sweep.Frequencies.push_back(frequency);
        for (std::size_t node = 0; node < 3; ++node) {
            // A first-order low-pass with its corner at 1 kHz on node 2, and a flat node 1
            const double ratio = node == 2 ? frequency / 1e3 : 0.0;
            sweep.NodeMagnitudesDecibels[node].push_back(-10.0 * std::log10(1.0 + ratio * ratio));
            sweep.NodePhasesDegrees[node].push_back(-std::atan(ratio) * 180.0 / std::numbers::pi);
        }
    }

    const std::string text = GUI::SummarizeACSweep(sweep, GUI::MakeSummaryContext(schematic, 3));

    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("1Hz to 1MHz, 61 points"));
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("| Signal | Peak gain | Peak at | -3 dB low | -3 dB high |"));
    // The corner of 1 kHz falls between two points of the sweep, and the flat node has none
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("| V(2) | 0.00dB | 1.000Hz | \xE2\x80\x94 | 997.3Hz |"));
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("| V(1) | 0.00dB | 1.000Hz | \xE2\x80\x94 | \xE2\x80\x94 |"));
}

TEST_CASE("A DC sweep reads each node over the swept range", "[ai_copy]") {
    GUI::Schematic schematic;
    OpenRCFilter(schematic);
    Core::DCSweep sweep;
    sweep.SweptSource = "V1";
    sweep.SweptUnit = "V";
    for (int step = 0; step <= 10; ++step) {
        sweep.SweptValues.push_back(step);
    }
    Core::DCSweepCurve curve;
    curve.NodeVoltages.assign(3, {});
    for (const double value : sweep.SweptValues) {
        curve.NodeVoltages[1].push_back(value);
        curve.NodeVoltages[2].push_back(value / 2.0);
    }
    sweep.Curves.push_back(curve);

    const std::string text = GUI::SummarizeDCSweep(sweep, GUI::MakeSummaryContext(schematic, 3));

    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("Swept V1 from 0V to 10V, 11 points"));
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("| V(2) | 0V | 5.000V | 2.500V | 0V | 5.000V |"));
}

TEST_CASE("A noise result gives the totals and the share of each part, largest first", "[ai_copy]") {
    GUI::Schematic schematic;
    OpenRCFilter(schematic);
    Core::Noise noise;
    noise.OutputNode = 2;
    noise.InputSource = "V1";
    noise.InputUnit = "V";
    noise.Frequencies = {10.0, 100e3};
    noise.OutputTotal = 2e-6;
    noise.InputTotal = 3e-6;
    noise.Contributions = {{.Name = "R2", .OutputDensities = {}, .OutputTotal = 1e-6},
                           {.Name = "R1", .OutputDensities = {}, .OutputTotal = 1.7320508e-6}};

    const std::string text = GUI::SummarizeNoise(noise, GUI::MakeSummaryContext(schematic, 3));

    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("RMS noise at the output: 2.000uV"));
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("referred to the input: 3.000uV"));
    // 3/4 of the power comes from R1 and 1/4 from R2
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("| R1 | 1.732uV | 75.0 % |"));
    CHECK(text.find("| R1 |") < text.find("| R2 |"));
}

TEST_CASE("A transfer function gives its gain and impedances", "[ai_copy]") {
    GUI::Schematic schematic;
    OpenRCFilter(schematic);

    const std::string text = GUI::SummarizeTransferFunction({.OutputNode = 2,
                                                             .InputSource = "V1",
                                                             .InputUnit = "V",
                                                             .Gain = 0.5,
                                                             .InputImpedance = 2e3,
                                                             .OutputImpedance = 500.0},
                                                            GUI::MakeSummaryContext(schematic, 3));

    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("From V1 to V(2)"));
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("DC gain: 500.0m"));
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("Input impedance: 2.000kOhm"));
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("Output impedance: 500.0Ohm"));
}

TEST_CASE("A sensitivity result ranks the parts", "[ai_copy]") {
    GUI::Schematic schematic;
    OpenRCFilter(schematic);
    Core::Sensitivity sensitivity{.OutputNode = 2, .Parts = {}};
    sensitivity.Parts.push_back({.Name = "R1",
                                 .Type = Core::ComponentType::RESISTOR,
                                 .Element = "R1",
                                 .Unit = "Ohm",
                                 .Value = 1e3,
                                 .Absolute = -1e-3,
                                 .Normalized = -0.01});
    sensitivity.Parts.push_back({.Name = "R2",
                                 .Type = Core::ComponentType::RESISTOR,
                                 .Element = "R2",
                                 .Unit = "Ohm",
                                 .Value = 1e3,
                                 .Absolute = 1e-4,
                                 .Normalized = 0.001});

    const std::string text = GUI::SummarizeSensitivity(sensitivity, GUI::MakeSummaryContext(schematic, 3));

    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("DC sensitivity of V(2)"));
    CHECK_THAT(text,
               Catch::Matchers::ContainsSubstring("| Part | Value | Per unit of the value | Per 1 % of the value |"));
    CHECK(text.find("R1") < text.find("R2"));
}

TEST_CASE("An AC sensitivity result is read at the -3 dB frequency", "[ai_copy]") {
    GUI::Schematic schematic;
    OpenRCFilter(schematic);
    Core::ACSensitivity sensitivity;
    sensitivity.OutputNode = 2;
    sensitivity.InputSource = "V1";
    sensitivity.Frequencies = {100.0, 1e3, 10e3};
    sensitivity.Output = {{1.0, 0.0}, {0.7, -0.7}, {0.0, -0.1}};
    sensitivity.Parts.push_back({.Name = "R1",
                                 .Type = Core::ComponentType::RESISTOR,
                                 .Element = "R1",
                                 .Unit = "Ohm",
                                 .Value = 1e3,
                                 .MagnitudeDecibelsPerPercent = {-0.01, -0.04, -0.01},
                                 .PhaseDegreesPerPercent = {-0.1, -0.3, -0.1}});

    const std::string text = GUI::SummarizeACSensitivity(sensitivity, GUI::MakeSummaryContext(schematic, 3));

    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("AC sensitivity of V(2) to V1"));
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("| Part | Value | Gain per 1 % | Phase per 1 % |"));
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("R1"));
}

TEST_CASE("Poles and zeros are listed with their frequencies", "[ai_copy]") {
    GUI::Schematic schematic;
    OpenRCFilter(schematic);
    Core::PoleZero result;
    result.OutputNode = 2;
    result.InputSource = "V1";
    result.Roots.push_back({.IsPole = true,
                            .Real = -1e3,
                            .Imaginary = 0.0,
                            .NaturalFrequencyHertz = 159.2,
                            .QualityFactor = std::nullopt,
                            .DampingRatio = std::nullopt,
                            .IsRightHalfPlane = false});
    result.Warnings = {"One pole was refined"};

    const std::string text = GUI::SummarizePoleZero(result, GUI::MakeSummaryContext(schematic, 3));

    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("| pole |"));
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("- One pole was refined"));
}

TEST_CASE("S-parameters are listed at the ends and the middle of the sweep, with the stability of a two-port",
          "[ai_copy]") {
    Core::SParameters result;
    result.Frequencies = {1e6, 1e7, 1e8};
    result.ReferenceOhms = {50.0, 50.0};
    for (std::size_t frequency = 0; frequency < 3; ++frequency) {
        // S11 = 0.1, S12 = 0, S21 = 1, S22 = 0.1: a matched, unilateral amplifier
        result.Values.insert(result.Values.end(), {{0.1, 0.0}, {0.0, 0.0}, {1.0, 0.0}, {0.1, 0.0}});
    }

    const std::string text = GUI::SummarizeSParameters(result);

    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("2 ports, 1MHz to 100MHz, 3 points"));
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("At 10MHz"));
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("| S21 | 0.00 dB | 0.0 deg |"));
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("| S11 | -20.00 dB |"));
}

TEST_CASE("A truth table is written with a row per combination and cut when it is long", "[ai_copy]") {
    GUI::Schematic schematic;
    OpenRCFilter(schematic);
    Core::TruthTable table;
    table.InputNames = {"Vlogic1"};
    table.InputLevels = {5.0};
    table.OutputNodes = {2};
    for (int row = 0; row < 40; ++row) {
        table.Rows.push_back({.InputsHigh = {row % 2 == 1}, .OutputVoltages = {row % 2 == 1 ? 0.0 : 5.0}});
    }

    const std::string text =
        GUI::SummarizeTruthTable(table, schematic.BuildCircuit(), GUI::MakeSummaryContext(schematic, 3));

    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("Vlogic1"));
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("Only the first 32 of 40 combinations"));
}

TEST_CASE("A harmonic balance result gives the DC value, fundamental and THD of each node", "[ai_copy]") {
    GUI::Schematic schematic;
    OpenRCFilter(schematic);
    Core::HarmonicBalance result;
    result.FundamentalFrequency = 1e3;
    result.Frequencies = {0.0, 1e3, 2e3};
    result.NodePhasors = {{{0.0, 0.0}, {0.0, 0.0}, {0.0, 0.0}},
                          {{1.0, 0.0}, {2.0, 0.0}, {0.2, 0.0}},
                          {{0.0, 0.0}, {1.0, 0.0}, {0.0, 0.0}}};
    result.Iterations = 4;
    result.Strategy = "Newton";

    const std::string text = GUI::SummarizeHarmonicBalance(result, GUI::MakeSummaryContext(schematic, 3));

    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("Fundamental 1kHz, 2 harmonics, 4 iterations (Newton)"));
    // The second harmonic is a tenth of the fundamental
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("| V(1) | 1.000V | 2.000V | 10.00 % |"));
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("| V(2) | 0V | 1.000V | 0.00 % |"));
}

TEST_CASE("A Monte Carlo run is summarized by the spread of each measured quantity", "[ai_copy]") {
    GUI::Schematic schematic;
    OpenRCFilter(schematic);
    GUI::SimulationSettings settings = schematic.GetSimulationSettings();
    settings.Selected = GUI::Analysis::TRANSIENT;
    settings.MonteCarlo.Enabled = true;
    schematic.SetSimulationSettings(settings);
    GUI::MonteCarloTable table;
    table.Parts = {"R1"};
    table.Units = {"Ohm"};
    table.Columns = {{.Node = 2, .Kind = GUI::MeasureKind::MEAN, .Current = {}}};
    table.RequestedRuns = 3;
    for (const double value : {2.5, 2.4, 2.5, 2.6}) {
        table.Runs.push_back({.Values = {1e3}, .Measures = {value}, .IsFailed = false, .Error = {}});
    }
    GUI::ResultStore results;
    results.SetMonteCarlo(table);

    const std::string text = GUI::BuildAICopyText(schematic, results, ENGINE);

    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("Monte Carlo: "));
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("3 runs of 3 asked for after the nominal one, 0 failed"));
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("| Quantity | Nominal | Mean | Std deviation | Min | Max |"));
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("| V(2) Mean | 2.500V |"));
}

TEST_CASE("A stepped run says which step the figures are of", "[ai_copy]") {
    GUI::Schematic schematic;
    OpenRCFilter(schematic);
    Select(schematic, GUI::Analysis::TRANSIENT);
    GUI::ResultStore results;
    GUI::StepFamily<Core::Transient> family{.Parameter = "R1",
                                            .Unit = "Ohm",
                                            .Values = {1e3, 2e3},
                                            .Results = {MakeTransient(10), MakeTransient(10)},
                                            .Selected = 1};
    results.SetTransient(MakeTransient(10));
    results.SetTransientSteps(family);

    const std::string text = GUI::BuildAICopyText(schematic, results, ENGINE);

    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("Stepped run: 2 steps of R1"));
}

TEST_CASE("The settings of the selected analysis are written, with the solver options that are not the default",
          "[ai_copy]") {
    GUI::Schematic schematic;
    OpenRCFilter(schematic);
    GUI::SimulationSettings settings = schematic.GetSimulationSettings();
    settings.Selected = GUI::Analysis::NOISE;
    settings.NoiseSource.clear();
    settings.Solver.Method = Core::IntegrationMethod::GEAR;
    schematic.SetSimulationSettings(settings);
    const GUI::ResultStore results;

    const std::string text = GUI::BuildAICopyText(schematic, results, ENGINE);

    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("## Analysis: Noise"));
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("- Input source: chosen automatically"));
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("integration Gear"));
}
