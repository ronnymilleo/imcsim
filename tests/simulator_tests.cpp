/**
 * @file    simulator_tests.cpp
 * @brief   Tests for the operating point simulation, run through the real ngspice library.
 */

#include "circuit.h"
#include "components/capacitor.h"
#include "components/ground.h"
#include "components/resistor.h"
#include "components/vcc.h"
#include "simulator.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

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
