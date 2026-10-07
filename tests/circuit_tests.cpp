/**
 * @file    circuit_tests.cpp
 * @brief   Tests for the circuit topology and its SPICE netlist.
 */

#include "circuit.h"
#include "components/capacitor.h"
#include "components/current_source.h"
#include "components/diode.h"
#include "components/ground.h"
#include "components/inductor.h"
#include "components/resistor.h"
#include "components/vcc.h"
#include "components/voltage_source.h"
#include <catch2/catch_test_macros.hpp>
#include <string>

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

TEST_CASE("ToSpiceNetlist writes diodes from anode to cathode and each model once", "[circuit]") {
    Core::Diode first(Core::ComponentType::Diode);
    first.SetName("D1");
    Core::Diode second(Core::ComponentType::Diode);
    second.SetName("D2");
    Core::Diode led(Core::ComponentType::LED);
    led.SetName("D3");
    Core::Circuit circuit;
    circuit.Add(first, {1, 2});
    circuit.Add(second, {2, 0});
    circuit.Add(led, {1, 0});
    CHECK(circuit.ToSpiceNetlist(".op") == "* imcsim netlist\n"
                                           "D1 1 2 D1N4148\n"
                                           "D2 2 0 D1N4148\n"
                                           "D3 1 0 DLEDRED\n"
                                           ".model D1N4148 D(IS=2.52e-09 N=1.752 RS=0.568 BV=100 IBV=0.0001 "
                                           "CJO=4e-12 VJ=1 M=0.4 TT=2e-08)\n"
                                           ".model DLEDRED D(IS=3.3e-17 N=2 RS=2 BV=5 IBV=1e-05 CJO=4e-11 VJ=1 M=0.5 "
                                           "TT=0)\n"
                                           ".op\n"
                                           ".end\n");
}

TEST_CASE("ToSpiceNetlist puts a probe source after each diode only when asked", "[circuit]") {
    Core::Diode first(Core::ComponentType::ZenerDiode);
    first.SetName("D1");
    Core::Diode second(Core::ComponentType::ZenerDiode);
    second.SetName("Dclamp");
    Core::Circuit circuit;
    circuit.Add(first, {0, 1});
    circuit.Add(second, {2, 1});

    const std::string model = ".model DZ5V1 D(IS=1e-09 N=1.5 RS=1 BV=5.1 IBV=0.005 CJO=1e-10 VJ=1 M=0.5 TT=0)\n";
    CHECK(circuit.ToSpiceNetlist() == "* imcsim netlist\nD1 0 1 DZ5V1\nDclamp 2 1 DZ5V1\n" + model + ".end\n");
    // Probe nodes come after the circuit nodes, which keep their numbers
    CHECK(circuit.ToSpiceNetlist("", true) == "* imcsim netlist\n"
                                              "D1 0 3 DZ5V1\n"
                                              "Vprobe-D1 3 1 DC 0\n"
                                              "Dclamp 2 4 DZ5V1\n"
                                              "Vprobe-Dclamp 4 1 DC 0\n" +
                                                  model + ".end\n");
    CHECK(Core::GetCurrentProbeName(second) == "Vprobe-Dclamp");
}

TEST_CASE("ToSpiceNetlist gives each custom diode a model of its own", "[circuit]") {
    Core::Diode first(Core::ComponentType::Diode);
    first.SetName("D1");
    first.SetCustomParameters({.SaturationCurrent = 1e-12, .BreakdownVoltage = 50.0});
    Core::Diode second(Core::ComponentType::Diode);
    second.SetName("D2");
    second.SetCustomParameters({.SaturationCurrent = 1e-12, .BreakdownVoltage = 50.0});
    Core::Circuit circuit;
    circuit.Add(first, {1, 0});
    circuit.Add(second, {2, 0});
    CHECK(circuit.ToSpiceNetlist() == "* imcsim netlist\n"
                                      "D1 1 0 DCUSTOM_D1\n"
                                      "D2 2 0 DCUSTOM_D2\n"
                                      ".model DCUSTOM_D1 D(IS=1e-12 N=1 RS=0 BV=50 IBV=0.001 CJO=0 VJ=1 M=0.5 TT=0)\n"
                                      ".model DCUSTOM_D2 D(IS=1e-12 N=1 RS=0 BV=50 IBV=0.001 CJO=0 VJ=1 M=0.5 TT=0)\n"
                                      ".end\n");
}
