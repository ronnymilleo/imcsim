/**
 * @file    solver_options_tests.cpp
 * @brief   Tests for the solver options: their ranges, the netlist and the file, and what each engine does with them.
 */

#include "circuit/components/capacitor.h"
#include "circuit/components/diode.h"
#include "circuit/components/fuse.h"
#include "circuit/components/ground.h"
#include "circuit/components/inductor.h"
#include "circuit/components/resistor.h"
#include "circuit/components/subcircuit.h"
#include "circuit/components/vcc.h"
#include "circuit/components/voltage_source.h"
#include "circuit/solver_options.h"
#include "circuit/subcircuit.h"
#include "nodalys_in_use.h"
#include "schematic/schematic.h"
#include "schematic/schematic_file.h"
#include "simulation/check_solver_options.h"
#include "simulation/circuit_checks.h"
#include "simulation/circuit_copy.h"
#include "simulation/simulator.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;

/// A 1 mH inductor and a 1 uF capacitor in a loop, the capacitor charged to 1 V: a tank of about 5 kHz.
struct Tank {
    Core::Inductor Coil;
    Core::Capacitor Capacitor;
    Core::Ground Reference;
    Core::Circuit Circuit;

    Tank() {
        Coil.SetName("L1");
        Coil.SetValue(1e-3);
        Capacitor.SetName("C1");
        Capacitor.SetValue(1e-6);
        Capacitor.SetInitialCondition(1.0);
        Circuit.Add(Coil, {1, 0});
        Circuit.Add(Capacitor, {1, 0});
        Circuit.Add(Reference, {0});
    }
};

/// Largest voltage of node 1 between two times, which for the tank is the amplitude of the ringing.
double PeakBetween(const Core::Transient &transient, const double from, const double to) {
    double peak = 0.0;
    for (std::size_t index = 0; index < transient.Times.size(); ++index) {
        if (transient.Times[index] >= from && transient.Times[index] <= to) {
            peak = std::max(peak, std::abs(transient.NodeVoltages[1][index]));
        }
    }
    return peak;
}

/// A 5 V supply, a 1k resistor and a diode in series to ground, with the diode voltage on node 2.
struct BiasedDiode {
    Core::VCC Supply;
    Core::Resistor Series;
    Core::Diode Part{Core::ComponentType::DIODE};
    Core::Ground Reference;
    Core::Circuit Circuit;

    BiasedDiode() {
        Supply.SetName("V1");
        Supply.SetValue(5.0);
        Series.SetName("R1");
        Series.SetValue(1e3);
        Part.SetName("D1");
        Circuit.Add(Supply, {1});
        Circuit.Add(Series, {1, 2});
        Circuit.Add(Part, {2, 0});
        Circuit.Add(Reference, {0});
    }
};

/// A rectifier driven by a sine, whose time step an engine picks by the error it allows.
struct Rectifier {
    Core::VoltageSource Input;
    Core::Resistor Series;
    Core::Diode Part{Core::ComponentType::DIODE};
    Core::Ground Reference;
    Core::Circuit Circuit;

    Rectifier() {
        Input.SetName("V1");
        Input.SetSourceType(Core::VoltageSource::SourceType::AC);
        Input.SetAC({.Amplitude = 5.0, .Frequency = 1e3, .Offset = 0.0});
        Series.SetName("R1");
        Series.SetValue(1e3);
        Part.SetName("D1");
        Circuit.Add(Input, {1, 0});
        Circuit.Add(Series, {1, 2});
        Circuit.Add(Part, {2, 0});
        Circuit.Add(Reference, {0});
    }
};

/// Runs a check on ngspice and then on Nodalys.
template <typename Check>
void OnBothEngines(Check check) {
    check();
    const TestSupport::NodalysInUse nodalys_in_use;
    check();
}

/// A 5 V supply on node 1 feeding a fuse to node 2, where a resistor goes to ground.
struct FuseLoop {
    Core::VCC Supply;
    Core::Fuse Part;
    Core::Resistor Load;
    Core::Ground Reference;
    Core::Circuit Circuit;

    FuseLoop() {
        Supply.SetName("V1");
        Supply.SetValue(5.0);
        Part.SetName("FU1");
        Load.SetName("R1");
        Load.SetValue(100.0);
        Circuit.Add(Supply, {1});
        Circuit.Add(Part, {1, 2});
        Circuit.Add(Load, {2, 0});
        Circuit.Add(Reference, {0});
    }
};

std::size_t CountOccurrences(const std::string &text, const std::string_view part) {
    std::size_t count = 0;
    for (std::size_t at = text.find(part); at != std::string::npos; at = text.find(part, at + 1)) {
        ++count;
    }
    return count;
}

GUI::LoadedSchematic LoadOrFail(const std::string &text) {
    auto loaded = GUI::LoadSchematic(text);
    REQUIRE(loaded.has_value());
    return std::move(*loaded);
}

/// Largest distance between the voltage of node 2 and the one a fine run gives at the same time.
double MaxErrorAgainst(const Core::Transient &run, const Core::Transient &reference) {
    double error = 0.0;
    for (std::size_t index = 0; index < run.Times.size(); ++index) {
        const auto at = std::ranges::lower_bound(reference.Times, run.Times[index]);
        const auto other = static_cast<std::size_t>(at - reference.Times.begin());
        if (other < reference.Times.size()) {
            error = std::max(error, std::abs(run.NodeVoltages[2][index] - reference.NodeVoltages[2][other]));
        }
    }
    return error;
}

} // namespace

TEST_CASE("The default solver options are those of SPICE and write nothing in the netlist", "[solver_options]") {
    const Core::SolverOptions options;
    CHECK(options.RelativeTolerance == 1e-3);
    CHECK(options.CurrentToleranceAmperes == 1e-12);
    CHECK(options.VoltageToleranceVolts == 1e-6);
    CHECK(options.Method == Core::IntegrationMethod::DEFAULT);
    CHECK(options.NominalTemperatureCelsius == 27.0);
    CHECK_FALSE(Core::CheckSolverOptions(options).has_value());

    BiasedDiode setup;
    CHECK(setup.Circuit.GetSolverOptions() == options);
    CHECK_THAT(setup.Circuit.ToSpiceNetlist(), !ContainsSubstring(".options"));
}

TEST_CASE("The netlist writes the options that are not the default on one .options line", "[solver_options]") {
    BiasedDiode setup;
    SECTION("Every option") {
        setup.Circuit.SetSolverOptions({.RelativeTolerance = 1e-4,
                                        .CurrentToleranceAmperes = 1e-13,
                                        .VoltageToleranceVolts = 1e-7,
                                        .Method = Core::IntegrationMethod::GEAR,
                                        .NominalTemperatureCelsius = 50.0});
        CHECK_THAT(setup.Circuit.ToSpiceNetlist(),
                   ContainsSubstring(".options reltol=100u abstol=100f vntol=100n tnom=50 method=gear\n"));
    }
    SECTION("Only what changed") {
        setup.Circuit.SetSolverOptions({.Method = Core::IntegrationMethod::TRAPEZOIDAL});
        CHECK_THAT(setup.Circuit.ToSpiceNetlist(), ContainsSubstring(".options method=trap\n"));
        setup.Circuit.SetSolverOptions({.NominalTemperatureCelsius = -10.0});
        CHECK_THAT(setup.Circuit.ToSpiceNetlist(), ContainsSubstring(".options tnom=-10\n"));
    }
}

TEST_CASE("A fuse asks for Gear unless the user picked a method, and the method is never written twice",
          "[solver_options]") {
    FuseLoop setup;
    const auto method_lines = [&] { return CountOccurrences(setup.Circuit.ToSpiceNetlist(), "method="); };
    SECTION("By default the fuse writes its Gear") {
        CHECK_THAT(setup.Circuit.ToSpiceNetlist(), ContainsSubstring(".options method=gear\n"));
        CHECK(method_lines() == 1);
    }
    SECTION("With the other options the fuse still writes its Gear once") {
        setup.Circuit.SetSolverOptions({.RelativeTolerance = 1e-4});
        const std::string netlist = setup.Circuit.ToSpiceNetlist();
        CHECK_THAT(netlist, ContainsSubstring(".options method=gear\n"));
        CHECK_THAT(netlist, ContainsSubstring(".options reltol=100u\n"));
        CHECK(method_lines() == 1);
    }
    SECTION("Trapezoidal wins over the fuse") {
        setup.Circuit.SetSolverOptions({.Method = Core::IntegrationMethod::TRAPEZOIDAL});
        const std::string netlist = setup.Circuit.ToSpiceNetlist();
        CHECK_THAT(netlist, ContainsSubstring("method=trap"));
        CHECK_THAT(netlist, !ContainsSubstring("method=gear"));
        CHECK(method_lines() == 1);
    }
    SECTION("Gear picked by the user is written once") {
        setup.Circuit.SetSolverOptions({.Method = Core::IntegrationMethod::GEAR});
        CHECK(method_lines() == 1);
    }
}

TEST_CASE("A fuse inside a block does not write the method inside the block, and the options come once",
          "[solver_options]") {
    static Core::Fuse fuse;
    static Core::Resistor load;
    fuse.SetName("FU1");
    load.SetName("R1");
    auto definition = std::make_shared<Core::SubcircuitDefinition>();
    definition->Name = "PROTECTED";
    definition->Pins = {{.Name = "A", .Node = 1}};
    definition->Body.Add(fuse, {1, 2});
    definition->Body.Add(load, {2, 0});
    Core::SubcircuitInstance instance("PROTECTED", {"A"});
    instance.SetName("X1");
    Core::VCC supply;
    supply.SetName("V1");
    Core::Ground ground;
    Core::Circuit circuit;
    circuit.AddSubcircuit(definition);
    circuit.Add(supply, {1});
    circuit.Add(instance, {1});
    circuit.Add(ground, {0});
    circuit.SetSolverOptions({.RelativeTolerance = 1e-4});
    const std::string netlist = circuit.ToSpiceNetlist();
    CHECK(CountOccurrences(netlist, ".options method=gear\n") == 1);
    CHECK(CountOccurrences(netlist, ".options reltol=100u\n") == 1);
}

TEST_CASE("The options must lie in a range both engines take, and the message names the option", "[solver_options]") {
    const auto problem_with = [](const Core::SolverOptions &options) { return Core::CheckSolverOptions(options); };
    SECTION("Relative tolerance") {
        for (const double value : {0.0, -1e-3, 1.0, 1e-12, std::nan("")}) {
            const auto problem = problem_with({.RelativeTolerance = value});
            REQUIRE(problem.has_value());
            CHECK_THAT(*problem, ContainsSubstring("relative tolerance (reltol)"));
        }
        CHECK_FALSE(problem_with({.RelativeTolerance = Core::MIN_RELATIVE_TOLERANCE}).has_value());
        CHECK_FALSE(problem_with({.RelativeTolerance = Core::MAX_RELATIVE_TOLERANCE}).has_value());
    }
    SECTION("Current tolerance") {
        const auto problem = problem_with({.CurrentToleranceAmperes = 1e-3});
        REQUIRE(problem.has_value());
        CHECK_THAT(*problem, ContainsSubstring("current tolerance (abstol)"));
        CHECK_THAT(*problem, ContainsSubstring("A"));
        CHECK(problem_with({.CurrentToleranceAmperes = 0.0}).has_value());
    }
    SECTION("Voltage tolerance") {
        const auto problem = problem_with({.VoltageToleranceVolts = 1.0});
        REQUIRE(problem.has_value());
        CHECK_THAT(*problem, ContainsSubstring("voltage tolerance (vntol)"));
        CHECK(problem_with({.VoltageToleranceVolts = -1e-6}).has_value());
    }
    SECTION("Nominal temperature") {
        const auto problem = problem_with({.NominalTemperatureCelsius = -273.15});
        REQUIRE(problem.has_value());
        CHECK_THAT(*problem, ContainsSubstring("nominal temperature (tnom)"));
        CHECK(problem_with({.NominalTemperatureCelsius = 1e4}).has_value());
        CHECK_FALSE(problem_with({.NominalTemperatureCelsius = -40.0}).has_value());
    }
}

TEST_CASE("The checks of a circuit refuse options out of range before a backend runs it", "[solver_options]") {
    BiasedDiode setup;
    setup.Circuit.SetSolverOptions({.RelativeTolerance = 0.5});
    const std::optional<std::string> problem = Core::CheckCircuit(setup.Circuit);
    REQUIRE(problem.has_value());
    CHECK_THAT(*problem, ContainsSubstring("reltol"));
}

TEST_CASE("The circuit copies and derived circuits carry the solver options", "[solver_options]") {
    BiasedDiode setup;
    const Core::SolverOptions options{.RelativeTolerance = 1e-5, .Method = Core::IntegrationMethod::GEAR};
    setup.Circuit.SetSolverOptions(options);
    const Core::CircuitCopy copy(setup.Circuit);
    CHECK(copy.GetCircuit().GetSolverOptions() == options);
    Core::Circuit derived;
    derived.AddDefinitionsOf(setup.Circuit);
    CHECK(derived.GetSolverOptions() == options);
}

TEST_CASE("The circuit of a schematic takes the solver options of its settings", "[solver_options]") {
    GUI::Schematic schematic;
    GUI::SimulationSettings settings = schematic.GetSimulationSettings();
    settings.Solver = {.RelativeTolerance = 2e-4, .NominalTemperatureCelsius = 35.0};
    schematic.SetSimulationSettings(settings);
    CHECK(schematic.BuildCircuit().GetSolverOptions() == settings.Solver);
}

TEST_CASE("The solver options load back unchanged and an old file has the defaults", "[solver_options]") {
    GUI::SimulationSettings settings;
    settings.Solver = {.RelativeTolerance = 1e-5,
                       .CurrentToleranceAmperes = 1e-14,
                       .VoltageToleranceVolts = 1e-8,
                       .Method = Core::IntegrationMethod::TRAPEZOIDAL,
                       .NominalTemperatureCelsius = 40.0};
    const GUI::LoadedSchematic loaded = LoadOrFail(GUI::SaveSchematic({}, {}, settings, {}));
    CHECK(loaded.Warnings.empty());
    CHECK(loaded.Settings.Solver == settings.Solver);

    for (const Core::IntegrationMethod method : {Core::IntegrationMethod::DEFAULT, Core::IntegrationMethod::GEAR}) {
        settings.Solver.Method = method;
        CHECK(LoadOrFail(GUI::SaveSchematic({}, {}, settings, {})).Settings.Solver.Method == method);
    }

    // A file saved before the options existed
    const GUI::LoadedSchematic old = LoadOrFail(
        R"({"format": "imcsim-schematic", "version": 1, "simulation": {"transient": {"stop": 1e-3, "step": 1e-6}}})");
    CHECK(old.Warnings.empty());
    CHECK(old.Settings.Solver == Core::SolverOptions{});

    // A file with some of the keys keeps the defaults of the others
    const GUI::LoadedSchematic partial =
        LoadOrFail(R"({"format": "imcsim-schematic", "version": 1, "simulation": {"solver": {"reltol": 1e-4}}})");
    CHECK(partial.Warnings.empty());
    CHECK(partial.Settings.Solver == Core::SolverOptions{.RelativeTolerance = 1e-4});
}

TEST_CASE("Invalid solver options fall back to their defaults with a warning", "[solver_options]") {
    SECTION("A value out of range") {
        const GUI::LoadedSchematic loaded =
            LoadOrFail(R"({"format": "imcsim-schematic", "version": 1, "simulation": {"solver": {"reltol": 5.0}}})");
        REQUIRE(loaded.Warnings.size() == 1);
        CHECK_THAT(loaded.Warnings[0], ContainsSubstring("solver settings"));
        CHECK(loaded.Settings.Solver == Core::SolverOptions{});
    }
    SECTION("An unknown method") {
        const GUI::LoadedSchematic loaded = LoadOrFail(
            R"({"format": "imcsim-schematic", "version": 1, "simulation": {"solver": {"method": "euler"}}})");
        REQUIRE(loaded.Warnings.size() == 1);
        CHECK(loaded.Settings.Solver == Core::SolverOptions{});
    }
    SECTION("A value that is not a number") {
        const GUI::LoadedSchematic loaded =
            LoadOrFail(R"({"format": "imcsim-schematic", "version": 1, "simulation": {"solver": {"tnom": "hot"}}})");
        REQUIRE(loaded.Warnings.size() == 1);
        CHECK(loaded.Settings.Solver == Core::SolverOptions{});
    }
}

TEST_CASE("A looser relative tolerance leaves the operating point farther from the exact one on both engines",
          "[solver_options]") {
    OnBothEngines([] {
        const auto solve = [](const double reltol) {
            BiasedDiode setup;
            setup.Circuit.SetSolverOptions({.RelativeTolerance = reltol});
            const Core::OperatingPointRun run = Core::RunOperatingPoint(setup.Circuit);
            REQUIRE(run.Result.has_value());
            return run.Result->NodeVoltages[2];
        };
        const double exact = solve(1e-9);
        // The solver stops where two iterations agree within the tolerance, so the distance follows it
        CHECK(std::abs(solve(0.1) - exact) > 2e-3);
        CHECK(std::abs(solve(1e-2) - exact) > 5e-5);
        CHECK(std::abs(solve(1e-2) - exact) < 5e-3);
        CHECK(std::abs(solve(1e-6) - exact) < 1e-6);
    });
}

TEST_CASE("The current tolerance reaches both engines", "[solver_options]") {
    OnBothEngines([] {
        const auto solve = [](const double abstol) {
            BiasedDiode setup;
            setup.Circuit.SetSolverOptions(
                {.RelativeTolerance = 1e-9, .CurrentToleranceAmperes = abstol, .VoltageToleranceVolts = 1e-2});
            const Core::OperatingPointRun run = Core::RunOperatingPoint(setup.Circuit);
            REQUIRE(run.Result.has_value());
            return run.Result->NodeVoltages[2];
        };
        // A tolerance of a microampere on a diode that carries 4 mA leaves the voltage a fraction of a microvolt off
        const double difference = std::abs(solve(1e-6) - solve(1e-12));
        CHECK(difference > 1e-7);
        CHECK(difference < 1e-4);
    });
}

TEST_CASE("A tighter relative tolerance makes a transient more exact on both engines", "[solver_options]") {
    constexpr double COARSE_STEP = 100e-6;
    const auto run_rectifier = [](const double reltol, const double step) {
        Rectifier setup;
        setup.Circuit.SetSolverOptions({.RelativeTolerance = reltol});
        Core::TransientRun run = Core::RunTransient(setup.Circuit, {.StopTime = 5e-3, .TimeStep = step});
        REQUIRE(run.Result.has_value());
        return std::move(*run.Result);
    };
    SECTION("ngspice takes more steps") {
        const std::size_t loose = run_rectifier(1e-3, COARSE_STEP).Times.size();
        const std::size_t tight = run_rectifier(1e-5, COARSE_STEP).Times.size();
        CHECK(tight > 2 * loose);
    }
    SECTION("Nodalys follows the fine run closer") {
        const TestSupport::NodalysInUse nodalys_in_use;
        const Core::Transient reference = run_rectifier(1e-6, 1e-6);
        const double loose = MaxErrorAgainst(run_rectifier(1e-2, COARSE_STEP), reference);
        const double tight = MaxErrorAgainst(run_rectifier(1e-5, COARSE_STEP), reference);
        CHECK(tight < loose / 5.0);
    }
}

TEST_CASE("Gear damps the ringing of a tank that the trapezoidal rule keeps, on both engines", "[solver_options]") {
    OnBothEngines([] {
        constexpr double STOP_TIME = 3e-3;
        const auto last_peak = [](const Core::IntegrationMethod method) {
            Tank setup;
            setup.Circuit.SetSolverOptions({.Method = method});
            const Core::TransientRun run =
                Core::RunTransient(setup.Circuit, {.StopTime = STOP_TIME, .TimeStep = 10e-6});
            REQUIRE(run.Result.has_value());
            // The charge of the capacitor starts the ringing at 1 V, and the last 0.4 ms hold two periods
            CHECK_THAT(PeakBetween(*run.Result, 0.0, 0.4e-3), WithinAbs(1.0, 1e-3));
            return PeakBetween(*run.Result, STOP_TIME - 0.4e-3, STOP_TIME);
        };
        const double trapezoidal = last_peak(Core::IntegrationMethod::TRAPEZOIDAL);
        const double gear = last_peak(Core::IntegrationMethod::GEAR);
        CHECK(trapezoidal > 0.95);
        CHECK(gear < 0.75);
        // Without a fuse the default is the trapezoidal rule
        CHECK_THAT(last_peak(Core::IntegrationMethod::DEFAULT), WithinAbs(trapezoidal, 1e-6));
    });
}

TEST_CASE("The nominal temperature moves the diode of a run at the default temperature on both engines",
          "[solver_options]") {
    OnBothEngines([] {
        const auto diode_voltage = [](const double nominal) {
            BiasedDiode setup;
            setup.Circuit.SetSolverOptions({.NominalTemperatureCelsius = nominal});
            const Core::OperatingPointRun run = Core::RunOperatingPoint(setup.Circuit);
            REQUIRE(run.Result.has_value());
            return run.Result->NodeVoltages[2];
        };
        // The parameters are given at tnom, so a run at 27 C with a hotter tnom has a diode that leaks less than it
        // was measured to, and needs more voltage for the same current
        const double nominal = diode_voltage(27.0);
        CHECK(diode_voltage(77.0) > nominal + 0.1);
        CHECK(diode_voltage(-20.0) < nominal - 0.1);
    });
}

TEST_CASE("Both engines agree on the diode at another nominal temperature", "[solver_options]") {
    const auto diode_voltage = [] {
        BiasedDiode setup;
        setup.Circuit.SetSolverOptions({.NominalTemperatureCelsius = 77.0});
        const Core::OperatingPointRun run = Core::RunOperatingPoint(setup.Circuit);
        REQUIRE(run.Result.has_value());
        return run.Result->NodeVoltages[2];
    };
    const double ngspice = diode_voltage();
    const TestSupport::NodalysInUse nodalys_in_use;
    CHECK_THAT(diode_voltage(), WithinAbs(ngspice, 1e-3));
}
