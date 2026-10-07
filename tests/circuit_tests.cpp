/**
 * @file    circuit_tests.cpp
 * @brief   Tests for the circuit topology and its SPICE netlist.
 */

#include "circuit.h"
#include "components/capacitor.h"
#include "components/current_source.h"
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

TEST_CASE("ToSpiceNetlist writes an AC source for every analysis at once", "[circuit]") {
    Core::VoltageSource source;
    source.SetName("Vin1");
    source.SetSourceType(Core::VoltageSource::SourceType::AC);
    source.SetAC({.Amplitude = 2.0, .Frequency = 50.0, .Offset = 0.5});

    Core::Circuit circuit;
    circuit.Add(source, {1, 0});

    CHECK(circuit.ToSpiceNetlist() == "* imcsim netlist\n"
                                      "Vin1 1 0 DC 500m AC 2 SIN(500m 2 50)\n"
                                      ".end\n");
}

TEST_CASE("ToSpiceNetlist writes a pulse source starting at its low level", "[circuit]") {
    Core::VoltageSource source;
    source.SetName("Vin1");
    source.SetSourceType(Core::VoltageSource::SourceType::Pulse);
    source.SetPulse({.Low = -1.0,
                     .High = 3.3,
                     .Delay = 1e-3,
                     .RiseTime = 10e-9,
                     .FallTime = 20e-9,
                     .Width = 0.5e-3,
                     .Period = 2e-3});

    Core::Circuit circuit;
    circuit.Add(source, {1, 0});

    CHECK(circuit.ToSpiceNetlist() == "* imcsim netlist\n"
                                      "Vin1 1 0 DC -1 PULSE(-1 3.3 1m 10n 20n 500u 2m)\n"
                                      ".end\n");
}

TEST_CASE("ToSpiceNetlist writes a current source like a voltage source, under its own name", "[circuit]") {
    Core::CurrentSource source;
    source.SetName("I1");
    Core::Circuit circuit;
    circuit.Add(source, {0, 3});
    CHECK(circuit.ToSpiceNetlist() == "* imcsim netlist\n"
                                      "I1 0 3 DC 1m\n"
                                      ".end\n");

    source.SetSourceType(Core::CurrentSource::SourceType::AC);
    CHECK(circuit.ToSpiceNetlist() == "* imcsim netlist\n"
                                      "I1 0 3 DC 0 AC 1m SIN(0 1m 1k)\n"
                                      ".end\n");
}

TEST_CASE("HasACSource looks for a voltage source set to AC", "[circuit]") {
    Core::VoltageSource source;
    source.SetName("Vin1");
    Core::Circuit circuit;
    circuit.Add(source, {1, 0});
    CHECK_FALSE(circuit.HasACSource());

    source.SetSourceType(Core::VoltageSource::SourceType::Pulse);
    CHECK_FALSE(circuit.HasACSource());

    source.SetSourceType(Core::VoltageSource::SourceType::AC);
    CHECK(circuit.HasACSource());
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
