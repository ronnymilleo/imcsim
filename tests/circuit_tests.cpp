/**
 * @file    circuit_tests.cpp
 * @brief   Tests for the circuit topology and its SPICE netlist.
 */

#include "circuit.h"
#include "components/bjt.h"
#include "components/capacitor.h"
#include "components/clock.h"
#include "components/controlled_source.h"
#include "components/current_source.h"
#include "components/diode.h"
#include "components/ground.h"
#include "components/inductor.h"
#include "components/logic_input.h"
#include "components/logic_part.h"
#include "components/mosfet.h"
#include "components/op_amp.h"
#include "components/potentiometer.h"
#include "components/relay.h"
#include "components/resistor.h"
#include "components/switch.h"
#include "components/transformer.h"
#include "components/vcc.h"
#include "components/voltage_controlled_switch.h"
#include "components/voltage_source.h"
#include "simulator.h"
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

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

TEST_CASE("ToSpiceNetlist writes the initial condition of a capacitor and an inductor", "[circuit]") {
    Core::Capacitor capacitor;
    capacitor.SetName("C1");
    capacitor.SetValue(100e-9);
    capacitor.SetInitialCondition(5.0);
    Core::Inductor inductor;
    inductor.SetName("L1");
    inductor.SetInitialCondition(2e-3);

    Core::Circuit circuit;
    circuit.Add(capacitor, {2, 0});
    circuit.Add(inductor, {2, 0});

    CHECK(circuit.ToSpiceNetlist() == "* imcsim netlist\n"
                                      "C1 2 0 100n IC=5\n"
                                      "L1 2 0 1m IC=2m\n"
                                      ".end\n");
}

TEST_CASE("HasInitialConditions looks for a part with an initial condition", "[circuit]") {
    Core::Capacitor capacitor;
    capacitor.SetName("C1");
    Core::Circuit circuit;
    circuit.Add(capacitor, {1, 0});
    CHECK_FALSE(circuit.HasInitialConditions());

    capacitor.SetInitialCondition(3.0);
    CHECK(circuit.HasInitialConditions());

    capacitor.ClearInitialCondition();
    CHECK_FALSE(circuit.HasInitialConditions());
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

TEST_CASE("ToSpiceNetlist writes an exponential source starting from its first value", "[circuit]") {
    Core::VoltageSource source;
    source.SetName("Vin1");
    source.SetSourceType(Core::VoltageSource::SourceType::EXP);
    source.SetEXP({.InitialValue = -1.0,
                   .PeakValue = 3.3,
                   .RiseDelay = 1e-3,
                   .RiseTimeConstant = 10e-6,
                   .FallDelay = 5e-3,
                   .FallTimeConstant = 20e-6});

    Core::Circuit circuit;
    circuit.Add(source, {1, 0});

    CHECK(circuit.ToSpiceNetlist() == "* imcsim netlist\n"
                                      "Vin1 1 0 DC -1 EXP(-1 3.3 1m 10u 5m 20u)\n"
                                      ".end\n");
}

TEST_CASE("ToSpiceNetlist writes a single-frequency FM source from its offset", "[circuit]") {
    Core::VoltageSource source;
    source.SetName("Vin1");
    source.SetSourceType(Core::VoltageSource::SourceType::SFFM);
    source.SetSFFM(
        {.Offset = 0.5, .Amplitude = 2.0, .CarrierFrequency = 1e3, .ModulationIndex = 5.0, .SignalFrequency = 100.0});

    Core::Circuit circuit;
    circuit.Add(source, {1, 0});

    CHECK(circuit.ToSpiceNetlist() == "* imcsim netlist\n"
                                      "Vin1 1 0 DC 500m SFFM(500m 2 1k 5 100)\n"
                                      ".end\n");
}

TEST_CASE("ToSpiceNetlist writes an amplitude-modulated source with a suppressed carrier", "[circuit]") {
    Core::VoltageSource source;
    source.SetName("Vin1");
    source.SetSourceType(Core::VoltageSource::SourceType::AM);
    source.SetAM(
        {.Amplitude = 2.0, .Offset = 0.5, .ModulatingFrequency = 200.0, .CarrierFrequency = 10e3, .Delay = 1e-3});

    Core::Circuit circuit;
    circuit.Add(source, {1, 0});

    // The modulation offset of ngspice stays 0, so the offset sits on the two multiplied sines
    CHECK(circuit.ToSpiceNetlist() == "* imcsim netlist\n"
                                      "Vin1 1 0 DC 500m AM(500m 0 2 200 10k 1m)\n"
                                      ".end\n");
}

TEST_CASE("ToSpiceNetlist writes the piecewise linear points from its first value", "[circuit]") {
    Core::CurrentSource source;
    source.SetName("I1");
    source.SetSourceType(Core::CurrentSource::SourceType::PWL);
    source.SetPWL({.Points = {{0.0, 0.0}, {1e-3, 5e-3}, {2e-3, 0.0}}});

    Core::Circuit circuit;
    circuit.Add(source, {0, 3});

    CHECK(circuit.ToSpiceNetlist() == "* imcsim netlist\n"
                                      "I1 0 3 DC 0 PWL(0 0 1m 5m 2m 0)\n"
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

TEST_CASE("ToSpiceNetlist writes a clock as a pulse with its own short edges", "[circuit]") {
    Core::Clock clock;
    clock.SetName("V1");
    clock.SetParameters({.Frequency = 1e3, .DutyCycle = 0.25, .LogicLevel = 3.3, .StartDelay = 1e-3});
    Core::Circuit circuit;
    circuit.Add(clock, {1});
    // Period 1 ms, an edge of 1 us, and a flat top of 0.25 ms - 1 us = 249 us, so the duty reads as 0.25
    CHECK(circuit.ToSpiceNetlist() == "* imcsim netlist\n"
                                      "V1 1 0 DC 0 PULSE(0 3.3 1m 1u 1u 249u 1m)\n"
                                      ".end\n");
}

TEST_CASE("ToSpiceNetlist writes a logic input as a DC source to ground", "[circuit]") {
    Core::LogicInput input;
    input.SetName("V1");
    input.SetParameters({.LogicLevel = 3.3});
    Core::Circuit circuit;
    circuit.Add(input, {2});
    CHECK(circuit.ToSpiceNetlist() == "* imcsim netlist\n"
                                      "V1 2 0 DC 0\n"
                                      ".end\n");
    input.SetHigh(true);
    CHECK(circuit.ToSpiceNetlist() == "* imcsim netlist\n"
                                      "V1 2 0 DC 3.3\n"
                                      ".end\n");
}

TEST_CASE("A clock and a logic input carry their branch current for a controlled source", "[circuit]") {
    Core::Clock clock;
    clock.SetName("V1");
    Core::LogicInput input;
    input.SetName("V2");
    Core::ControlledSource cccs(Core::ComponentType::CCCS);
    cccs.SetName("F1");
    cccs.SetControllingCurrent("V1");
    Core::Circuit circuit;
    circuit.Add(clock, {1});
    circuit.Add(input, {2});
    circuit.Add(cccs, {3, 0});
    // Both are voltage sources, so ngspice follows their current with no probe
    const std::string netlist = circuit.ToSpiceNetlist();
    CHECK(netlist.contains("F1 3 0 V1 10\n"));
    CHECK_FALSE(netlist.contains("Vprobe-V1"));
    CHECK(Core::FindControllingSource(circuit, "V1") == "V1");
    CHECK(Core::FindControllingSource(circuit, "V2") == "V2");
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
    CHECK(netlist.contains("G-U1 0 6 1 2 0.001\nR-U1 6 0 1e+08 noisy=0\nC-U1 6 0 1.59155e-10\n"
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

TEST_CASE("ToSpiceNetlist writes a potentiometer as two resistors that meet at the wiper", "[circuit]") {
    Core::Potentiometer pot;
    pot.SetName("RV1");
    pot.SetValue(10e3);
    pot.SetWiper(0.25);
    Core::Circuit circuit;
    // End 1 on node 1, end 2 on node 2 and the wiper on node 3
    circuit.Add(pot, {1, 2, 3});

    const std::string netlist = circuit.ToSpiceNetlist();
    CHECK(netlist.find("R-RV1a 1 3 2.5k\n") != std::string::npos);
    CHECK(netlist.find("R-RV1b 3 2 7.5k\n") != std::string::npos);
}

TEST_CASE("A wiper at an end keeps a tiny resistor there, since SPICE rejects 0 Ohm", "[circuit]") {
    Core::Potentiometer pot;
    pot.SetName("RV1");
    Core::Circuit circuit;
    circuit.Add(pot, {1, 2, 3});

    pot.SetWiper(0.0);
    CHECK(circuit.ToSpiceNetlist().find("R-RV1a 1 3 1m\n") != std::string::npos);
    pot.SetWiper(1.0);
    CHECK(circuit.ToSpiceNetlist().find("R-RV1b 3 2 1m\n") != std::string::npos);
}

TEST_CASE("A potentiometer probes its three terminals, and the probes carry its currents", "[circuit]") {
    Core::Potentiometer pot;
    pot.SetName("RV1");
    const std::vector<Core::CurrentProbe> probes = Core::GetCurrentProbes(pot);
    REQUIRE(probes.size() == 3);
    CHECK(probes[0].Terminal == 0);
    CHECK(std::string(probes[0].Label) == "A");
    CHECK(probes[1].Terminal == 1);
    CHECK(std::string(probes[1].Label) == "B");
    CHECK(probes[2].Terminal == 2);
    CHECK(std::string(probes[2].Label) == "W");
    CHECK(Core::GetCurrentName(pot, "W") == "RV1.W");

    Core::Circuit circuit;
    circuit.Add(pot, {1, 2, 3});
    const std::string netlist = circuit.ToSpiceNetlist("", true);
    CHECK(netlist.find("Vprobe-RV1-A 1 4 DC 0\n") != std::string::npos);
    CHECK(netlist.find("Vprobe-RV1-W 3 6 DC 0\n") != std::string::npos);
}

TEST_CASE("ToSpiceNetlist writes a switch as a resistor that is nearly a wire closed and nearly nothing open",
          "[circuit]") {
    Core::Switch toggle(Core::ComponentType::Switch);
    toggle.SetName("SW1");
    Core::Switch button(Core::ComponentType::PushButton);
    button.SetName("PB1");
    Core::Circuit circuit;
    circuit.Add(toggle, {1, 2});
    circuit.Add(button, {2, 3});

    std::string netlist = circuit.ToSpiceNetlist();
    CHECK(netlist.find("R-SW1 1 2 1T\n") != std::string::npos);
    CHECK(netlist.find("R-PB1 2 3 1T\n") != std::string::npos);
    toggle.SetClosed(true);
    button.SetClosed(true);
    netlist = circuit.ToSpiceNetlist();
    CHECK(netlist.find("R-SW1 1 2 1m\n") != std::string::npos);
    CHECK(netlist.find("R-PB1 2 3 1m\n") != std::string::npos);
}

TEST_CASE("A switch has one current probe, on its first terminal, named after the part", "[circuit]") {
    Core::Switch toggle(Core::ComponentType::Switch);
    toggle.SetName("SW1");
    const std::vector<Core::CurrentProbe> probes = Core::GetCurrentProbes(toggle);
    REQUIRE(probes.size() == 1);
    CHECK(probes[0].Terminal == 0);
    CHECK(Core::GetCurrentName(toggle, probes[0].Label) == "SW1");

    Core::Circuit circuit;
    circuit.Add(toggle, {1, 2});
    const std::string netlist = circuit.ToSpiceNetlist("", true);
    CHECK(netlist.find("Vprobe-SW1 1 3 DC 0\n") != std::string::npos);
    CHECK(netlist.find("R-SW1 3 2 1T\n") != std::string::npos);
}

TEST_CASE("ToSpiceNetlist writes a voltage-controlled switch with a model of its own", "[circuit]") {
    Core::VoltageControlledSwitch controlled;
    controlled.SetName("S1");
    controlled.SetParameters({.Threshold = 1.5, .Hysteresis = 0.25, .OnResistance = 0.5, .OffResistance = 1e6});
    Core::Circuit circuit;
    // Switch between nodes 1 and 2, controlled by node 3 against node 0
    circuit.Add(controlled, {1, 2, 3, 0});

    const std::string netlist = circuit.ToSpiceNetlist();
    CHECK(netlist.find("S1 1 2 3 0 SW-S1\n") != std::string::npos);
    CHECK(netlist.find(".model SW-S1 SW(VT=1.5 VH=0.25 RON=0.5 ROFF=1e+06)\n") != std::string::npos);
}

TEST_CASE("ToSpiceNetlist writes a relay as a coil of a resistor and an inductor and a switch on the resistor",
          "[circuit]") {
    Core::Relay relay;
    relay.SetName("K1");
    Core::Circuit circuit;
    // Contact between nodes 1 and 2, coil from node 3 to ground
    circuit.Add(relay, {1, 2, 3, 0});

    const std::string netlist = circuit.ToSpiceNetlist();
    // The middle of the coil is the first node past the circuit ones, which are 0 to 3
    CHECK(netlist.find("R-K1coil 3 4 70\n") != std::string::npos);
    CHECK(netlist.find("L-K1coil 4 0 50m\n") != std::string::npos);
    CHECK(netlist.find("S-K1 1 2 3 4 SW-K1\n") != std::string::npos);
    // Closes at 3.5 V and opens at 1 V: threshold 2.25 V with 1.25 V of hysteresis
    CHECK(netlist.find(".model SW-K1 SW(VT=2.25 VH=1.25 RON=0.05 ROFF=1e+09)\n") != std::string::npos);
}

TEST_CASE("Probes of the switches and the relay", "[circuit]") {
    Core::VoltageControlledSwitch controlled;
    controlled.SetName("S1");
    const std::vector<Core::CurrentProbe> switch_probes = Core::GetCurrentProbes(controlled);
    REQUIRE(switch_probes.size() == 1);
    CHECK(switch_probes[0].Terminal == 0);

    Core::Relay relay;
    relay.SetName("K1");
    const std::vector<Core::CurrentProbe> relay_probes = Core::GetCurrentProbes(relay);
    REQUIRE(relay_probes.size() == 2);
    CHECK(relay_probes[0].Terminal == 0);
    CHECK(Core::GetCurrentName(relay, relay_probes[0].Label) == "K1.S");
    CHECK(relay_probes[1].Terminal == 2);
    CHECK(Core::GetCurrentName(relay, relay_probes[1].Label) == "K1.C");
}

TEST_CASE("ToSpiceNetlist writes a transformer as two inductors behind their winding resistances, coupled",
          "[circuit]") {
    Core::Transformer transformer;
    transformer.SetName("T1");
    transformer.SetValue(100e-3);
    transformer.SetParameters({.TurnsRatio = 2.0, .Coupling = 0.98, .WindingResistance = 0.5});
    Core::Circuit circuit;
    // Primary on nodes 1 and 0, secondary on nodes 2 and 0
    circuit.Add(transformer, {1, 0, 2, 0});

    const std::string netlist = circuit.ToSpiceNetlist();
    // The middles of the windings are the first nodes past the circuit ones, which are 0 to 2
    CHECK(netlist.find("R-T1p 1 3 500m\n") != std::string::npos);
    CHECK(netlist.find("L-T1p 3 0 100m\n") != std::string::npos);
    CHECK(netlist.find("R-T1s 2 4 500m\n") != std::string::npos);
    CHECK(netlist.find("L-T1s 4 0 25m\n") != std::string::npos);
    CHECK(netlist.find("K-T1 L-T1p L-T1s 980m\n") != std::string::npos);
}

TEST_CASE("A transformer probes the + terminal of each winding", "[circuit]") {
    Core::Transformer transformer;
    transformer.SetName("T1");
    const std::vector<Core::CurrentProbe> probes = Core::GetCurrentProbes(transformer);
    REQUIRE(probes.size() == 2);
    CHECK(probes[0].Terminal == 0);
    CHECK(Core::GetCurrentName(transformer, probes[0].Label) == "T1.P");
    CHECK(probes[1].Terminal == 2);
    CHECK(Core::GetCurrentName(transformer, probes[1].Label) == "T1.S");
}

TEST_CASE("Each current of a part belongs to the terminal it flows into", "[circuit]") {
    Core::Resistor named;
    named.SetName("R1");
    CHECK(Core::GetCurrentTerminals(named) == std::vector<std::size_t>{0});
    Core::BJT transistor(Core::ComponentType::NPN);
    transistor.SetName("Q1");
    CHECK(Core::GetCurrentTerminals(transistor) == std::vector<std::size_t>{0, 1, 2});
    Core::Relay relay;
    relay.SetName("K1");
    // The contact current enters terminal 1, and the coil current terminal 3
    CHECK(Core::GetCurrentTerminals(relay) == std::vector<std::size_t>{0, 2});
    Core::Transformer transformer;
    transformer.SetName("T1");
    CHECK(Core::GetCurrentTerminals(transformer) == std::vector<std::size_t>{0, 2});
}

TEST_CASE("ToSpiceNetlist writes a gate behind bridges, with a pull-down on each input", "[circuit]") {
    Core::LogicPart gate(Core::ComponentType::Nand);
    gate.SetName("U1");
    Core::Circuit circuit;
    // Inputs on nodes 1 and 2, output on node 3
    circuit.Add(gate, {1, 2, 3});

    const std::string netlist = circuit.ToSpiceNetlist();
    // Digital nodes are numbered past the circuit ones, 0 to 3: 4 and 5 for the inputs, 6 for the output, 7 behind
    // its resistor
    CHECK(netlist.find("A-U1-A [1] [4] ADC-U1\n") != std::string::npos);
    CHECK(netlist.find("A-U1-B [2] [5] ADC-U1\n") != std::string::npos);
    CHECK(netlist.find("R-U1-A 1 0 1G\n") != std::string::npos);
    CHECK(netlist.find("R-U1-B 2 0 1G\n") != std::string::npos);
    CHECK(netlist.find("A-U1 [4 5] 6 LOGIC-U1\n") != std::string::npos);
    CHECK(netlist.find("A-U1-Y [6] [7] DAC-U1\n") != std::string::npos);
    CHECK(netlist.find("R-U1-Y 7 3 50\n") != std::string::npos);
    // The inputs read 0 below 30% of the logic level and 1 above 70%
    CHECK(netlist.find(".model ADC-U1 adc_bridge(in_low=1.5 in_high=3.5)\n") != std::string::npos);
    CHECK(netlist.find(".model DAC-U1 dac_bridge(out_low=0 out_high=5 out_undef=2.5)\n") != std::string::npos);
    CHECK(netlist.find(".model LOGIC-U1 d_nand(rise_delay=10n fall_delay=10n)\n") != std::string::npos);
}

TEST_CASE("ToSpiceNetlist writes a gate with three inputs as a vector of three digital nodes", "[circuit]") {
    Core::LogicPart gate(Core::ComponentType::Xor);
    gate.SetName("U1");
    gate.SetInputCount(3);
    Core::Circuit circuit;
    // Inputs on nodes 1, 2 and 3, output on node 4
    circuit.Add(gate, {1, 2, 3, 4});

    const std::string netlist = circuit.ToSpiceNetlist();
    // Digital nodes start past the circuit ones, 0 to 4: 5, 6 and 7 for the inputs, 8 for the output, 9 behind its
    // resistor
    CHECK(netlist.contains("A-U1-A [1] [5] ADC-U1\n"));
    CHECK(netlist.contains("A-U1-B [2] [6] ADC-U1\n"));
    CHECK(netlist.contains("A-U1-C [3] [7] ADC-U1\n"));
    CHECK(netlist.contains("R-U1-C 3 0 1G\n"));
    CHECK(netlist.contains("A-U1 [5 6 7] 8 LOGIC-U1\n"));
    CHECK(netlist.contains("A-U1-Y [8] [9] DAC-U1\n"));
    CHECK(netlist.contains("R-U1-Y 9 4 50\n"));
    CHECK(netlist.contains(".model LOGIC-U1 d_xor(rise_delay=10n fall_delay=10n)\n"));
    // The current of the gate is measured at its output, which is the fourth terminal now
    CHECK(Core::GetCurrentTerminals(gate) == std::vector<std::size_t>{3});
}

TEST_CASE("ToSpiceNetlist gives a single-input gate scalar ports and follows the logic level", "[circuit]") {
    Core::LogicPart inverter(Core::ComponentType::Not);
    inverter.SetName("U1");
    inverter.SetParameters({.LogicLevel = 3.3, .PropagationDelay = 2e-9});
    Core::Circuit circuit;
    circuit.Add(inverter, {1, 2});

    const std::string netlist = circuit.ToSpiceNetlist();
    CHECK(netlist.find("A-U1 3 4 LOGIC-U1\n") != std::string::npos);
    CHECK(netlist.find(".model ADC-U1 adc_bridge(in_low=990m in_high=2.31)\n") != std::string::npos);
    CHECK(netlist.find(".model DAC-U1 dac_bridge(out_low=0 out_high=3.3 out_undef=1.65)\n") != std::string::npos);
    CHECK(netlist.find(".model LOGIC-U1 d_inverter(rise_delay=2n fall_delay=2n)\n") != std::string::npos);
}

TEST_CASE("ToSpiceNetlist writes a flip-flop with its inputs in order and both outputs", "[circuit]") {
    Core::LogicPart flip_flop(Core::ComponentType::JKFlipFlop);
    flip_flop.SetName("U1");
    Core::Circuit circuit;
    // J, K, CLK, SET and RESET on nodes 1 to 5, Q on node 6 and QN on node 7
    circuit.Add(flip_flop, {1, 2, 3, 4, 5, 6, 7});

    const std::string netlist = circuit.ToSpiceNetlist();
    // Digital nodes 8 to 12 for the inputs, 13 and 14 for the outputs
    CHECK(netlist.find("A-U1 8 9 10 11 12 13 14 LOGIC-U1\n") != std::string::npos);
    CHECK(netlist.find("A-U1-CLK [3] [10] ADC-U1\n") != std::string::npos);
    CHECK(netlist.find("R-U1-QN 16 7 50\n") != std::string::npos);
    CHECK(netlist.find(".model LOGIC-U1 d_jkff(clk_delay=10n set_delay=10n reset_delay=10n rise_delay=10n "
                       "fall_delay=10n)\n") != std::string::npos);
}

TEST_CASE("Probes of the logic parts", "[circuit]") {
    Core::LogicPart gate(Core::ComponentType::And);
    gate.SetName("U1");
    const std::vector<Core::CurrentProbe> gate_probes = Core::GetCurrentProbes(gate);
    REQUIRE(gate_probes.size() == 1);
    CHECK(gate_probes[0].Terminal == 2);
    CHECK(Core::GetCurrentName(gate, gate_probes[0].Label) == "U1");

    Core::LogicPart inverter(Core::ComponentType::Not);
    inverter.SetName("U2");
    CHECK(Core::GetCurrentProbes(inverter)[0].Terminal == 1);

    Core::LogicPart flip_flop(Core::ComponentType::DFlipFlop);
    flip_flop.SetName("U3");
    const std::vector<Core::CurrentProbe> flip_flop_probes = Core::GetCurrentProbes(flip_flop);
    REQUIRE(flip_flop_probes.size() == 2);
    CHECK(flip_flop_probes[0].Terminal == 4);
    CHECK(Core::GetCurrentName(flip_flop, flip_flop_probes[0].Label) == "U3.Q");
    CHECK(flip_flop_probes[1].Terminal == 5);
    CHECK(Core::GetCurrentName(flip_flop, flip_flop_probes[1].Label) == "U3.QN");
}

TEST_CASE("GetElementNames lists the elements the netlist writes for each part", "[circuit]") {
    Core::Resistor resistor;
    resistor.SetName("R1");
    Core::Potentiometer pot;
    pot.SetName("RV1");
    Core::OpAmp ideal;
    ideal.SetName("U1");
    Core::OpAmp macromodel;
    macromodel.SetName("U2");
    macromodel.SetModel(*Core::FindOpAmpModel(Core::ComponentType::OpAmp, "uA741"));
    Core::Switch toggle(Core::ComponentType::Switch);
    toggle.SetName("SW1");
    Core::Relay relay;
    relay.SetName("K1");
    Core::Transformer transformer;
    transformer.SetName("T1");
    Core::LogicPart gate(Core::ComponentType::Nand);
    gate.SetName("U3");
    Core::Ground ground;
    Core::Circuit circuit;
    circuit.Add(resistor, {1, 0});
    circuit.Add(pot, {1, 2, 3});
    circuit.Add(gate, {1, 2, 3});
    circuit.Add(ideal, {3, 1, 2, 4, 5});
    circuit.Add(macromodel, {6, 1, 2, 4, 5});
    circuit.Add(toggle, {1, 2});
    circuit.Add(relay, {1, 2, 3, 0});
    circuit.Add(transformer, {1, 0, 2, 0});
    circuit.Add(ground, {0});

    // Every name starts a line of the netlist, and every part line is covered
    const std::string netlist = "\n" + circuit.ToSpiceNetlist();
    std::size_t element_count = 0;
    for (const Core::CircuitEntry &entry : circuit.GetEntries()) {
        for (const std::string &name : Core::GetElementNames(*entry.Part)) {
            CHECK(netlist.contains("\n" + name + " "));
            ++element_count;
        }
    }
    // A NAND gate has its model, and a bridge and a resistor on each of its two inputs and its output
    CHECK(element_count == 1 + 2 + 6 + 1 + 1 + 3 + 5 + 7);
    CHECK(Core::GetElementNames(ground).empty());
    CHECK(Core::GetElementNames(macromodel) == std::vector<std::string>{"X-U2"});
}
