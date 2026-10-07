/**
 * @file    simulator_tests.cpp
 * @brief   Tests for the operating point simulation, run through the real ngspice library.
 */

#include "circuit.h"
#include "components/capacitor.h"
#include "components/current_source.h"
#include "components/diode.h"
#include "components/ground.h"
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
