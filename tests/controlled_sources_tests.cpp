/**
 * @file    controlled_sources_tests.cpp
 * @brief   Theory checks for the Nodalys controlled sources: the VCVS, VCCS, CCCS and CCVS against
 *          hand-computed gains.
 */

#include "components/controlled_source.h"
#include "components/ground.h"
#include "components/resistor.h"
#include "components/vcc.h"
#include "model/circuit.h"
#include "nodalys/nodalys_backend.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

namespace {

using Catch::Matchers::WithinRel;

double CurrentOf(const Core::OperatingPoint &point, const std::string &name) {
    const auto found = std::ranges::find(point.Currents, name, &Core::ComponentCurrent::Name);
    REQUIRE(found != point.Currents.end());
    return found->Current;
}

} // namespace

TEST_CASE("A VCVS multiplies the controlling voltage") {
    Core::VCC supply;
    supply.SetName("V1");
    supply.SetValue(3.0);
    Core::Resistor divider;
    divider.SetName("R1");
    divider.SetValue(1e3);
    Core::ControlledSource amplifier(Core::ComponentType::VCVS);
    amplifier.SetName("E1");
    amplifier.SetValue(2.0);
    Core::Resistor load;
    load.SetName("R2");
    load.SetValue(10e3);
    Core::Ground ground;
    Core::Circuit circuit;
    // Node 1 = 3 V; E1 drives node 2 from ground, sensing node 1 against ground; a load keeps it honest
    circuit.Add(supply, {1});
    circuit.Add(divider, {1, 0});
    circuit.Add(amplifier, {2, 0, 1, 0});
    circuit.Add(load, {2, 0});
    circuit.Add(ground, {0});

    Core::NodalysBackend backend;
    const Core::OperatingPointRun run = backend.RunOperatingPoint(circuit, std::nullopt);
    REQUIRE(run.Result.has_value());
    REQUIRE_THAT(run.Result->NodeVoltages[2], WithinRel(6.0, 1e-5));
}

TEST_CASE("A VCCS drives a current proportional to the controlling voltage") {
    Core::VCC supply;
    supply.SetName("V1");
    supply.SetValue(2.0);
    Core::ControlledSource transconductance(Core::ComponentType::VCCS);
    transconductance.SetName("G1");
    transconductance.SetValue(0.01);
    Core::Resistor load;
    load.SetName("R1");
    load.SetValue(1e3);
    Core::Ground ground;
    Core::Circuit circuit;
    // G1 pulls node 2 down from ground, sensing node 1: v(2) = −gm·v(1)·R
    circuit.Add(supply, {1});
    circuit.Add(transconductance, {2, 0, 1, 0});
    circuit.Add(load, {2, 0});
    circuit.Add(ground, {0});

    Core::NodalysBackend backend;
    const Core::OperatingPointRun run = backend.RunOperatingPoint(circuit, std::nullopt);
    REQUIRE(run.Result.has_value());
    REQUIRE_THAT(run.Result->NodeVoltages[2], WithinRel(-20.0, 1e-5));
}

TEST_CASE("A CCCS follows the current of a sense resistor") {
    Core::VCC supply;
    supply.SetName("V1");
    supply.SetValue(5.0);
    Core::Resistor sense;
    sense.SetName("RS");
    sense.SetValue(100.0);
    Core::Resistor load;
    load.SetName("RL");
    load.SetValue(1e3);
    Core::ControlledSource current_amplifier(Core::ComponentType::CCCS);
    current_amplifier.SetName("F1");
    current_amplifier.SetValue(10.0);
    current_amplifier.SetControllingCurrent("RS");
    Core::Ground ground;
    Core::Circuit circuit;
    // Sense resistor from node 1 to ground carries 50 mA; F1 pushes 10x that through the load, downwards
    circuit.Add(supply, {1});
    circuit.Add(sense, {1, 0});
    circuit.Add(current_amplifier, {2, 0, 0, 0});
    circuit.Add(load, {2, 0});
    circuit.Add(ground, {0});

    Core::NodalysBackend backend;
    const Core::OperatingPointRun run = backend.RunOperatingPoint(circuit, std::nullopt);
    REQUIRE(run.Result.has_value());
    REQUIRE_THAT(CurrentOf(*run.Result, "RS"), WithinRel(50e-3, 1e-5));
    REQUIRE_THAT(run.Result->NodeVoltages[2], WithinRel(-500.0, 1e-5));
}

TEST_CASE("A CCVS converts a sense current to a voltage") {
    Core::VCC supply;
    supply.SetName("V1");
    supply.SetValue(4.0);
    Core::Resistor sense;
    sense.SetName("RS");
    sense.SetValue(200.0);
    Core::ControlledSource converter(Core::ComponentType::CCVS);
    converter.SetName("H1");
    converter.SetValue(1e3);
    converter.SetControllingCurrent("RS");
    Core::Resistor load;
    load.SetName("RL");
    load.SetValue(10e3);
    Core::Ground ground;
    Core::Circuit circuit;
    // RS carries 20 mA; H1 makes v(2) = 1k·20 mA = 20 V across the load
    circuit.Add(supply, {1});
    circuit.Add(sense, {1, 0});
    circuit.Add(converter, {2, 0, 0, 0});
    circuit.Add(load, {2, 0});
    circuit.Add(ground, {0});

    Core::NodalysBackend backend;
    const Core::OperatingPointRun run = backend.RunOperatingPoint(circuit, std::nullopt);
    REQUIRE(run.Result.has_value());
    REQUIRE_THAT(run.Result->NodeVoltages[2], WithinRel(20.0, 1e-5));
}
