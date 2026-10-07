/**
 * @file    simulator_tests.cpp
 * @brief   Tests for the operating point simulation, run through the real ngspice library.
 */

#include "circuit.h"
#include "components/capacitor.h"
#include "components/ground.h"
#include "components/resistor.h"
#include "components/vcc.h"
#include "components/voltage_source.h"
#include "simulator.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
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
    source.SetAmplitude(2.0);
    source.SetOffset(1.0);
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
    low_pass.Source.SetSourceType(Core::VoltageSource::SourceType::DC);
    const Core::ACSweepRun run = Core::RunACSweep(low_pass.Circuit, {});
    REQUIRE_FALSE(run.Result);
    CHECK(run.Result.error().find("AC source") != std::string::npos);
    CHECK(run.Messages.empty());
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
