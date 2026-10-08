/**
 * @file    circuit_tests.cpp
 * @brief   Tests for the circuit topology and its SPICE netlist.
 */

#include "circuit.h"
#include "components/bjt.h"
#include "components/capacitor.h"
#include "components/controlled_source.h"
#include "components/current_source.h"
#include "components/diode.h"
#include "components/ground.h"
#include "components/inductor.h"
#include "components/mosfet.h"
#include "components/op_amp.h"
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
    source.SetAC({.Amplitude = 2.0, .Frequency = 50.0, .Offset = 0.5, .Magnitude = 0.25});

    Core::Circuit circuit;
    circuit.Add(source, {1, 0});

    // The AC sweep magnitude is apart from the amplitude of the sine
    CHECK(circuit.ToSpiceNetlist() == "* imcsim netlist\n"
                                      "Vin1 1 0 DC 500m AC 250m SIN(500m 2 50)\n"
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
                                      "I1 0 3 DC 0 AC 1 SIN(0 1m 1k)\n"
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

TEST_CASE("ToSpiceNetlist puts a probe source on each diode anode only when asked", "[circuit]") {
    Core::Diode first(Core::ComponentType::ZenerDiode);
    first.SetName("D1");
    Core::Diode second(Core::ComponentType::ZenerDiode);
    second.SetName("Dclamp");
    Core::Circuit circuit;
    circuit.Add(first, {0, 1});
    circuit.Add(second, {2, 1});

    const std::string model = ".model DZ5V1 D(IS=1e-09 N=1.5 RS=1 BV=5.1 IBV=0.005 CJO=1e-10 VJ=1 M=0.5 TT=0)\n";
    CHECK(circuit.ToSpiceNetlist() == "* imcsim netlist\nD1 0 1 DZ5V1\nDclamp 2 1 DZ5V1\n" + model + ".end\n");
    // Probe nodes come after the circuit nodes, which keep their numbers; the current flows from the circuit node
    // into the anode
    CHECK(circuit.ToSpiceNetlist("", true) == "* imcsim netlist\n"
                                              "D1 3 1 DZ5V1\n"
                                              "Vprobe-D1 0 3 DC 0\n"
                                              "Dclamp 4 1 DZ5V1\n"
                                              "Vprobe-Dclamp 2 4 DC 0\n" +
                                                  model + ".end\n");
    CHECK(Core::GetCurrentProbeName(second, "") == "Vprobe-Dclamp");
    CHECK(Core::GetCurrentName(second, "") == "Dclamp");
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

TEST_CASE("ToSpiceNetlist writes transistors in SPICE terminal order with the MOSFET body on its source", "[circuit]") {
    Core::BJT npn(Core::ComponentType::NPN);
    npn.SetName("Q1");
    Core::MOSFET nmos(Core::ComponentType::NMOS);
    nmos.SetName("M1");
    Core::Circuit circuit;
    circuit.Add(npn, {1, 2, 0});
    circuit.Add(nmos, {3, 2, 0});
    CHECK(circuit.ToSpiceNetlist() ==
          "* imcsim netlist\n"
          "Q1 1 2 0 Q2N3904\n"
          "M1 3 2 0 0 M2N7000 W=0.0001 L=0.0001\n"
          ".model Q2N3904 NPN(IS=6.734e-15 BF=416.4 BR=0.7371 VAF=74.03 IKF=0.06678 ISE=6.734e-15 NE=1.259 RB=10 "
          "RC=1 RE=0 CJE=4.493e-12 CJC=3.638e-12 TF=3.012e-10)\n"
          ".model M2N7000 NMOS(LEVEL=1 VTO=2.1 KP=0.0703 LAMBDA=0.01 RD=0 RS=0 CGSO=2e-07 CGDO=5e-08)\n"
          ".end\n");
}

TEST_CASE("ToSpiceNetlist probes every transistor terminal, with the current flowing into it", "[circuit]") {
    Core::BJT pnp(Core::ComponentType::PNP);
    pnp.SetName("Q1");
    Core::MOSFET pmos(Core::ComponentType::PMOS);
    pmos.SetName("M1");
    Core::Circuit circuit;
    circuit.Add(pnp, {1, 2, 0});
    circuit.Add(pmos, {1, 2, 0});
    const std::string netlist = circuit.ToSpiceNetlist("", true);
    CHECK(netlist.contains("Q1 3 4 5 Q2N3906\nVprobe-Q1-C 1 3 DC 0\nVprobe-Q1-B 2 4 DC 0\nVprobe-Q1-E 0 5 DC 0\n"));
    CHECK(netlist.contains("M1 6 7 8 8 MBS250 W=0.0001 L=0.0001\nVprobe-M1-D 1 6 DC 0\nVprobe-M1-G 2 7 DC 0\n"
                           "Vprobe-M1-S 0 8 DC 0\n"));
    CHECK(netlist.contains(".model Q2N3906 PNP("));
    CHECK(netlist.contains(".model MBS250 PMOS(LEVEL=1 VTO=-2 "));
    CHECK(Core::GetCurrentName(pnp, "C") == "Q1.C");
}

TEST_CASE("ToSpiceNetlist writes voltage-controlled sources with their control terminals after the output",
          "[circuit]") {
    Core::ControlledSource vcvs(Core::ComponentType::VCVS);
    vcvs.SetName("E1");
    Core::ControlledSource vccs(Core::ComponentType::VCCS);
    vccs.SetName("G1");
    vccs.SetValue(-2e-3);
    Core::Circuit circuit;
    circuit.Add(vcvs, {2, 0, 1, 0});
    circuit.Add(vccs, {3, 0, 2, 1});
    const std::string netlist = circuit.ToSpiceNetlist();
    CHECK(netlist.contains("E1 2 0 1 0 10\n"));
    CHECK(netlist.contains("G1 3 0 2 1 -2m\n"));
    // Their output current is probed, like a diode's, only when the simulator asks
    CHECK_FALSE(netlist.contains("Vprobe"));
    CHECK(circuit.ToSpiceNetlist("", true).contains("E1 4 0 1 0 10\nVprobe-E1 2 4 DC 0\n"));
}

TEST_CASE("ToSpiceNetlist gives the part a current-controlled source follows a voltage source in its path",
          "[circuit]") {
    Core::VoltageSource input;
    input.SetName("Vin1");
    Core::Resistor sense;
    sense.SetName("R1");
    Core::ControlledSource cccs(Core::ComponentType::CCCS);
    cccs.SetName("F1");
    cccs.SetControllingCurrent("R1");
    Core::ControlledSource ccvs(Core::ComponentType::CCVS);
    ccvs.SetName("H1");
    ccvs.SetControllingCurrent("Vin1");
    Core::Circuit circuit;
    circuit.Add(input, {1, 0});
    circuit.Add(sense, {1, 0});
    circuit.Add(cccs, {2, 0});
    circuit.Add(ccvs, {3, 0});

    // The probe is there even in the netlist the user reads, since the source refers to it
    const std::string netlist = circuit.ToSpiceNetlist();
    CHECK(netlist.contains("R1 4 0 1k\nVprobe-R1 1 4 DC 0\n"));
    CHECK(netlist.contains("F1 2 0 Vprobe-R1 10\n"));
    // A voltage source carries its own current
    CHECK(netlist.contains("H1 3 0 Vin1 1k\n"));
    CHECK_FALSE(netlist.contains("Vprobe-Vin1"));

    CHECK(Core::FindControllingSource(circuit, "R1") == "Vprobe-R1");
    CHECK(Core::FindControllingSource(circuit, "Vin1") == "Vin1");
    CHECK_FALSE(Core::FindControllingSource(circuit, "R2"));
}

TEST_CASE("A current-controlled source can follow a transistor terminal", "[circuit]") {
    Core::BJT npn(Core::ComponentType::NPN);
    npn.SetName("Q1");
    Core::ControlledSource cccs(Core::ComponentType::CCCS);
    cccs.SetName("F1");
    cccs.SetControllingCurrent("Q1.C");
    Core::Circuit circuit;
    circuit.Add(npn, {1, 2, 0});
    circuit.Add(cccs, {3, 0});
    const std::string netlist = circuit.ToSpiceNetlist();
    CHECK(netlist.contains("Vprobe-Q1-C 1 4 DC 0\n"));
    CHECK(netlist.contains("F1 3 0 Vprobe-Q1-C 10\n"));
}

TEST_CASE("ToSpiceNetlist writes an ideal op-amp as a gain stage with a pole, clamped to its supply pins",
          "[circuit]") {
    Core::OpAmp first;
    first.SetName("U1");
    Core::OpAmp second;
    second.SetName("U2");
    second.SetIdealBandwidth(10e6);
    Core::Circuit circuit;
    circuit.Add(first, {3, 1, 2, 4, 5});
    circuit.Add(second, {1, 0, 1, 4, 0});
    const std::string netlist = circuit.ToSpiceNetlist();
    // The gain stage takes the first node after the circuit's; 1 MHz over 100 dB puts the pole at 10 Hz
    CHECK(netlist.contains("G-U1 0 6 1 2 0.001\nR-U1 6 0 1e+08\nC-U1 6 0 1.59155e-10\n"
                           "D-U1-P 6 4 imcsim_opamp_clamp\nD-U1-N 5 6 imcsim_opamp_clamp\nE-U1 3 0 6 0 1\n"));
    CHECK(netlist.contains("C-U2 7 0 1.59155e-11\n"));
    // Both share one model
    CHECK(netlist.find(".model imcsim_opamp_clamp D(IS=1e-15 N=0.01)\n") == netlist.rfind(".model imcsim_opamp"));
}

TEST_CASE("ToSpiceNetlist writes a macromodel op-amp as an instance of its subcircuit", "[circuit]") {
    Core::OpAmp first;
    first.SetName("U1");
    first.SetModel(*Core::FindOpAmpModel(Core::ComponentType::OpAmp, "uA741"));
    Core::OpAmp second;
    second.SetName("U2");
    second.SetModel(*Core::FindOpAmpModel(Core::ComponentType::OpAmp, "uA741"));
    Core::OpAmp custom;
    custom.SetName("U3");
    custom.SetCustom();
    Core::Circuit circuit;
    circuit.Add(first, {3, 1, 2, 4, 5});
    circuit.Add(second, {6, 0, 6, 4, 5});
    circuit.Add(custom, {7, 0, 7, 4, 5});
    const std::string netlist = circuit.ToSpiceNetlist();
    CHECK(netlist.contains("X-U1 3 1 2 4 5 imcsim_uA741\n"));
    CHECK(netlist.contains("X-U3 7 0 7 4 5 imcsim_opamp_U3\n"));
    // One subcircuit per model, and one for the custom op-amp
    CHECK(netlist.find(".subckt imcsim_uA741 6 3 2 7 4\n") == netlist.rfind(".subckt imcsim_uA741"));
    CHECK(netlist.contains(".subckt imcsim_opamp_U3 6 3 2 7 4\n"));
    CHECK(netlist.contains(".ends imcsim_uA741\n"));
}
