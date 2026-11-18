/**
 * @file    s_parameter_ports_tests.cpp
 * @brief   Tests of what each backend asks of the ports and the sweep of an S-parameter analysis, the one-port run on
 *          Nodalys, the impedance and admittance matrices it gives, and the wording of the causes shared with the
 *          engine.
 */

#include "backends/nodalys/nodalys_backend.h"
#include "circuit/circuit.h"
#include "circuit/components/capacitor.h"
#include "circuit/components/ground.h"
#include "circuit/components/resistor.h"
#include "simulation/check_s_parameters.h"
#include "simulation/simulation_backend.h"
#include "simulation/simulator.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <complex>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace {

using Catch::Matchers::WithinAbs;
using Complex = std::complex<double>;

constexpr double TWO_PI = 2.0 * std::numbers::pi;

/// A resistor from node 1 to ground, or between nodes 1 and 2 when two ports are wanted.
struct Resistive {
    Core::Resistor Load;
    Core::Ground Reference;
    Core::Circuit Circuit;

    Resistive(const double ohms, const int far_node) {
        Load.SetName("R1");
        Load.SetValue(ohms);
        Circuit.Add(Load, {1, far_node});
        Circuit.Add(Reference, {0});
    }
};

/// A resistor in series between two nodes with ports on both, and nothing else: Z does not exist.
Resistive MakeSeries(const double ohms) {
    return Resistive(ohms, 2);
}

/// A capacitor from node 1 to ground, a one-port with a closed-form impedance.
struct CapacitorToGround {
    Core::Capacitor Shunt;
    Core::Ground Reference;
    Core::Circuit Circuit;

    explicit CapacitorToGround(const double farads) {
        Shunt.SetName("C1");
        Shunt.SetValue(farads);
        Circuit.Add(Shunt, {1, 0});
        Circuit.Add(Reference, {0});
    }
};

/// The checks as the Nodalys backend asks for them.
std::optional<std::string> CheckOnNodalys(const Core::Circuit &circuit, const Core::SParameterSettings &settings) {
    const Core::NodalysBackend nodalys;
    return Core::CheckSParameterSettings(circuit, settings, nodalys.GetCapabilities().SParameters);
}

/// The checks as ngspice asks for them.
std::optional<std::string> CheckOnNgspice(const Core::Circuit &circuit, const Core::SParameterSettings &settings) {
    Core::SetBackend(nullptr);
    return Core::CheckSParameterSettings(circuit, settings, Core::GetBackend().GetCapabilities().SParameters);
}

/// Runs the analysis the way the editor does, through the simulator, on Nodalys, and puts ngspice back after.
Core::SParametersRun RunOnNodalys(const Core::Circuit &circuit, const Core::SParameterSettings &settings) {
    Core::SetBackend(std::make_unique<Core::NodalysBackend>());
    Core::SParametersRun run = Core::RunSParameters(circuit, settings);
    Core::SetBackend(nullptr);
    return run;
}

/// What the engine itself says about a circuit, past the checks of imcsim.
std::string EngineError(const Core::Circuit &circuit, const Core::SParameterSettings &settings) {
    Core::NodalysBackend nodalys;
    const Core::SParametersRun run = nodalys.RunSParameters(circuit, settings);
    REQUIRE_FALSE(run.Result.has_value());
    return run.Result.error();
}

void RequireClose(const Complex actual, const Complex expected, const double tolerance = 1e-6) {
    CHECK_THAT(actual.real(), WithinAbs(expected.real(), tolerance));
    CHECK_THAT(actual.imag(), WithinAbs(expected.imag(), tolerance));
}

} // namespace

TEST_CASE("Each backend declares what it asks of the ports and the sweep, with the reason", "[s_parameters][ports]") {
    Core::SetBackend(nullptr);
    const Core::SParameterLimits &ngspice = Core::GetBackend().GetCapabilities().SParameters;
    CHECK(ngspice.MinPorts == 2);
    CHECK(ngspice.MinLinearPoints == 3);
    CHECK(ngspice.MinPortsReason.contains("ngspice needs at least two"));
    CHECK(ngspice.MinPortsReason.contains("Nodalys takes one"));
    CHECK(ngspice.MinLinearPointsReason.contains("ngspice gives only the first point"));

    const Core::NodalysBackend nodalys;
    const Core::SParameterLimits &limits = nodalys.GetCapabilities().SParameters;
    CHECK(limits.MinPorts == 1);
    CHECK(limits.MinLinearPoints == 2);
    CHECK(limits.MinPortsReason.empty());
}

TEST_CASE("A backend with no limits of its own accepts one port and two points", "[s_parameters][ports]") {
    const Core::SParameterLimits none;
    Resistive load(100.0, 0);
    load.Circuit.AddPort({.Number = 1, .Node = 1, .ReferenceOhms = 50.0});
    CHECK_FALSE(
        Core::CheckSParameterSettings(load.Circuit, {.IsLogarithmic = false, .LinearPoints = 2}, none).has_value());

    // A limit without a reason of its own is still worded, with the number it asks for
    const Core::SParameterLimits three{.MinPorts = 3};
    const std::optional<std::string> reason = Core::CheckSParameterSettings(load.Circuit, {}, three);
    REQUIRE(reason.has_value());
    CHECK(reason->contains("at least 3 S-parameter ports"));
}

TEST_CASE("One port is a circuit for Nodalys and a refusal for ngspice, which says Nodalys takes one",
          "[s_parameters][ports]") {
    Resistive load(100.0, 0);
    load.Circuit.AddPort({.Number = 1, .Node = 1, .ReferenceOhms = 50.0});
    CHECK_FALSE(CheckOnNodalys(load.Circuit, {}).has_value());
    const std::optional<std::string> ngspice = CheckOnNgspice(load.Circuit, {});
    REQUIRE(ngspice.has_value());
    CHECK(ngspice->contains("ngspice needs at least two S-parameter ports, and Nodalys takes one"));

    // No port at all is wrong for both, and says so without naming an engine
    Resistive bare(100.0, 0);
    for (const std::optional<std::string> &reason :
         {CheckOnNodalys(bare.Circuit, {}), CheckOnNgspice(bare.Circuit, {})}) {
        REQUIRE(reason.has_value());
        CHECK(reason->starts_with("The circuit has no S-parameter port"));
    }
}

TEST_CASE("A linear sweep of two points runs on Nodalys, which gives both, and is refused on ngspice",
          "[s_parameters][ports]") {
    Resistive load = MakeSeries(100.0);
    load.Circuit.AddPort({.Number = 1, .Node = 1, .ReferenceOhms = 50.0});
    load.Circuit.AddPort({.Number = 2, .Node = 2, .ReferenceOhms = 50.0});
    const Core::SParameterSettings two_points{.IsLogarithmic = false, .LinearPoints = 2};
    CHECK_FALSE(CheckOnNodalys(load.Circuit, two_points).has_value());
    const std::optional<std::string> ngspice = CheckOnNgspice(load.Circuit, two_points);
    REQUIRE(ngspice.has_value());
    CHECK(ngspice->contains("between 3 and"));
    CHECK(ngspice->contains("ngspice gives only the first point for a linear sweep of two points, and Nodalys "
                            "gives both"));

    // A single point is no sweep for any of them
    const Core::SParameterSettings one_point{.IsLogarithmic = false, .LinearPoints = 1};
    CHECK(CheckOnNodalys(load.Circuit, one_point).value_or("").contains("between 2 and"));
    CHECK(CheckOnNgspice(load.Circuit, one_point).has_value());

    const Core::SParametersRun run =
        RunOnNodalys(load.Circuit,
                     {.StartFrequency = 1e6, .StopFrequency = 2e6, .IsLogarithmic = false, .LinearPoints = 2});
    REQUIRE(run.Result.has_value());
    CHECK(run.Result->Frequencies == std::vector<double>{1e6, 2e6});
}

TEST_CASE("Ports numbered with a gap are refused on every backend, and ports listed out of order are not",
          "[s_parameters][ports]") {
    // The matrix and the Touchstone file index the ports by number, so a gap would leave a row with no port
    Resistive gap = MakeSeries(100.0);
    gap.Circuit.AddPort({.Number = 1, .Node = 1, .ReferenceOhms = 50.0});
    gap.Circuit.AddPort({.Number = 3, .Node = 2, .ReferenceOhms = 50.0});
    CHECK(CheckOnNodalys(gap.Circuit, {}).value_or("").contains("port 2 is missing"));
    CHECK(CheckOnNgspice(gap.Circuit, {}).value_or("").contains("port 2 is missing"));

    Resistive shuffled = MakeSeries(100.0);
    shuffled.Circuit.AddPort({.Number = 3, .Node = 3, .ReferenceOhms = 50.0});
    shuffled.Circuit.AddPort({.Number = 1, .Node = 1, .ReferenceOhms = 50.0});
    shuffled.Circuit.AddPort({.Number = 2, .Node = 2, .ReferenceOhms = 50.0});
    CHECK_FALSE(CheckOnNodalys(shuffled.Circuit, {}).has_value());
    CHECK_FALSE(CheckOnNgspice(shuffled.Circuit, {}).has_value());
}

TEST_CASE("The engine words each cause as the checks before it do", "[s_parameters][ports]") {
    const Core::SParameterSettings settings{.StartFrequency = 1e6, .StopFrequency = 1e9};
    const auto engine_and_check = [&settings](Resistive &circuit) {
        return std::pair{EngineError(circuit.Circuit, settings), CheckOnNodalys(circuit.Circuit, settings)};
    };

    Resistive zero_reference = MakeSeries(100.0);
    zero_reference.Circuit.AddPort({.Number = 1, .Node = 1, .ReferenceOhms = 0.0});
    zero_reference.Circuit.AddPort({.Number = 2, .Node = 2, .ReferenceOhms = 50.0});
    auto [engine, check] = engine_and_check(zero_reference);
    CHECK(engine == "The reference impedance of port 1 must be positive");
    CHECK(engine == check);

    Resistive ground = MakeSeries(100.0);
    ground.Circuit.AddPort({.Number = 1, .Node = 0, .ReferenceOhms = 50.0});
    ground.Circuit.AddPort({.Number = 2, .Node = 2, .ReferenceOhms = 50.0});
    std::tie(engine, check) = engine_and_check(ground);
    CHECK(engine == "Port 1 is on ground; an S-parameter port needs a node of its own");
    CHECK(engine == check);

    Resistive shared = MakeSeries(100.0);
    shared.Circuit.AddPort({.Number = 1, .Node = 2, .ReferenceOhms = 50.0});
    shared.Circuit.AddPort({.Number = 2, .Node = 2, .ReferenceOhms = 50.0});
    std::tie(engine, check) = engine_and_check(shared);
    CHECK(engine == "Ports 1 and 2 are on the same node; each port needs a node of its own");
    CHECK(engine == check);

    Resistive repeat = MakeSeries(100.0);
    repeat.Circuit.AddPort({.Number = 1, .Node = 1, .ReferenceOhms = 50.0});
    repeat.Circuit.AddPort({.Number = 1, .Node = 2, .ReferenceOhms = 50.0});
    std::tie(engine, check) = engine_and_check(repeat);
    CHECK(engine == "Two S-parameter ports are numbered 1; give each port its own number");
    CHECK(engine == check);

    Resistive gap = MakeSeries(100.0);
    gap.Circuit.AddPort({.Number = 1, .Node = 1, .ReferenceOhms = 50.0});
    gap.Circuit.AddPort({.Number = 3, .Node = 2, .ReferenceOhms = 50.0});
    std::tie(engine, check) = engine_and_check(gap);
    CHECK(engine == "The S-parameter ports must be numbered 1 to 2 with no gap; port 2 is missing");
    CHECK(engine == check);

    Resistive none = MakeSeries(100.0);
    std::tie(engine, check) = engine_and_check(none);
    CHECK(engine == "The circuit has no S-parameter port");
    REQUIRE(check.has_value());
    CHECK(check->starts_with(engine));
}

TEST_CASE("The engine words a bad sweep as the checks before it do", "[s_parameters][ports]") {
    Resistive load = MakeSeries(100.0);
    load.Circuit.AddPort({.Number = 1, .Node = 1, .ReferenceOhms = 50.0});
    load.Circuit.AddPort({.Number = 2, .Node = 2, .ReferenceOhms = 50.0});
    for (const Core::SParameterSettings &settings :
         {Core::SParameterSettings{.StartFrequency = 1e9, .StopFrequency = 1e6},
          Core::SParameterSettings{.StartFrequency = 0.0, .StopFrequency = 1e6},
          Core::SParameterSettings{.StartFrequency = -5.0, .StopFrequency = 1e6}}) {
        const std::optional<std::string> check = CheckOnNodalys(load.Circuit, settings);
        REQUIRE(check.has_value());
        CHECK(EngineError(load.Circuit, settings) == *check);
    }
}

TEST_CASE("One port on Nodalys gives S11 of the load, and the impedance and admittance of it",
          "[s_parameters][ports]") {
    // 100 ohm against 50: (100 - 50) / (100 + 50) = 1/3
    Resistive load(100.0, 0);
    load.Circuit.AddPort({.Number = 1, .Node = 1, .ReferenceOhms = 50.0});
    const Core::SParametersRun run =
        RunOnNodalys(load.Circuit, {.StartFrequency = 1e6, .StopFrequency = 1e7, .PointsPerDecade = 2});
    REQUIRE(run.Result.has_value());
    CHECK(run.Result->GetPortCount() == 1);
    REQUIRE(run.Result->Frequencies.size() == 3);
    CHECK(run.Result->HasPortMatrices());
    for (std::size_t index = 0; index < run.Result->Frequencies.size(); ++index) {
        RequireClose(run.Result->Get(index, 1, 1), 1.0 / 3.0);
        RequireClose(*run.Result->GetImpedance(index, 1, 1), 100.0, 1e-4);
        RequireClose(*run.Result->GetAdmittance(index, 1, 1), 0.01, 1e-9);
    }
}

TEST_CASE("One port on a capacitor reads as 1 / (j w C) and j w C", "[s_parameters][ports]") {
    constexpr double FARADS = 1e-9;
    CapacitorToGround capacitor(FARADS);
    capacitor.Circuit.AddPort({.Number = 1, .Node = 1, .ReferenceOhms = 50.0});
    const Core::SParametersRun run =
        RunOnNodalys(capacitor.Circuit, {.StartFrequency = 1e6, .StopFrequency = 1e7, .PointsPerDecade = 2});
    REQUIRE(run.Result.has_value());
    for (std::size_t index = 0; index < run.Result->Frequencies.size(); ++index) {
        const double omega = TWO_PI * run.Result->Frequencies[index];
        const Complex admittance(0.0, omega * FARADS);
        const Complex impedance = 1.0 / admittance;
        RequireClose(*run.Result->GetImpedance(index, 1, 1), impedance, 1e-3);
        RequireClose(*run.Result->GetAdmittance(index, 1, 1), admittance, 1e-9);
        // The reflection is the one the impedance gives against the reference
        RequireClose(run.Result->Get(index, 1, 1), (impedance - 50.0) / (impedance + 50.0));
    }
}

TEST_CASE("The matrices are absent where the circuit has none, and the S matrix is still there",
          "[s_parameters][ports]") {
    // Two ports joined by a series resistor and nothing else: they float, so there is no finite Z, but Y exists
    Resistive series = MakeSeries(100.0);
    series.Circuit.AddPort({.Number = 1, .Node = 1, .ReferenceOhms = 50.0});
    series.Circuit.AddPort({.Number = 2, .Node = 2, .ReferenceOhms = 50.0});
    const Core::SParametersRun run =
        RunOnNodalys(series.Circuit, {.StartFrequency = 1e6, .StopFrequency = 1e7, .PointsPerDecade = 2});
    REQUIRE(run.Result.has_value());
    REQUIRE(run.Result->HasPortMatrices());
    for (std::size_t index = 0; index < run.Result->Frequencies.size(); ++index) {
        RequireClose(run.Result->Get(index, 2, 1), 0.5);
        CHECK_FALSE(run.Result->GetImpedance(index, 1, 1).has_value());
        // Y = (1 / R) [[1, -1], [-1, 1]]
        RequireClose(*run.Result->GetAdmittance(index, 1, 1), 0.01, 1e-9);
        RequireClose(*run.Result->GetAdmittance(index, 2, 1), -0.01, 1e-9);
    }
}

TEST_CASE("A result without matrices reads as having no impedance or admittance", "[s_parameters][ports]") {
    Core::SParameters result;
    result.Frequencies = {1e6};
    result.ReferenceOhms = {50.0};
    result.Values = {0.0};
    CHECK_FALSE(result.HasPortMatrices());
    CHECK_FALSE(result.GetImpedance(0, 1, 1).has_value());
    CHECK_FALSE(result.GetAdmittance(0, 1, 1).has_value());

    result.ImpedanceMatrices = {std::nullopt};
    result.AdmittanceMatrices = {std::vector<Complex>{Complex(0.02, 0.0)}};
    CHECK(result.HasPortMatrices());
    CHECK_FALSE(result.GetImpedance(0, 1, 1).has_value());
    CHECK(result.GetAdmittance(0, 1, 1) == Complex(0.02, 0.0));
    CHECK_FALSE(result.GetAdmittance(5, 1, 1).has_value());
}

TEST_CASE("ngspice gives no impedance or admittance matrices", "[s_parameters][ports]") {
    Core::SetBackend(nullptr);
    Resistive series = MakeSeries(100.0);
    series.Circuit.AddPort({.Number = 1, .Node = 1, .ReferenceOhms = 50.0});
    series.Circuit.AddPort({.Number = 2, .Node = 2, .ReferenceOhms = 50.0});
    const Core::SParametersRun run = Core::RunSParameters(series.Circuit, {});
    REQUIRE(run.Result.has_value());
    CHECK_FALSE(run.Result->HasPortMatrices());
    CHECK(run.Result->ImpedanceMatrices.empty());
    CHECK(run.Result->AdmittanceMatrices.empty());
}
