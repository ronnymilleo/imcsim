/**
 * @file    s_parameters_tests.cpp
 * @brief   Tests of the S-parameter analysis: the netlist lines, the quantities read from a reflection, the checks
 *          before a run, and the run on ngspice against closed-form scattering matrices.
 */

#include "simulation/s_parameters.h"

#include "circuit/circuit.h"
#include "circuit/components/capacitor.h"
#include "circuit/components/ground.h"
#include "circuit/components/inductor.h"
#include "circuit/components/resistor.h"
#include "circuit/components/voltage_source.h"
#include "simulation/circuit_checks.h"
#include "simulation/circuit_copy.h"
#include "simulation/simulation_backend.h"
#include "simulation/simulator.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <complex>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

namespace {

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using Complex = std::complex<double>;

constexpr double TWO_PI = 2.0 * std::numbers::pi;

/// A resistor between two nodes, with the ground part the checks ask for.
struct SeriesResistor {
    Core::Resistor Series;
    Core::Ground Reference;
    Core::Circuit Circuit;

    explicit SeriesResistor(const double ohms) {
        Series.SetName("R1");
        Series.SetValue(ohms);
        Circuit.Add(Series, {1, 2});
        Circuit.Add(Reference, {0});
    }
};

/// A T low-pass of two inductors and a capacitor to ground, with ports on both ends.
struct LowPassT {
    Core::Inductor First;
    Core::Inductor Second;
    Core::Capacitor Shunt;
    Core::Ground Reference;
    Core::Circuit Circuit;

    LowPassT() {
        First.SetName("L1");
        First.SetValue(8e-6);
        Second.SetName("L2");
        Second.SetValue(8e-6);
        Shunt.SetName("C1");
        Shunt.SetValue(3.18e-9);
        Circuit.Add(First, {1, 3});
        Circuit.Add(Second, {3, 2});
        Circuit.Add(Shunt, {3, 0});
        Circuit.Add(Reference, {0});
    }
};

Core::SParameterSettings SinglePoint(const double frequency) {
    return {.StartFrequency = frequency,
            .StopFrequency = frequency * 2.0,
            .IsLogarithmic = false,
            .PointsPerDecade = 1,
            .LinearPoints = 3};
}

void RequireClose(const Complex actual, const Complex expected, const double tolerance = 1e-6) {
    CHECK_THAT(actual.real(), WithinAbs(expected.real(), tolerance));
    CHECK_THAT(actual.imag(), WithinAbs(expected.imag(), tolerance));
}

/// S11 and S21 of the T low-pass from its ABCD matrix, which ngspice must agree with.
std::pair<Complex, Complex> LowPassTClosedForm(const double frequency, const double reference) {
    const Complex series = Complex(0.0, TWO_PI * frequency * 8e-6);
    const Complex shunt = Complex(0.0, TWO_PI * frequency * 3.18e-9);
    // ABCD of series Z, shunt Y, series Z multiplied out
    const Complex a = 1.0 + series * shunt;
    const Complex b = 2.0 * series + series * series * shunt;
    const Complex c = shunt;
    const Complex d = 1.0 + series * shunt;
    const Complex denominator = a + b / reference + c * reference + d;
    return {(a + b / reference - c * reference - d) / denominator, 2.0 / denominator};
}

} // namespace

TEST_CASE("Each port is a source of its number and reference impedance on its node", "[s_parameters]") {
    const std::vector<Core::CircuitPort> ports = {{.Number = 1, .Node = 3, .ReferenceOhms = 50.0},
                                                  {.Number = 2, .Node = 4, .ReferenceOhms = 75.0}};
    CHECK(Core::FormatPortSources(ports) == "Vsport1 3 0 dc 0 portnum 1 z0 50\nVsport2 4 0 dc 0 portnum 2 z0 75\n");
    CHECK(Core::FormatPortSources({}).empty());
}

TEST_CASE("The S-parameter card follows the sweep type", "[s_parameters]") {
    CHECK(Core::FormatSParameterAnalysis({.StartFrequency = 1e6, .StopFrequency = 1e9, .PointsPerDecade = 20}) ==
          ".sp dec 20 1Meg 1G");
    CHECK(Core::FormatSParameterAnalysis(
              {.StartFrequency = 1e3, .StopFrequency = 1e6, .IsLogarithmic = false, .LinearPoints = 101}) ==
          ".sp lin 101 1k 1Meg");
}

TEST_CASE("A reflection tells the impedance, the standing wave ratio and the return loss", "[s_parameters]") {
    // A matched port sees its reference; a short sees nothing; a reactance reflects with the angle of its phase
    RequireClose(*Core::ToImpedance(0.0, 50.0), 50.0);
    RequireClose(*Core::ToImpedance(-1.0, 50.0), 0.0);
    RequireClose(*Core::ToImpedance(Complex(1.0 / 3.0, 0.0), 50.0), 100.0);
    // z = 1 + j has the reflection (z - 1) / (z + 1) = j / (2 + j)
    RequireClose(*Core::ToImpedance(Complex(0.0, 1.0) / Complex(2.0, 1.0), 50.0), Complex(50.0, 50.0));
    CHECK_FALSE(Core::ToImpedance(1.0, 50.0).has_value());

    CHECK_THAT(*Core::ComputeVswr(0.5), WithinAbs(3.0, 1e-12));
    CHECK_THAT(*Core::ComputeVswr(0.0), WithinAbs(1.0, 1e-12));
    CHECK_FALSE(Core::ComputeVswr(1.0).has_value());
    CHECK_FALSE(Core::ComputeVswr(Complex(0.0, 1.5)).has_value());

    CHECK_THAT(*Core::ComputeReturnLossDecibels(0.1), WithinAbs(20.0, 1e-9));
    CHECK_FALSE(Core::ComputeReturnLossDecibels(0.0).has_value());
}

TEST_CASE("Decibels and degrees of a value never reach minus infinity", "[s_parameters]") {
    CHECK_THAT(Core::ToDecibels(0.5), WithinAbs(-6.0206, 1e-4));
    CHECK_THAT(Core::ToDecibels(0.0), WithinAbs(-300.0, 1e-9));
    CHECK_THAT(Core::ToDegrees(Complex(0.0, 1.0)), WithinAbs(90.0, 1e-9));
    CHECK_THAT(Core::ToDegrees(Complex(-1.0, -1e-12)), WithinAbs(-180.0, 1e-6));
}

TEST_CASE("A matrix is read frequency by frequency and row by row", "[s_parameters]") {
    Core::SParameters result;
    result.Frequencies = {1e6, 2e6};
    result.ReferenceOhms = {50.0, 50.0};
    result.Values = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0};
    CHECK(result.GetPortCount() == 2);
    CHECK(result.Get(0, 1, 1) == Complex(1.0));
    CHECK(result.Get(0, 1, 2) == Complex(2.0));
    CHECK(result.Get(0, 2, 1) == Complex(3.0));
    CHECK(result.Get(1, 1, 1) == Complex(5.0));
    CHECK(result.Get(1, 2, 2) == Complex(8.0));
}

TEST_CASE("The first fatal error ngspice printed explains why it gave no matrix", "[s_parameters]") {
    const std::optional<std::string> reason = Core::DescribeSParameterFailure(
        {{"Doing analysis at TEMP = 27.000000", false}, {"Fatal error: v2: incorrect port ordering", true}});
    REQUIRE(reason.has_value());
    CHECK(reason->contains("incorrect port ordering"));
    CHECK_FALSE(Core::DescribeSParameterFailure({{"Doing analysis at TEMP = 27.000000", false}}).has_value());
    CHECK_FALSE(Core::DescribeSParameterFailure({}).has_value());
}

TEST_CASE("The ports reach the circuit, its copies and its node count, but never its netlist", "[s_parameters]") {
    SeriesResistor line(100.0);
    line.Circuit.AddPort({.Number = 1, .Node = 1, .ReferenceOhms = 50.0});
    line.Circuit.AddPort({.Number = 2, .Node = 6, .ReferenceOhms = 75.0});
    CHECK(line.Circuit.GetNodeCount() == 7);
    CHECK_FALSE(line.Circuit.ToSpiceNetlist().contains("portnum"));
    const Core::CircuitCopy copy(line.Circuit);
    REQUIRE(copy.GetCircuit().GetPorts().size() == 2);
    CHECK(copy.GetCircuit().GetPorts()[1].Number == 2);
    CHECK(copy.GetCircuit().GetPorts()[1].Node == 6);
    CHECK(copy.GetCircuit().GetPorts()[1].ReferenceOhms == 75.0);
}

TEST_CASE("The analysis is refused with fewer than two ports before ngspice sees it", "[s_parameters][simulator]") {
    Core::SetBackend(nullptr);
    SeriesResistor line(100.0);
    const Core::SParameterSettings settings;
    const std::optional<std::string> none = Core::CheckSParameterSettings(line.Circuit, settings);
    REQUIRE(none.has_value());
    CHECK(none->contains("ngspice needs at least two S-parameter ports"));
    line.Circuit.AddPort({.Number = 1, .Node = 1, .ReferenceOhms = 50.0});
    const std::optional<std::string> one = Core::CheckSParameterSettings(line.Circuit, settings);
    REQUIRE(one.has_value());
    CHECK(one->contains("ngspice needs at least two S-parameter ports"));

    // ngspice stops for good when it finds a single port, so a run that reached it would end every later one
    const Core::SParametersRun run = Core::RunSParameters(line.Circuit, settings);
    REQUIRE_FALSE(run.Result.has_value());
    CHECK(run.Result.error().contains("at least two"));
    CHECK(run.Messages.empty());
    Core::VoltageSource source;
    Core::Resistor load;
    Core::Ground reference;
    source.SetName("V1");
    load.SetName("R1");
    load.SetValue(1e3);
    Core::Circuit loop;
    loop.Add(source, {1, 0});
    loop.Add(load, {1, 0});
    loop.Add(reference, {0});
    CHECK(Core::RunOperatingPoint(loop).Result.has_value());
}

TEST_CASE("Ports must be numbered 1 to N, have a reference above zero and sit on nodes of their own",
          "[s_parameters]") {
    const Core::SParameterSettings settings;
    SeriesResistor line(100.0);
    line.Circuit.AddPort({.Number = 1, .Node = 1, .ReferenceOhms = 50.0});
    line.Circuit.AddPort({.Number = 2, .Node = 2, .ReferenceOhms = 50.0});
    CHECK_FALSE(Core::CheckSParameterSettings(line.Circuit, settings).has_value());

    SeriesResistor gap(100.0);
    gap.Circuit.AddPort({.Number = 1, .Node = 1, .ReferenceOhms = 50.0});
    gap.Circuit.AddPort({.Number = 3, .Node = 2, .ReferenceOhms = 50.0});
    const std::optional<std::string> gap_reason = Core::CheckSParameterSettings(gap.Circuit, settings);
    REQUIRE(gap_reason.has_value());
    CHECK(gap_reason->contains("port 2 is missing"));

    SeriesResistor repeat(100.0);
    repeat.Circuit.AddPort({.Number = 1, .Node = 1, .ReferenceOhms = 50.0});
    repeat.Circuit.AddPort({.Number = 1, .Node = 2, .ReferenceOhms = 50.0});
    const std::optional<std::string> repeat_reason = Core::CheckSParameterSettings(repeat.Circuit, settings);
    REQUIRE(repeat_reason.has_value());
    CHECK(repeat_reason->contains("numbered 1"));

    SeriesResistor impedance(100.0);
    impedance.Circuit.AddPort({.Number = 1, .Node = 1, .ReferenceOhms = 0.0});
    impedance.Circuit.AddPort({.Number = 2, .Node = 2, .ReferenceOhms = 50.0});
    const std::optional<std::string> impedance_reason = Core::CheckSParameterSettings(impedance.Circuit, settings);
    REQUIRE(impedance_reason.has_value());
    CHECK(impedance_reason->contains("reference impedance of port 1"));

    SeriesResistor ground(100.0);
    ground.Circuit.AddPort({.Number = 1, .Node = 0, .ReferenceOhms = 50.0});
    ground.Circuit.AddPort({.Number = 2, .Node = 2, .ReferenceOhms = 50.0});
    const std::optional<std::string> ground_reason = Core::CheckSParameterSettings(ground.Circuit, settings);
    REQUIRE(ground_reason.has_value());
    CHECK(ground_reason->contains("on ground"));

    // ngspice takes two ports on a node without a word and returns meaningless numbers
    SeriesResistor shared(100.0);
    shared.Circuit.AddPort({.Number = 1, .Node = 2, .ReferenceOhms = 50.0});
    shared.Circuit.AddPort({.Number = 2, .Node = 2, .ReferenceOhms = 50.0});
    const std::optional<std::string> shared_reason = Core::CheckSParameterSettings(shared.Circuit, settings);
    REQUIRE(shared_reason.has_value());
    CHECK(shared_reason->contains("same node"));
}

TEST_CASE("The frequency range of the analysis is checked, as ngspice says nothing about some of its mistakes",
          "[s_parameters]") {
    SeriesResistor line(100.0);
    line.Circuit.AddPort({.Number = 1, .Node = 1, .ReferenceOhms = 50.0});
    line.Circuit.AddPort({.Number = 2, .Node = 2, .ReferenceOhms = 50.0});
    CHECK_FALSE(Core::CheckSParameterSettings(line.Circuit, {}).has_value());
    CHECK(Core::CheckSParameterSettings(line.Circuit, {.StartFrequency = 0.0}).has_value());
    CHECK(Core::CheckSParameterSettings(line.Circuit, {.StartFrequency = 1e9, .StopFrequency = 1e6}).has_value());
    CHECK(Core::CheckSParameterSettings(line.Circuit, {.StartFrequency = 1e6, .StopFrequency = 1e6}).has_value());
    CHECK(Core::CheckSParameterSettings(line.Circuit, {.PointsPerDecade = 0}).has_value());
    CHECK(Core::CheckSParameterSettings(line.Circuit, {.IsLogarithmic = false, .LinearPoints = 2}).has_value());
    CHECK_FALSE(Core::CheckSParameterSettings(line.Circuit, {.IsLogarithmic = false, .LinearPoints = 3}).has_value());
}

TEST_CASE("ngspice does not simulate logic parts in an S-parameter analysis, and the analysis is refused",
          "[s_parameters][simulator]") {
    Core::SetBackend(nullptr);
    const std::optional<std::string> reason =
        Core::FindUnsupported(Core::GetBackend(), {Core::ComponentType::AND}, Core::AnalysisKind::S_PARAMETERS);
    REQUIRE(reason.has_value());
    CHECK(reason->contains("digital"));
    CHECK_FALSE(Core::FindUnsupported(Core::GetBackend(), {Core::ComponentType::RESISTOR, Core::ComponentType::NPN},
                                      Core::AnalysisKind::S_PARAMETERS)
                    .has_value());
    CHECK(Core::GetAnalysisKindName(Core::AnalysisKind::S_PARAMETERS) == "S-parameter");
}

TEST_CASE("A series resistor between two matched ports has S11 and S21 of one half", "[s_parameters][simulator]") {
    Core::SetBackend(nullptr);
    SeriesResistor line(100.0);
    line.Circuit.AddPort({.Number = 1, .Node = 1, .ReferenceOhms = 50.0});
    line.Circuit.AddPort({.Number = 2, .Node = 2, .ReferenceOhms = 50.0});
    const Core::SParametersRun run = Core::RunSParameters(line.Circuit, SinglePoint(1e6));
    REQUIRE(run.Result.has_value());
    REQUIRE(run.Result->Frequencies.size() == 3);
    CHECK(run.Result->GetPortCount() == 2);
    // R / (R + 2 Z0) for the reflection and 2 Z0 / (R + 2 Z0) for the transmission
    for (std::size_t frequency = 0; frequency < 3; ++frequency) {
        RequireClose(run.Result->Get(frequency, 1, 1), 0.5);
        RequireClose(run.Result->Get(frequency, 2, 1), 0.5);
        RequireClose(run.Result->Get(frequency, 1, 2), 0.5);
        RequireClose(run.Result->Get(frequency, 2, 2), 0.5);
    }
}

TEST_CASE("Ports of different references use power waves, whatever order they were added in",
          "[s_parameters][simulator]") {
    Core::SetBackend(nullptr);
    SeriesResistor line(100.0);
    // Added out of order, to check that the matrix follows the port numbers
    line.Circuit.AddPort({.Number = 2, .Node = 2, .ReferenceOhms = 75.0});
    line.Circuit.AddPort({.Number = 1, .Node = 1, .ReferenceOhms = 50.0});
    const Core::SParametersRun run = Core::RunSParameters(line.Circuit, SinglePoint(1e6));
    REQUIRE(run.Result.has_value());
    CHECK(run.Result->ReferenceOhms == std::vector<double>{50.0, 75.0});
    // Looking into port 1 the resistor and port 2 make 175 ohm against 50; into port 2, 150 ohm against 75
    RequireClose(run.Result->Get(0, 1, 1), 125.0 / 225.0);
    RequireClose(run.Result->Get(0, 2, 2), 75.0 / 225.0);
    RequireClose(run.Result->Get(0, 2, 1), 2.0 * std::sqrt(50.0 * 75.0) / 225.0);
}

TEST_CASE("A low-pass of two inductors and a capacitor matches its ABCD matrix across the band",
          "[s_parameters][simulator]") {
    Core::SetBackend(nullptr);
    LowPassT filter;
    filter.Circuit.AddPort({.Number = 1, .Node = 1, .ReferenceOhms = 50.0});
    filter.Circuit.AddPort({.Number = 2, .Node = 2, .ReferenceOhms = 50.0});
    const Core::SParametersRun run =
        Core::RunSParameters(filter.Circuit, {.StartFrequency = 100e3, .StopFrequency = 10e6, .PointsPerDecade = 3});
    REQUIRE(run.Result.has_value());
    REQUIRE(run.Result->Frequencies.size() == 7);
    for (std::size_t index = 0; index < run.Result->Frequencies.size(); ++index) {
        const auto [reflection, transmission] = LowPassTClosedForm(run.Result->Frequencies[index], 50.0);
        RequireClose(run.Result->Get(index, 1, 1), reflection);
        RequireClose(run.Result->Get(index, 2, 2), reflection);
        RequireClose(run.Result->Get(index, 2, 1), transmission);
        RequireClose(run.Result->Get(index, 1, 2), transmission);
    }
}

TEST_CASE("Three ports give a three by three matrix", "[s_parameters][simulator]") {
    Core::SetBackend(nullptr);
    Core::Resistor first;
    Core::Resistor second;
    Core::Resistor third;
    first.SetName("R1");
    second.SetName("R2");
    third.SetName("R3");
    for (Core::Resistor *leg : {&first, &second, &third}) {
        leg->SetValue(50.0);
    }
    Core::Ground reference;
    Core::Circuit circuit;
    circuit.Add(first, {1, 4});
    circuit.Add(second, {2, 4});
    circuit.Add(third, {3, 4});
    circuit.Add(reference, {0});
    for (int port = 1; port <= 3; ++port) {
        circuit.AddPort({.Number = port, .Node = port, .ReferenceOhms = 50.0});
    }
    const Core::SParametersRun run = Core::RunSParameters(circuit, SinglePoint(1e6));
    REQUIRE(run.Result.has_value());
    CHECK(run.Result->GetPortCount() == 3);
    // Into one port the star shows its leg and the other two legs in parallel with their ports: 50 + 50 = 100 ohm
    RequireClose(run.Result->Get(0, 1, 1), 1.0 / 3.0);
    RequireClose(run.Result->Get(0, 3, 2), 1.0 / 3.0);
}

TEST_CASE("The ports are invisible to the other analyses", "[s_parameters][simulator]") {
    Core::SetBackend(nullptr);
    SeriesResistor line(100.0);
    line.Circuit.AddPort({.Number = 1, .Node = 1, .ReferenceOhms = 50.0});
    line.Circuit.AddPort({.Number = 2, .Node = 2, .ReferenceOhms = 50.0});
    // With the port sources in the netlist, node 1 would be held at 0 V through 50 ohm; without them nothing drives it
    const std::string netlist = line.Circuit.ToSpiceNetlist(".op");
    CHECK_FALSE(netlist.contains("Vsport"));
    CHECK_FALSE(netlist.contains("z0"));
}
