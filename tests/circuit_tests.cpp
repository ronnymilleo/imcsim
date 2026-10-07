/**
 * @file    circuit_tests.cpp
 * @brief   Tests for the circuit topology and its SPICE netlist.
 */

#include "circuit.h"
#include "components/capacitor.h"
#include "components/ground.h"
#include "components/inductor.h"
#include "components/resistor.h"
#include "components/vcc.h"
#include "components/voltage_source.h"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Circuit counts nodes including ground", "[circuit]") {
    Core::Circuit circuit;
    CHECK(circuit.GetNodeCount() == 0);

    const Core::Resistor resistor;
    circuit.Add(resistor, {2, 0});
    CHECK(circuit.GetNodeCount() == 3);
    REQUIRE(circuit.GetEntries().size() == 1);
    CHECK(circuit.GetEntries()[0].Part == &resistor);
}

TEST_CASE("ToSpiceNetlist writes one line per component and leaves ground out", "[circuit]") {
    Core::VCC supply;
    supply.SetName("V1");
    Core::Resistor resistor;
    resistor.SetName("R1");
    resistor.SetValue(2.2e6);
    Core::Capacitor capacitor;
    capacitor.SetName("C1");
    capacitor.SetValue(100e-9);
    Core::Inductor inductor;
    inductor.SetName("L1");
    const Core::Ground ground;

    Core::Circuit circuit;
    circuit.Add(supply, {1});
    circuit.Add(resistor, {1, 2});
    circuit.Add(capacitor, {2, 0});
    circuit.Add(inductor, {2, 0});
    circuit.Add(ground, {0});

    CHECK(circuit.ToSpiceNetlist() == "* imcsim netlist\n"
                                      "V1 1 0 DC 5\n"
                                      "R1 1 2 2.2Meg\n"
                                      "C1 2 0 100n\n"
                                      "L1 2 0 1m\n"
                                      ".end\n");
}

TEST_CASE("ToSpiceNetlist writes a voltage source between its two nodes, positive first", "[circuit]") {
    Core::VoltageSource source;
    source.SetName("Vin1");
    source.SetValue(12.0);

    Core::Circuit circuit;
    circuit.Add(source, {2, 1});

    CHECK(circuit.ToSpiceNetlist() == "* imcsim netlist\n"
                                      "Vin1 2 1 DC 12\n"
                                      ".end\n");
}

TEST_CASE("ToSpiceNetlist writes the analysis right before .end", "[circuit]") {
    Core::Resistor resistor;
    resistor.SetName("R1");
    Core::Circuit circuit;
    circuit.Add(resistor, {1, 0});
    CHECK(circuit.ToSpiceNetlist(".op") == "* imcsim netlist\n"
                                           "R1 1 0 1k\n"
                                           ".op\n"
                                           ".end\n");
}

TEST_CASE("HasGround looks for a ground component", "[circuit]") {
    Core::Resistor resistor;
    const Core::Ground ground;
    Core::Circuit circuit;
    circuit.Add(resistor, {1, 0});
    CHECK_FALSE(circuit.HasGround());
    circuit.Add(ground, {0});
    CHECK(circuit.HasGround());
}
