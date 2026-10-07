/**
 * @file    simulator_tests.cpp
 * @brief   Tests for the operating point simulation, run through the real ngspice library.
 */

#include "circuit.h"
#include "components/bjt.h"
#include "components/capacitor.h"
#include "components/current_source.h"
#include "components/diode.h"
#include "components/ground.h"
#include "components/mosfet.h"
#include "components/resistor.h"
#include "components/vcc.h"
#include "components/voltage_source.h"
#include "simulator.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <complex>
#include <numbers>

namespace {

/**
 * @struct  Divider
 * @brief   A 10 V supply on node 1 feeding 1k and 3k resistors in series to ground, with the middle on node 2.
 */
struct Divider {
    Core::VCC Supply;
    Core::Resistor Top;
    Core::Resistor Bottom;
    Core::Ground Reference;
    Core::Circuit Circuit;

    Divider() {
        Supply.SetName("V1");
        Supply.SetValue(10.0);
        Top.SetName("R1");
        Bottom.SetName("R2");
        Bottom.SetValue(3e3);
        Circuit.Add(Supply, {1});
        Circuit.Add(Top, {1, 2});
        Circuit.Add(Bottom, {2, 0});
        Circuit.Add(Reference, {0});
    }
};

/**
 * @struct  LowPass
 * @brief   An AC source on node 1 driving a 1k resistor and a 1u capacitor to ground, with the output on node 2.
 */
struct LowPass {
    Core::VoltageSource Source;
    Core::Resistor Series;
    Core::Capacitor Shunt;
    Core::Ground Reference;
    Core::Circuit Circuit;

    LowPass() {
        Source.SetName("Vin1");
        Source.SetSourceType(Core::VoltageSource::SourceType::AC);
        Series.SetName("R1");
        Shunt.SetName("C1");
        Circuit.Add(Source, {1, 0});
        Circuit.Add(Series, {1, 2});
        Circuit.Add(Shunt, {2, 0});
        Circuit.Add(Reference, {0});
    }
};

/**
 * @struct  DiodeLoop
 * @brief   A supply on node 1 feeding a resistor to node 2, where a diode part connects to ground.
 * @details The diode is forward biased from node 2 to ground, except a Zener, which is reversed into breakdown.
 */
struct DiodeLoop {
    Core::VCC Supply;
    Core::Resistor Series;
    Core::Diode Part;
    Core::Ground Reference;
    Core::Circuit Circuit;

    DiodeLoop(const Core::ComponentType type, const double supply, const double resistance) : Part(type) {
        Supply.SetName("V1");
        Supply.SetValue(supply);
        Series.SetName("R1");
        Series.SetValue(resistance);
        Part.SetName("D1");
        Circuit.Add(Supply, {1});
        Circuit.Add(Series, {1, 2});
        if (type == Core::ComponentType::ZenerDiode) {
            Circuit.Add(Part, {0, 2});
        } else {
            Circuit.Add(Part, {2, 0});
        }
        Circuit.Add(Reference, {0});
    }
};

/**
 * @struct  CommonEmitter
 * @brief   A bipolar transistor with its emitter on ground, its base fed from the supply through 470k and its
 *          collector through 1k, with the supply on node 1, the base on node 2 and the collector on node 3.
 * @details For a PNP the supply is negative, which mirrors every voltage and current of the NPN version.
 */
struct CommonEmitter {
    Core::VCC Supply;
    Core::Resistor BaseResistor;
    Core::Resistor CollectorResistor;
    Core::BJT Transistor;
    Core::Ground Reference;
    Core::Circuit Circuit;

    explicit CommonEmitter(const Core::ComponentType type) : Transistor(type) {
        Supply.SetName("V1");
        Supply.SetValue(type == Core::ComponentType::PNP ? -12.0 : 12.0);
        BaseResistor.SetName("R1");
        BaseResistor.SetValue(470e3);
        CollectorResistor.SetName("R2");
        Transistor.SetName("Q1");
        Circuit.Add(Supply, {1});
        Circuit.Add(BaseResistor, {1, 2});
        Circuit.Add(CollectorResistor, {1, 3});
        Circuit.Add(Transistor, {3, 2, 0});
        Circuit.Add(Reference, {0});
    }
};

/**
 * @struct  LowSideSwitch
 * @brief   A MOSFET with its source on ground, its drain fed from a 5 V supply through 1k and its gate held by
 *          a second supply, with the drain on node 2 and the gate on node 3.
 * @details For a PMOS both supplies are negative, which mirrors the NMOS version.
 */
struct LowSideSwitch {
    Core::VCC Supply;
    Core::VCC Gate;
    Core::Resistor Load;
    Core::MOSFET Transistor;
    Core::Ground Reference;
    Core::Circuit Circuit;

    LowSideSwitch(const Core::ComponentType type, const double gate_voltage) : Transistor(type) {
        const double sign = type == Core::ComponentType::PMOS ? -1.0 : 1.0;
        Supply.SetName("V1");
        Supply.SetValue(sign * 5.0);
        Gate.SetName("V2");
        Gate.SetValue(sign * gate_voltage);
        Load.SetName("R1");
        Transistor.SetName("M1");
        Circuit.Add(Supply, {1});
        Circuit.Add(Gate, {3});
        Circuit.Add(Load, {1, 2});
        Circuit.Add(Transistor, {2, 3, 0});
        Circuit.Add(Reference, {0});
    }
};

// Rebuilds a complex current from the decibels and degrees an AC sweep reports
std::complex<double> FromDecibels(const double decibels, const double degrees) {
    return std::polar(std::pow(10.0, decibels / 20.0), degrees * std::numbers::pi / 180.0);
}

// Finds a current by component name, failing the test when it is missing
template <typename Current> const Current &FindCurrent(const std::vector<Current> &currents, const std::string &name) {
    const auto found = std::ranges::find_if(currents, [&name](const Current &current) { return current.Name == name; });
    REQUIRE(found != currents.end());
    return *found;
}

} // namespace

TEST_CASE("The operating point of a resistive divider matches the hand calculation", "[simulator]") {
    const Divider divider;
    const Core::OperatingPointRun run = Core::RunOperatingPoint(divider.Circuit);
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    const std::vector<double> &voltages = run.Result->NodeVoltages;
    REQUIRE(voltages.size() == 3);
    CHECK(voltages[0] == 0.0);
    CHECK_THAT(voltages[1], Catch::Matchers::WithinRel(10.0, 1e-9));
    CHECK_THAT(voltages[2], Catch::Matchers::WithinRel(7.5, 1e-9));
    CHECK_FALSE(run.Messages.empty());
}

TEST_CASE("Runs are independent of each other", "[simulator]") {
    Divider divider;
    REQUIRE(Core::RunOperatingPoint(divider.Circuit).Result);

    divider.Supply.SetValue(4.0);
    const Core::OperatingPointRun run = Core::RunOperatingPoint(divider.Circuit);
    REQUIRE(run.Result);
    CHECK_THAT(run.Result->NodeVoltages[2], Catch::Matchers::WithinRel(3.0, 1e-9));
}

TEST_CASE("Circuits without ground or components are rejected before reaching ngspice", "[simulator]") {
    SECTION("empty") {
        const Core::Circuit circuit;
        const Core::OperatingPointRun run = Core::RunOperatingPoint(circuit);
        CHECK_FALSE(run.Result);
        CHECK(run.Messages.empty());
    }
    SECTION("no ground") {
        Core::Resistor resistor;
        resistor.SetName("R1");
        Core::Circuit circuit;
        circuit.Add(resistor, {1, 2});
        const Core::OperatingPointRun run = Core::RunOperatingPoint(circuit);
        REQUIRE_FALSE(run.Result);
        CHECK(run.Result.error().find("ground") != std::string::npos);
        CHECK(run.Messages.empty());
    }
}

TEST_CASE("A node with no DC path to ground still runs but reports warnings", "[simulator]") {
    Core::VCC supply;
    supply.SetName("V1");
    Core::Capacitor capacitor;
    capacitor.SetName("C1");
    Core::Resistor resistor;
    resistor.SetName("R1");
    const Core::Ground reference;
    Core::Circuit circuit;
    circuit.Add(supply, {1});
    circuit.Add(capacitor, {1, 2});
    circuit.Add(resistor, {2, 3});
    circuit.Add(reference, {0});

    const Core::OperatingPointRun run = Core::RunOperatingPoint(circuit);
    CHECK(run.Result);
    CHECK(std::ranges::any_of(run.Messages, [](const Core::SimulatorMessage &message) {
        return message.FromErrorStream && message.Text.find("singular matrix") != std::string::npos;
    }));
}

TEST_CASE("A transient run follows the sine of an AC source", "[simulator]") {
    Core::VoltageSource source;
    source.SetName("Vin1");
    source.SetSourceType(Core::VoltageSource::SourceType::AC);
    source.SetAC({.Amplitude = 2.0, .Frequency = 1e3, .Offset = 1.0});
    Core::Resistor load;
    load.SetName("R1");
    const Core::Ground reference;
    Core::Circuit circuit;
    circuit.Add(source, {1, 0});
    circuit.Add(load, {1, 0});
    circuit.Add(reference, {0});

    const Core::TransientRun run = Core::RunTransient(circuit, {.StopTime = 2e-3, .TimeStep = 10e-6});
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    const Core::Transient &transient = *run.Result;
    REQUIRE(transient.NodeVoltages.size() == 2);
    REQUIRE(transient.NodeVoltages[1].size() == transient.Times.size());
    CHECK_THAT(transient.Times.back(), Catch::Matchers::WithinRel(2e-3, 1e-9));
    for (std::size_t index = 0; index < transient.Times.size(); ++index) {
        const double expected = 1.0 + 2.0 * std::sin(2.0 * std::numbers::pi * 1e3 * transient.Times[index]);
        CHECK_THAT(transient.NodeVoltages[1][index], Catch::Matchers::WithinAbs(expected, 1e-3));
    }
}

TEST_CASE("An AC sweep of an RC low-pass matches its transfer function", "[simulator]") {
    const LowPass low_pass;
    const Core::ACSweepRun run =
        Core::RunACSweep(low_pass.Circuit, {.StartFrequency = 1.0, .StopFrequency = 1e6, .PointsPerDecade = 10});
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    const Core::ACSweep &sweep = *run.Result;
    REQUIRE(sweep.Frequencies.size() == 61);
    CHECK_THAT(sweep.Frequencies.front(), Catch::Matchers::WithinRel(1.0, 1e-9));
    CHECK_THAT(sweep.Frequencies.back(), Catch::Matchers::WithinRel(1e6, 1e-9));
    for (std::size_t index = 0; index < sweep.Frequencies.size(); ++index) {
        const double ratio = 2.0 * std::numbers::pi * sweep.Frequencies[index] * 1e3 * 1e-6;
        CHECK_THAT(sweep.NodeMagnitudesDecibels[1][index], Catch::Matchers::WithinAbs(0.0, 1e-6));
        CHECK_THAT(sweep.NodeMagnitudesDecibels[2][index],
                   Catch::Matchers::WithinAbs(-10.0 * std::log10(1.0 + ratio * ratio), 1e-6));
        CHECK_THAT(sweep.NodePhasesDegrees[2][index],
                   Catch::Matchers::WithinAbs(-std::atan(ratio) * 180.0 / std::numbers::pi, 1e-6));
    }
}

TEST_CASE("An AC sweep without an AC source is rejected before reaching ngspice", "[simulator]") {
    LowPass low_pass;
    for (const auto type : {Core::VoltageSource::SourceType::DC, Core::VoltageSource::SourceType::Pulse}) {
        low_pass.Source.SetSourceType(type);
        const Core::ACSweepRun run = Core::RunACSweep(low_pass.Circuit, {});
        REQUIRE_FALSE(run.Result);
        CHECK(run.Result.error().find("AC source") != std::string::npos);
        CHECK(run.Messages.empty());
    }
}

TEST_CASE("A pulse charges an RC low-pass with its time constant", "[simulator]") {
    LowPass low_pass;
    low_pass.Source.SetSourceType(Core::VoltageSource::SourceType::Pulse);
    // A single 1 V step at t = 0, far longer than the 1 ms time constant
    low_pass.Source.SetPulse(
        {.Low = 0.0, .High = 1.0, .RiseTime = 1e-9, .FallTime = 1e-9, .Width = 1.0, .Period = 2.0});

    const Core::TransientRun run = Core::RunTransient(low_pass.Circuit, {.StopTime = 5e-3, .TimeStep = 10e-6});
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    const Core::Transient &transient = *run.Result;
    CHECK(transient.NodeVoltages[2].front() == 0.0);
    for (std::size_t index = 0; index < transient.Times.size(); ++index) {
        const double expected = 1.0 - std::exp(-transient.Times[index] / 1e-3);
        CHECK_THAT(transient.NodeVoltages[2][index], Catch::Matchers::WithinAbs(expected, 5e-3));
    }
}

TEST_CASE("Invalid analysis settings are rejected before reaching ngspice", "[simulator]") {
    const LowPass low_pass;
    SECTION("transient") {
        for (const Core::TransientSettings settings : {Core::TransientSettings{.StopTime = 0.0, .TimeStep = 1e-6},
                                                       Core::TransientSettings{.StopTime = 1e-3, .TimeStep = 0.0},
                                                       Core::TransientSettings{.StopTime = 1e-3, .TimeStep = 1e-2},
                                                       Core::TransientSettings{.StopTime = 1.0, .TimeStep = 1e-9}}) {
            const Core::TransientRun run = Core::RunTransient(low_pass.Circuit, settings);
            CHECK_FALSE(run.Result);
            CHECK(run.Messages.empty());
        }
    }
    SECTION("AC sweep") {
        for (const Core::ACSweepSettings settings :
             {Core::ACSweepSettings{.StartFrequency = 0.0, .StopFrequency = 1e3, .PointsPerDecade = 10},
              Core::ACSweepSettings{.StartFrequency = 1e3, .StopFrequency = 1e3, .PointsPerDecade = 10},
              Core::ACSweepSettings{.StartFrequency = 1.0, .StopFrequency = 1e3, .PointsPerDecade = 0}}) {
            const Core::ACSweepRun run = Core::RunACSweep(low_pass.Circuit, settings);
            CHECK_FALSE(run.Result);
            CHECK(run.Messages.empty());
        }
    }
}

TEST_CASE("The operating point has the current through every component, signed as in SPICE", "[simulator]") {
    const Divider divider;
    const Core::OperatingPointRun run = Core::RunOperatingPoint(divider.Circuit);
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    const std::vector<Core::ComponentCurrent> &currents = run.Result->Currents;
    // Ground carries no current
    CHECK(currents.size() == 3);
    CHECK_THAT(FindCurrent(currents, "R1").Current, Catch::Matchers::WithinRel(2.5e-3, 1e-9));
    CHECK_THAT(FindCurrent(currents, "R2").Current, Catch::Matchers::WithinRel(2.5e-3, 1e-9));
    // The supply delivers the current, so it flows through it from ground to its terminal
    CHECK_THAT(FindCurrent(currents, "V1").Current, Catch::Matchers::WithinRel(-2.5e-3, 1e-9));
}

TEST_CASE("A current source pushes its current into the node at its second terminal", "[simulator]") {
    Core::CurrentSource source;
    source.SetName("I1");
    source.SetValue(2e-3);
    Core::Resistor load;
    load.SetName("R1");
    const Core::Ground reference;
    Core::Circuit circuit;
    circuit.Add(source, {0, 1});
    circuit.Add(load, {1, 0});
    circuit.Add(reference, {0});

    const Core::OperatingPointRun run = Core::RunOperatingPoint(circuit);
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    CHECK_THAT(run.Result->NodeVoltages[1], Catch::Matchers::WithinRel(2.0, 1e-9));
    CHECK_THAT(FindCurrent(run.Result->Currents, "I1").Current, Catch::Matchers::WithinRel(2e-3, 1e-9));
    CHECK_THAT(FindCurrent(run.Result->Currents, "R1").Current, Catch::Matchers::WithinRel(2e-3, 1e-9));
}

TEST_CASE("A transient has the charging current of an RC low-pass", "[simulator]") {
    LowPass low_pass;
    low_pass.Source.SetSourceType(Core::VoltageSource::SourceType::Pulse);
    low_pass.Source.SetPulse(
        {.Low = 0.0, .High = 1.0, .RiseTime = 1e-9, .FallTime = 1e-9, .Width = 1.0, .Period = 2.0});

    const Core::TransientRun run = Core::RunTransient(low_pass.Circuit, {.StopTime = 5e-3, .TimeStep = 10e-6});
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    const Core::Transient &transient = *run.Result;
    const std::vector<double> &capacitor = FindCurrent(transient.Currents, "C1").Values;
    const std::vector<double> &source = FindCurrent(transient.Currents, "Vin1").Values;
    REQUIRE(capacitor.size() == transient.Times.size());
    for (std::size_t index = 0; index < transient.Times.size(); ++index) {
        // The first points sit on the step itself, where the current jumps from 0 to 1 mA
        if (transient.Times[index] < 20e-6) {
            continue;
        }
        const double expected = 1e-3 * std::exp(-transient.Times[index] / 1e-3);
        CHECK_THAT(capacitor[index], Catch::Matchers::WithinAbs(expected, 1e-5));
        CHECK_THAT(source[index], Catch::Matchers::WithinAbs(-expected, 1e-5));
    }
}

TEST_CASE("An AC sweep has the current through every component", "[simulator]") {
    const LowPass low_pass;
    const Core::ACSweepRun run =
        Core::RunACSweep(low_pass.Circuit, {.StartFrequency = 1.0, .StopFrequency = 1e6, .PointsPerDecade = 10});
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    const Core::ACSweep &sweep = *run.Result;
    const std::vector<double> &resistor_magnitudes = FindCurrent(sweep.CurrentMagnitudesDecibels, "R1").Values;
    const std::vector<double> &resistor_phases = FindCurrent(sweep.CurrentPhasesDegrees, "R1").Values;
    const std::vector<double> &capacitor_magnitudes = FindCurrent(sweep.CurrentMagnitudesDecibels, "C1").Values;
    const std::vector<double> &source_magnitudes = FindCurrent(sweep.CurrentMagnitudesDecibels, "Vin1").Values;
    const std::vector<double> &source_phases = FindCurrent(sweep.CurrentPhasesDegrees, "Vin1").Values;
    for (std::size_t index = 0; index < sweep.Frequencies.size(); ++index) {
        const double omega = 2.0 * std::numbers::pi * sweep.Frequencies[index];
        const std::complex<double> current = 1.0 / std::complex<double>(1e3, -1.0 / (omega * 1e-6));
        const double magnitude = 20.0 * std::log10(std::abs(current));
        const double phase = std::arg(current) * 180.0 / std::numbers::pi;
        CHECK_THAT(resistor_magnitudes[index], Catch::Matchers::WithinAbs(magnitude, 1e-6));
        CHECK_THAT(resistor_phases[index], Catch::Matchers::WithinAbs(phase, 1e-6));
        CHECK_THAT(capacitor_magnitudes[index], Catch::Matchers::WithinAbs(magnitude, 1e-6));
        // The source carries the same current the other way, half a turn apart
        CHECK_THAT(source_magnitudes[index], Catch::Matchers::WithinAbs(magnitude, 1e-6));
        CHECK_THAT(std::abs(std::remainder(source_phases[index] - phase, 360.0)),
                   Catch::Matchers::WithinAbs(180.0, 1e-6));
    }
}

TEST_CASE("An AC current source excites an AC sweep", "[simulator]") {
    Core::CurrentSource source;
    source.SetName("I1");
    source.SetSourceType(Core::CurrentSource::SourceType::AC);
    Core::Resistor load;
    load.SetName("R1");
    const Core::Ground reference;
    Core::Circuit circuit;
    circuit.Add(source, {0, 1});
    circuit.Add(load, {1, 0});
    circuit.Add(reference, {0});

    const Core::ACSweepRun run = Core::RunACSweep(circuit, {.StartFrequency = 1.0, .StopFrequency = 1e3});
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    // 1 mA into 1k is 1 V, which is 0 dB; the source current is 1 mA, which is -60 dB relative to 1 A
    for (std::size_t index = 0; index < run.Result->Frequencies.size(); ++index) {
        CHECK_THAT(run.Result->NodeMagnitudesDecibels[1][index], Catch::Matchers::WithinAbs(0.0, 1e-6));
        CHECK_THAT(FindCurrent(run.Result->CurrentMagnitudesDecibels, "I1").Values[index],
                   Catch::Matchers::WithinAbs(-60.0, 1e-6));
        CHECK_THAT(FindCurrent(run.Result->CurrentMagnitudesDecibels, "R1").Values[index],
                   Catch::Matchers::WithinAbs(-60.0, 1e-6));
    }
}

TEST_CASE("A forward biased diode drops its forward voltage and carries the loop current", "[simulator]") {
    const DiodeLoop loop(Core::ComponentType::Diode, 5.0, 1e3);
    const Core::OperatingPointRun run = Core::RunOperatingPoint(loop.Circuit);
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    const double forward_voltage = run.Result->NodeVoltages[2];
    CHECK(forward_voltage > 0.6);
    CHECK(forward_voltage < 0.8);
    // The diode current flows from anode to cathode, the same as through the resistor
    const double resistor_current = FindCurrent(run.Result->Currents, "R1").Current;
    CHECK_THAT(resistor_current, Catch::Matchers::WithinRel((5.0 - forward_voltage) / 1e3, 1e-6));
    CHECK_THAT(FindCurrent(run.Result->Currents, "D1").Current, Catch::Matchers::WithinRel(resistor_current, 1e-6));
}

TEST_CASE("A Zener diode in breakdown holds its voltage", "[simulator]") {
    const DiodeLoop loop(Core::ComponentType::ZenerDiode, 12.0, 1e3);
    const Core::OperatingPointRun run = Core::RunOperatingPoint(loop.Circuit);
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    CHECK_THAT(run.Result->NodeVoltages[2], Catch::Matchers::WithinAbs(5.1, 0.05));
    // The breakdown current flows from cathode to anode, against the forward direction
    CHECK(FindCurrent(run.Result->Currents, "D1").Current < 0.0);
    // Breakdown is how a Zener works, so it is not a problem
    CHECK(run.Result->Warnings.empty());
}

TEST_CASE("Each LED model drops its rated forward voltage at 20 mA", "[simulator]") {
    struct Rating {
        const char *Model;
        double ForwardVoltage;
    };
    const Rating rating = GENERATE(Rating{"Red", 1.8}, Rating{"Green", 2.1}, Rating{"Blue", 3.0});
    CAPTURE(rating.Model);
    // The resistor sets 20 mA when the LED drops its rated voltage
    DiodeLoop loop(Core::ComponentType::LED, 5.0, (5.0 - rating.ForwardVoltage) / 20e-3);
    const Core::DiodeModel *model = Core::FindDiodeModel(Core::ComponentType::LED, rating.Model);
    REQUIRE(model != nullptr);
    loop.Part.SetModel(*model);

    const Core::OperatingPointRun run = Core::RunOperatingPoint(loop.Circuit);
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    CHECK_THAT(run.Result->NodeVoltages[2], Catch::Matchers::WithinAbs(rating.ForwardVoltage, 0.02));
}

TEST_CASE("A half-wave rectifier passes only the positive half of a sine", "[simulator]") {
    Core::VoltageSource source;
    source.SetName("Vin1");
    source.SetSourceType(Core::VoltageSource::SourceType::AC);
    source.SetAC({.Amplitude = 5.0, .Frequency = 1e3, .Offset = 0.0});
    Core::Diode rectifier(Core::ComponentType::Diode);
    rectifier.SetName("D1");
    rectifier.SetModel(*Core::FindDiodeModel(Core::ComponentType::Diode, "1N4007"));
    Core::Resistor load;
    load.SetName("R1");
    const Core::Ground reference;
    Core::Circuit circuit;
    circuit.Add(source, {1, 0});
    circuit.Add(rectifier, {1, 2});
    circuit.Add(load, {2, 0});
    circuit.Add(reference, {0});

    const Core::TransientRun run = Core::RunTransient(circuit, {.StopTime = 2e-3, .TimeStep = 1e-6});
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    const std::vector<double> &output = run.Result->NodeVoltages[2];
    // Only leakage reaches the load in the negative half; the positive peak loses one forward drop
    CHECK(std::ranges::min(output) > -1e-3);
    CHECK(std::ranges::max(output) > 4.0);
    CHECK(std::ranges::max(output) < 4.5);
    CHECK(run.Result->Warnings.empty());
    const std::vector<double> &diode_current = FindCurrent(run.Result->Currents, "D1").Values;
    const std::vector<double> &load_current = FindCurrent(run.Result->Currents, "R1").Values;
    for (std::size_t index = 0; index < diode_current.size(); ++index) {
        CHECK_THAT(diode_current[index], Catch::Matchers::WithinAbs(load_current[index], 1e-9));
    }
}

TEST_CASE("An AC sweep has the small-signal current of a biased diode", "[simulator]") {
    Core::VoltageSource source;
    source.SetName("Vin1");
    source.SetSourceType(Core::VoltageSource::SourceType::AC);
    // The offset biases the diode, so it conducts around its operating point
    source.SetAC({.Amplitude = 1.0, .Frequency = 1e3, .Offset = 5.0});
    Core::Resistor series;
    series.SetName("R1");
    Core::Diode diode(Core::ComponentType::Diode);
    diode.SetName("D1");
    const Core::Ground reference;
    Core::Circuit circuit;
    circuit.Add(source, {1, 0});
    circuit.Add(series, {1, 2});
    circuit.Add(diode, {2, 0});
    circuit.Add(reference, {0});

    const Core::ACSweepRun run = Core::RunACSweep(circuit, {.StartFrequency = 1e3, .StopFrequency = 1e6});
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    const std::vector<double> &diode_magnitudes = FindCurrent(run.Result->CurrentMagnitudesDecibels, "D1").Values;
    const std::vector<double> &diode_phases = FindCurrent(run.Result->CurrentPhasesDegrees, "D1").Values;
    const std::vector<double> &resistor_magnitudes = FindCurrent(run.Result->CurrentMagnitudesDecibels, "R1").Values;
    const std::vector<double> &resistor_phases = FindCurrent(run.Result->CurrentPhasesDegrees, "R1").Values;
    for (std::size_t index = 0; index < run.Result->Frequencies.size(); ++index) {
        // Nearly all of the 1 mA per volt goes through the conducting diode, which is in series with the resistor
        CHECK(diode_magnitudes[index] > -61.0);
        CHECK_THAT(diode_magnitudes[index], Catch::Matchers::WithinAbs(resistor_magnitudes[index], 1e-6));
        CHECK_THAT(diode_phases[index], Catch::Matchers::WithinAbs(resistor_phases[index], 1e-6));
    }
}

TEST_CASE("A reversed LED past its rating is reported in reverse breakdown", "[simulator]") {
    Core::VCC supply;
    supply.SetName("V1");
    supply.SetValue(12.0);
    Core::Resistor series;
    series.SetName("R1");
    Core::Diode led(Core::ComponentType::LED);
    led.SetName("D1");
    const Core::Ground reference;
    Core::Circuit circuit;
    circuit.Add(supply, {1});
    circuit.Add(series, {1, 2});
    circuit.Add(led, {0, 2});
    circuit.Add(reference, {0});

    const Core::OperatingPointRun run = Core::RunOperatingPoint(circuit);
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    REQUIRE(run.Result->Warnings.size() == 1);
    CAPTURE(run.Result->Warnings[0]);
    CHECK(run.Result->Warnings[0].starts_with("D1 (Red) goes into reverse breakdown"));
    CHECK(run.Result->Warnings[0].ends_with("rated for 5V"));
}

TEST_CASE("A transient reports a diode that breaks down at any time point", "[simulator]") {
    Core::VoltageSource source;
    source.SetName("Vin1");
    source.SetSourceType(Core::VoltageSource::SourceType::AC);
    source.SetAC({.Amplitude = 150.0, .Frequency = 1e3, .Offset = 0.0});
    Core::Diode rectifier(Core::ComponentType::Diode);
    rectifier.SetName("D1");
    Core::Resistor load;
    load.SetName("R1");
    const Core::Ground reference;
    Core::Circuit circuit;
    circuit.Add(source, {1, 0});
    circuit.Add(rectifier, {1, 2});
    circuit.Add(load, {2, 0});
    circuit.Add(reference, {0});

    // The negative half of the sine puts 150 V across a 1N4148 rated for 100 V
    const Core::TransientRun run = Core::RunTransient(circuit, {.StopTime = 2e-3, .TimeStep = 1e-6});
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    REQUIRE(run.Result->Warnings.size() == 1);
    CAPTURE(run.Result->Warnings[0]);
    CHECK(run.Result->Warnings[0].starts_with("D1 (1N4148) goes into reverse breakdown"));
}

TEST_CASE("A custom Zener regulates at its own breakdown voltage", "[simulator]") {
    DiodeLoop loop(Core::ComponentType::ZenerDiode, 20.0, 1e3);
    loop.Part.SetCustom();
    Core::DiodeParameters parameters = loop.Part.GetParameters();
    parameters.BreakdownVoltage = 9.1;
    loop.Part.SetCustomParameters(parameters);

    const Core::OperatingPointRun run = Core::RunOperatingPoint(loop.Circuit);
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    CHECK_THAT(run.Result->NodeVoltages[2], Catch::Matchers::WithinAbs(9.1, 0.05));
}

TEST_CASE("The breakdown warning uses the voltage of a custom diode", "[simulator]") {
    // 12 V across a 1N4148 is fine, but not once its custom BV drops to 10 V
    DiodeLoop loop(Core::ComponentType::Diode, 12.0, 1e3);
    Core::Circuit reversed;
    reversed.Add(loop.Supply, {1});
    reversed.Add(loop.Series, {1, 2});
    reversed.Add(loop.Part, {0, 2});
    reversed.Add(loop.Reference, {0});

    const Core::OperatingPointRun rated = Core::RunOperatingPoint(reversed);
    if (!rated.Result) {
        FAIL(rated.Result.error());
    }
    CHECK(rated.Result->Warnings.empty());

    Core::DiodeParameters parameters = loop.Part.GetParameters();
    parameters.BreakdownVoltage = 10.0;
    loop.Part.SetCustomParameters(parameters);
    const Core::OperatingPointRun custom = Core::RunOperatingPoint(reversed);
    if (!custom.Result) {
        FAIL(custom.Result.error());
    }
    REQUIRE(custom.Result->Warnings.size() == 1);
    CAPTURE(custom.Result->Warnings[0]);
    CHECK(custom.Result->Warnings[0].starts_with("D1 (Custom) goes into reverse breakdown"));
    CHECK(custom.Result->Warnings[0].ends_with("rated for 10V"));
}

TEST_CASE("A common-emitter NPN amplifies its base current", "[simulator]") {
    const CommonEmitter stage(Core::ComponentType::NPN);
    const Core::OperatingPointRun run = Core::RunOperatingPoint(stage.Circuit);
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    const std::vector<Core::ComponentCurrent> &currents = run.Result->Currents;
    const double collector = FindCurrent(currents, "Q1.C").Current;
    const double base = FindCurrent(currents, "Q1.B").Current;
    const double emitter = FindCurrent(currents, "Q1.E").Current;
    // Currents flow into the collector and base and out of the emitter, and add up to zero
    CHECK(collector > 0.0);
    CHECK(base > 0.0);
    // ngspice converges to within a picoampere, so the sum is only that close to zero
    CHECK_THAT(collector + base + emitter, Catch::Matchers::WithinAbs(0.0, 1e-9));
    CHECK_THAT(collector, Catch::Matchers::WithinRel(FindCurrent(currents, "R2").Current, 1e-6));
    CHECK_THAT(base, Catch::Matchers::WithinRel(FindCurrent(currents, "R1").Current, 1e-6));
    // The 2N3904 gain at a few milliamperes, in its datasheet range
    CHECK(collector / base > 100.0);
    CHECK(collector / base < 300.0);
    // Active region: the collector stays above the base
    CHECK(run.Result->NodeVoltages[3] > run.Result->NodeVoltages[2]);
}

TEST_CASE("A PNP mirrors the voltages and currents of the NPN stage", "[simulator]") {
    const CommonEmitter stage(Core::ComponentType::PNP);
    const Core::OperatingPointRun run = Core::RunOperatingPoint(stage.Circuit);
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    const std::vector<Core::ComponentCurrent> &currents = run.Result->Currents;
    const double collector = FindCurrent(currents, "Q1.C").Current;
    const double base = FindCurrent(currents, "Q1.B").Current;
    CHECK(collector < 0.0);
    CHECK(base < 0.0);
    CHECK(FindCurrent(currents, "Q1.E").Current > 0.0);
    CHECK(collector / base > 100.0);
    CHECK(run.Result->NodeVoltages[3] < run.Result->NodeVoltages[2]);
}

TEST_CASE("Each bipolar model has its datasheet gain", "[simulator]") {
    struct Rating {
        Core::ComponentType Type;
        const char *Model;
        double MinimumGain;
        double MaximumGain;
    };
    const Rating rating = GENERATE(Rating{Core::ComponentType::NPN, "2N3904", 100.0, 300.0},
                                   Rating{Core::ComponentType::NPN, "2N2222A", 100.0, 300.0},
                                   Rating{Core::ComponentType::NPN, "BC547B", 200.0, 450.0},
                                   Rating{Core::ComponentType::PNP, "2N3906", 100.0, 300.0},
                                   Rating{Core::ComponentType::PNP, "2N2907A", 100.0, 300.0},
                                   Rating{Core::ComponentType::PNP, "BC557B", 220.0, 475.0});
    CAPTURE(rating.Model);
    CommonEmitter stage(rating.Type);
    stage.Transistor.SetModel(*Core::FindBJTModel(rating.Type, rating.Model));
    // 47k sets a base current near 0.24 mA, so the collector runs at a few tens of milliamperes
    stage.BaseResistor.SetValue(47e3);
    stage.CollectorResistor.SetValue(10.0);

    const Core::OperatingPointRun run = Core::RunOperatingPoint(stage.Circuit);
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    const double gain =
        FindCurrent(run.Result->Currents, "Q1.C").Current / FindCurrent(run.Result->Currents, "Q1.B").Current;
    CHECK(gain > rating.MinimumGain);
    CHECK(gain < rating.MaximumGain);
}

TEST_CASE("An NMOS in saturation follows the level 1 square law", "[simulator]") {
    // 3 V on the gate of a 2N7000 (VTO = 2.1 V) sets about 30 mA; through 10 Ohm the drain stays near 5 V, well
    // above the 0.9 V overdrive, so the channel is saturated
    LowSideSwitch stage(Core::ComponentType::NMOS, 3.0);
    stage.Load.SetValue(10.0);
    const Core::OperatingPointRun run = Core::RunOperatingPoint(stage.Circuit);
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    const Core::MOSFETParameters &model = stage.Transistor.GetParameters();
    const double drain_voltage = run.Result->NodeVoltages[2];
    const double overdrive = 3.0 - model.ThresholdVoltage;
    REQUIRE(drain_voltage > overdrive);
    const double expected =
        model.Transconductance / 2.0 * overdrive * overdrive * (1.0 + model.ChannelModulation * drain_voltage);
    const std::vector<Core::ComponentCurrent> &currents = run.Result->Currents;
    CHECK_THAT(FindCurrent(currents, "M1.D").Current, Catch::Matchers::WithinRel(expected, 1e-4));
    CHECK_THAT(FindCurrent(currents, "M1.G").Current, Catch::Matchers::WithinAbs(0.0, 1e-12));
    CHECK_THAT(FindCurrent(currents, "M1.S").Current, Catch::Matchers::WithinRel(-expected, 1e-4));
}

TEST_CASE("Each MOSFET model has its datasheet on-resistance at 10 V of gate drive", "[simulator]") {
    struct Rating {
        Core::ComponentType Type;
        const char *Model;
        double OnResistance;
    };
    const Rating rating =
        GENERATE(Rating{Core::ComponentType::NMOS, "2N7000", 1.8}, Rating{Core::ComponentType::NMOS, "BS170", 1.2},
                 Rating{Core::ComponentType::NMOS, "IRF540N", 0.044}, Rating{Core::ComponentType::PMOS, "BS250", 14.0},
                 Rating{Core::ComponentType::PMOS, "IRF9540N", 0.117});
    CAPTURE(rating.Model);
    LowSideSwitch stage(rating.Type, 10.0);
    stage.Transistor.SetModel(*Core::FindMOSFETModel(rating.Type, rating.Model));

    const Core::OperatingPointRun run = Core::RunOperatingPoint(stage.Circuit);
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    // The drain sits close to the source, so the channel is in its linear region
    const double resistance = run.Result->NodeVoltages[2] / FindCurrent(run.Result->Currents, "M1.D").Current;
    CHECK_THAT(resistance, Catch::Matchers::WithinRel(rating.OnResistance, 0.05));
}

TEST_CASE("A PMOS high-side switch turns on when its gate is pulled low", "[simulator]") {
    Core::VCC supply;
    supply.SetName("V1");
    Core::VCC gate;
    gate.SetName("V2");
    Core::MOSFET transistor(Core::ComponentType::PMOS);
    transistor.SetName("M1");
    Core::Resistor load;
    load.SetName("R1");
    const Core::Ground reference;
    Core::Circuit circuit;
    circuit.Add(supply, {1});
    circuit.Add(gate, {2});
    circuit.Add(transistor, {3, 2, 1});
    circuit.Add(load, {3, 0});
    circuit.Add(reference, {0});

    gate.SetValue(0.0);
    const Core::OperatingPointRun on = Core::RunOperatingPoint(circuit);
    if (!on.Result) {
        FAIL(on.Result.error());
    }
    CHECK(on.Result->NodeVoltages[3] > 4.5);

    gate.SetValue(5.0);
    const Core::OperatingPointRun off = Core::RunOperatingPoint(circuit);
    if (!off.Result) {
        FAIL(off.Result.error());
    }
    CHECK(off.Result->NodeVoltages[3] < 0.01);
}

TEST_CASE("A transient keeps the transistor terminal currents balanced", "[simulator]") {
    CommonEmitter stage(Core::ComponentType::NPN);
    Core::VoltageSource input;
    input.SetName("Vin1");
    input.SetSourceType(Core::VoltageSource::SourceType::AC);
    input.SetAC({.Amplitude = 10e-3, .Frequency = 1e3, .Offset = 0.0});
    Core::Capacitor coupling;
    coupling.SetName("C1");
    coupling.SetValue(10e-6);
    stage.Circuit.Add(input, {4, 0});
    stage.Circuit.Add(coupling, {4, 2});

    const Core::TransientRun run = Core::RunTransient(stage.Circuit, {.StopTime = 2e-3, .TimeStep = 10e-6});
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    const std::vector<double> &collector = FindCurrent(run.Result->Currents, "Q1.C").Values;
    const std::vector<double> &base = FindCurrent(run.Result->Currents, "Q1.B").Values;
    const std::vector<double> &emitter = FindCurrent(run.Result->Currents, "Q1.E").Values;
    for (std::size_t index = 0; index < collector.size(); ++index) {
        CHECK_THAT(collector[index] + base[index] + emitter[index], Catch::Matchers::WithinAbs(0.0, 1e-9));
    }
    // The input sine shows up amplified and inverted on the collector
    const auto [lowest, highest] = std::ranges::minmax(run.Result->NodeVoltages[3]);
    CHECK(highest - lowest > 0.5);
}

TEST_CASE("An AC sweep gives the small-signal gain and currents of a common-emitter stage", "[simulator]") {
    CommonEmitter stage(Core::ComponentType::NPN);
    // Without base and collector resistance, high injection and leakage, the gain is the textbook gm * RC
    Core::BJTParameters ideal = stage.Transistor.GetParameters();
    ideal.BaseResistance = 0.0;
    ideal.CollectorResistance = 0.0;
    ideal.ForwardKneeCurrent = 0.0;
    ideal.LeakageSaturationCurrent = 0.0;
    stage.Transistor.SetCustomParameters(ideal);
    const Core::OperatingPointRun bias = Core::RunOperatingPoint(stage.Circuit);
    if (!bias.Result) {
        FAIL(bias.Result.error());
    }
    const double collector_current = FindCurrent(bias.Result->Currents, "Q1.C").Current;

    Core::VoltageSource input;
    input.SetName("Vin1");
    input.SetSourceType(Core::VoltageSource::SourceType::AC);
    input.SetAC({.Amplitude = 1e-3, .Frequency = 1e3, .Offset = 0.0});
    Core::Capacitor coupling;
    coupling.SetName("C1");
    coupling.SetValue(100e-6);
    stage.Circuit.Add(input, {4, 0});
    stage.Circuit.Add(coupling, {4, 2});

    const Core::ACSweepRun run = Core::RunACSweep(stage.Circuit, {.StartFrequency = 1e3, .StopFrequency = 1e4});
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    const Core::ACSweep &sweep = *run.Result;
    // Gain from base to collector is gm * RC, with gm = IC / VT, reduced a little by the Early effect output
    // resistance in parallel with RC
    const double thermal_voltage = 0.025865;
    const double early_voltage = stage.Transistor.GetParameters().EarlyVoltage;
    const double output_resistance = early_voltage / collector_current;
    const double load = 1e3 * output_resistance / (1e3 + output_resistance);
    const double expected_gain = collector_current / thermal_voltage * load;
    for (std::size_t index = 0; index < sweep.Frequencies.size(); ++index) {
        const double gain_decibels = sweep.NodeMagnitudesDecibels[3][index] - sweep.NodeMagnitudesDecibels[2][index];
        CHECK_THAT(std::pow(10.0, gain_decibels / 20.0), Catch::Matchers::WithinRel(expected_gain, 0.02));

        const auto current = [&](const std::string &name) {
            return FromDecibels(FindCurrent(sweep.CurrentMagnitudesDecibels, name).Values[index],
                                FindCurrent(sweep.CurrentPhasesDegrees, name).Values[index]);
        };
        CHECK(std::abs(current("Q1.C") + current("Q1.B") + current("Q1.E")) < 1e-9);
        CHECK(std::abs(current("Q1.C") - current("R2")) < 1e-9);
    }
}

// True when some warning starts with the text, so tests can check one rating at a time
bool HasWarning(const std::vector<std::string> &warnings, const std::string &start) {
    return std::ranges::any_of(warnings, [&start](const std::string &warning) { return warning.starts_with(start); });
}

TEST_CASE("A BJT above its VCEO rating is reported", "[simulator]") {
    // A small base current keeps the 2N3904 barely on, so nearly the whole 60 V supply sits across it
    CommonEmitter stage(Core::ComponentType::NPN);
    stage.Supply.SetValue(60.0);
    stage.BaseResistor.SetValue(10e6);
    const Core::OperatingPointRun run = Core::RunOperatingPoint(stage.Circuit);
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    REQUIRE(run.Result->Warnings.size() == 1);
    CAPTURE(run.Result->Warnings[0]);
    CHECK(run.Result->Warnings[0].starts_with("Q1 (2N3904) exceeds its VCE rating: "));
    CHECK(run.Result->Warnings[0].ends_with("against 40V"));
}

TEST_CASE("A BJT above its collector current and power ratings is reported", "[simulator]") {
    // About 11 mA into the base drives the 2N3904 past 200 mA, with several volts left across it
    CommonEmitter stage(Core::ComponentType::NPN);
    stage.BaseResistor.SetValue(1e3);
    stage.CollectorResistor.SetValue(10.0);
    const Core::OperatingPointRun run = Core::RunOperatingPoint(stage.Circuit);
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    CAPTURE(run.Result->Warnings);
    CHECK(HasWarning(run.Result->Warnings, "Q1 (2N3904) exceeds its IC rating"));
    CHECK(HasWarning(run.Result->Warnings, "Q1 (2N3904) exceeds its power rating"));
    CHECK_FALSE(HasWarning(run.Result->Warnings, "Q1 (2N3904) exceeds its VCE rating"));
}

TEST_CASE("A MOSFET above its power rating is reported, with its other ratings respected", "[simulator]") {
    // About 70 mA with nearly 20 V across the 2N7000 is about 1.4 W, over its 400 mW
    LowSideSwitch stage(Core::ComponentType::NMOS, 3.5);
    stage.Supply.SetValue(20.0);
    stage.Load.SetValue(10.0);
    const Core::OperatingPointRun run = Core::RunOperatingPoint(stage.Circuit);
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    REQUIRE(run.Result->Warnings.size() == 1);
    CAPTURE(run.Result->Warnings[0]);
    CHECK(run.Result->Warnings[0].starts_with("M1 (2N7000) exceeds its power rating: "));
    CHECK(run.Result->Warnings[0].ends_with("against 400mW"));
}

TEST_CASE("A MOSFET gate above its VGS rating is reported", "[simulator]") {
    LowSideSwitch stage(Core::ComponentType::PMOS, 25.0);
    const Core::OperatingPointRun run = Core::RunOperatingPoint(stage.Circuit);
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    CAPTURE(run.Result->Warnings);
    CHECK(HasWarning(run.Result->Warnings, "M1 (BS250) exceeds its VGS rating: 25V against 20V"));
}

TEST_CASE("A transient checks the average power, so short pulses above the rating pass", "[simulator]") {
    // The same 1.4 W as the operating point test, but only during a tenth of every period
    Core::VCC supply;
    supply.SetName("V1");
    supply.SetValue(20.0);
    Core::VoltageSource gate;
    gate.SetName("Vin1");
    gate.SetSourceType(Core::VoltageSource::SourceType::Pulse);
    gate.SetPulse({.Low = 0.0, .High = 3.5, .Width = 0.1e-3, .Period = 1e-3});
    Core::Resistor load;
    load.SetName("R1");
    load.SetValue(10.0);
    Core::MOSFET transistor(Core::ComponentType::NMOS);
    transistor.SetName("M1");
    const Core::Ground reference;
    Core::Circuit circuit;
    circuit.Add(supply, {1});
    circuit.Add(gate, {3, 0});
    circuit.Add(load, {1, 2});
    circuit.Add(transistor, {2, 3, 0});
    circuit.Add(reference, {0});

    const Core::TransientRun pulsed = Core::RunTransient(circuit, {.StopTime = 5e-3, .TimeStep = 1e-6});
    if (!pulsed.Result) {
        FAIL(pulsed.Result.error());
    }
    CAPTURE(pulsed.Result->Warnings);
    CHECK(pulsed.Result->Warnings.empty());
    // With picofarads of gate capacitance the drain current settles within microseconds of each gate edge
    const std::vector<double> &times = pulsed.Result->Times;
    const std::vector<double> &drain = FindCurrent(pulsed.Result->Currents, "M1.D").Values;
    const auto current_at = [&](const double time) {
        return drain[static_cast<std::size_t>(std::ranges::lower_bound(times, time) - times.begin())];
    };
    CHECK_THAT(current_at(10e-6), Catch::Matchers::WithinRel(current_at(95e-6), 0.01));

    gate.SetPulse({.Low = 0.0, .High = 3.5, .Width = 0.9e-3, .Period = 1e-3});
    const Core::TransientRun mostly_on = Core::RunTransient(circuit, {.StopTime = 5e-3, .TimeStep = 1e-6});
    if (!mostly_on.Result) {
        FAIL(mostly_on.Result.error());
    }
    CAPTURE(mostly_on.Result->Warnings);
    CHECK(HasWarning(mostly_on.Result->Warnings, "M1 (2N7000) exceeds its power rating"));
}

TEST_CASE("The rating warnings use the ratings of a custom transistor", "[simulator]") {
    CommonEmitter stage(Core::ComponentType::NPN);
    Core::BJTParameters parameters = stage.Transistor.GetParameters();
    parameters.MaxCollectorEmitterVoltage = 5.0;
    stage.Transistor.SetCustomParameters(parameters);
    const Core::OperatingPointRun run = Core::RunOperatingPoint(stage.Circuit);
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    CAPTURE(run.Result->Warnings);
    CHECK(HasWarning(run.Result->Warnings, "Q1 (Custom) exceeds its VCE rating"));
}
