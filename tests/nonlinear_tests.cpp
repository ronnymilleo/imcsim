/**
 * @file    nonlinear_tests.cpp
 * @brief   Theory checks for the Nodalys nonlinear devices: the diode equation, the forward-active bipolar
 *          transistor and the level-1 MOSFET regions, each against hand-computed values.
 * @details The references are analytic: the Shockley diode equation solved for the loop, the Gummel-Poon forward
 *          current gain, and the level-1 saturation current of the parameter documentation in mosfet.h.
 */

#include "backends/nodalys/nodalys_backend.h"
#include "circuit/circuit.h"
#include "circuit/components/bjt.h"
#include "circuit/components/diode.h"
#include "circuit/components/ground.h"
#include "circuit/components/mosfet.h"
#include "circuit/components/resistor.h"
#include "circuit/components/vcc.h"
#include "circuit/components/voltage_source.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <format>

namespace {

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

/// kT/q at 300.15 K, the temperature the Nodalys models are evaluated at (pn_junction.h).
constexpr double VT = 8.617087399994e-5 * 300.15;

double CurrentOf(const Core::OperatingPoint &point, const std::string &name) {
    const auto found = std::ranges::find(point.Currents, name, &Core::ComponentCurrent::Name);
    REQUIRE(found != point.Currents.end());
    return found->Current;
}

} // namespace

TEST_CASE("A diode loop solves the Shockley equation") {
    Core::VCC supply;
    supply.SetName("V1");
    supply.SetValue(5.0);
    Core::Resistor series;
    series.SetName("R1");
    series.SetValue(10e3);
    Core::Diode diode(Core::ComponentType::DIODE);
    diode.SetName("D1");
    diode.SetCustom();
    Core::DiodeParameters parameters;
    parameters.SaturationCurrent = 1e-14;
    diode.SetCustomParameters(parameters);
    Core::Ground ground;
    Core::Circuit circuit;
    circuit.Add(supply, {1});
    circuit.Add(series, {1, 2});
    circuit.Add(diode, {2, 0});
    circuit.Add(ground, {0});

    Core::NodalysBackend backend;
    const Core::OperatingPointRun run = backend.RunOperatingPoint(circuit, std::nullopt);
    REQUIRE(run.Result.has_value());
    // Theory: I = (5 V − Vd) / 10k with I = IS·e^(Vd/Vt); the junction sits near 0.6 V
    const double current = CurrentOf(*run.Result, "D1");
    const double junction = run.Result->NodeVoltages[2];
    REQUIRE_THAT(junction, WithinRel(VT * std::log(current / 1e-14), 1e-3));
    REQUIRE_THAT(junction, WithinRel(0.6, 0.15));
    // The reported current is the Shockley current at the solution voltage; its consistency with the resistor
    // current is bounded by the Newton convergence tolerance (RelTol = 1e-3, as ngspice's), through the diode
    // conductance — here a KCL residual of tens of nanoamperes on a 0.4 mA loop
    REQUIRE_THAT(current, WithinRel((5.0 - junction) / 10e3, 1e-3));
}

TEST_CASE("A Zener diode in breakdown holds its breakdown voltage") {
    Core::VCC supply;
    supply.SetName("V1");
    supply.SetValue(10.0);
    Core::Resistor series;
    series.SetName("R1");
    series.SetValue(1e3);
    Core::Diode zener(Core::ComponentType::ZENER_DIODE);
    zener.SetName("D1");
    zener.SetCustom();
    Core::DiodeParameters parameters;
    parameters.BreakdownVoltage = 5.6;
    parameters.BreakdownCurrent = 1e-3;
    zener.SetCustomParameters(parameters);
    Core::Ground ground;
    Core::Circuit circuit;
    // Reversed into breakdown: the cathode faces the supply
    circuit.Add(supply, {1});
    circuit.Add(series, {1, 2});
    circuit.Add(zener, {0, 2});
    circuit.Add(ground, {0});

    Core::NodalysBackend backend;
    const Core::OperatingPointRun run = backend.RunOperatingPoint(circuit, std::nullopt);
    if (!run.Result) {
        for (const Core::SimulatorMessage &message : run.Messages) {
            INFO(std::format("message: {}", message.Text));
        }
        INFO(std::format("v2={}", run.Result->NodeVoltages[2]));
    }
    REQUIRE(run.Result.has_value());
    // The breakdown knee slides with the current: v = BV + Vt·ln(I/IBV) with I = (10 V − v)/1k, which lands
    // within a millivolt of 5.638 V; the current enters the cathode in breakdown, so it reads negative into the
    // anode terminal, exactly as ngspice reports it
    REQUIRE_THAT(run.Result->NodeVoltages[2], WithinRel(5.6381, 1e-3));
    REQUIRE_THAT(CurrentOf(*run.Result, "D1"), WithinRel(-4.367e-3, 1e-3));
}

TEST_CASE("A common-emitter amplifier sits at its quiescent point") {
    Core::VCC supply;
    supply.SetName("V1");
    supply.SetValue(12.0);
    Core::Resistor collector_load;
    collector_load.SetName("RC");
    collector_load.SetValue(1e3);
    Core::Resistor base_bias;
    base_bias.SetName("RB");
    base_bias.SetValue(470e3);
    Core::BJT transistor(Core::ComponentType::NPN);
    transistor.SetName("Q1");
    transistor.SetCustom();
    Core::BJTParameters parameters;
    parameters.ForwardBeta = 100.0;
    transistor.SetCustomParameters(parameters);
    Core::Ground ground;
    Core::Circuit circuit;
    // Supply on 1; collector resistor 1→2; base bias 1→3; collector on 2, base on 3, emitter on 0
    circuit.Add(supply, {1});
    circuit.Add(collector_load, {1, 2});
    circuit.Add(base_bias, {1, 3});
    circuit.Add(transistor, {2, 3, 0});
    circuit.Add(ground, {0});

    Core::NodalysBackend backend;
    const Core::OperatingPointRun run = backend.RunOperatingPoint(circuit, std::nullopt);
    REQUIRE(run.Result.has_value());
    // Hand analysis: IB ≈ (12 V − 0.7 V) / 470k = 24 uA, IC = beta·IB = 2.4 mA, VC = 12 V − 2.4 V = 9.6 V
    REQUIRE_THAT(CurrentOf(*run.Result, "Q1.B"), WithinRel(24e-6, 2e-2));
    REQUIRE_THAT(CurrentOf(*run.Result, "Q1.C"), WithinRel(2.4e-3, 2e-2));
    REQUIRE_THAT(run.Result->NodeVoltages[2], WithinRel(9.6, 2e-2));
}

TEST_CASE("An NMOS common-source stage follows the level-1 saturation current") {
    Core::VCC supply;
    supply.SetName("V1");
    supply.SetValue(10.0);
    Core::Resistor drain_load;
    drain_load.SetName("RD");
    drain_load.SetValue(1e3);
    Core::VoltageSource gate;
    gate.SetName("VG");
    gate.SetValue(5.0);
    Core::MOSFET transistor(Core::ComponentType::NMOS);
    transistor.SetName("M1");
    transistor.SetCustom();
    Core::MOSFETParameters parameters;
    parameters.ThresholdVoltage = 2.0;
    parameters.Transconductance = 2e-5;
    parameters.Width = 100e-6;
    parameters.Length = 10e-6;
    transistor.SetCustomParameters(parameters);
    Core::Ground ground;
    Core::Circuit circuit;
    // Supply 1; drain resistor 1→2; gate source 3→0; drain 2, gate 3, source 0
    circuit.Add(supply, {1});
    circuit.Add(drain_load, {1, 2});
    circuit.Add(gate, {3, 0});
    circuit.Add(transistor, {2, 3, 0});
    circuit.Add(ground, {0});

    Core::NodalysBackend backend;
    const Core::OperatingPointRun run = backend.RunOperatingPoint(circuit, std::nullopt);
    INFO(std::format("v1={} v2={} v3={}", run.Result->NodeVoltages[1], run.Result->NodeVoltages[2],
                     run.Result->NodeVoltages[3]));
    REQUIRE(run.Result.has_value());
    // Theory (mosfet.h): saturation current KP/2 · W/L · (VGS − VTO)² = 1e-5·10·9 = 0.9 mA, so the drain sits
    // at 10 V − 0.9 V
    REQUIRE_THAT(CurrentOf(*run.Result, "M1.D"), WithinRel(0.9e-3, 1e-3));
    REQUIRE_THAT(run.Result->NodeVoltages[2], WithinRel(9.1, 1e-3));
}

TEST_CASE("A PMOS with its gate low conducts from the supply") {
    Core::VCC supply;
    supply.SetName("V1");
    supply.SetValue(10.0);
    Core::Resistor drain_load;
    drain_load.SetName("RD");
    drain_load.SetValue(1e3);
    drain_load.SetName("RD");
    Core::VoltageSource gate;
    gate.SetName("VG");
    gate.SetValue(0.0);
    Core::MOSFET transistor(Core::ComponentType::PMOS);
    transistor.SetName("M1");
    transistor.SetCustom();
    Core::MOSFETParameters parameters;
    parameters.ThresholdVoltage = -2.0;
    parameters.Transconductance = 2e-5;
    parameters.Width = 100e-6;
    parameters.Length = 10e-6;
    transistor.SetCustomParameters(parameters);
    Core::Ground ground;
    Core::Circuit circuit;
    // Source on the supply 1, drain on 2 through the load to ground, gate on 3
    circuit.Add(supply, {1});
    circuit.Add(drain_load, {2, 0});
    circuit.Add(gate, {3, 0});
    circuit.Add(transistor, {2, 3, 1});
    circuit.Add(ground, {0});

    Core::NodalysBackend backend;
    const Core::OperatingPointRun run = backend.RunOperatingPoint(circuit, std::nullopt);
    REQUIRE(run.Result.has_value());
    // VSG = 10 V and a threshold of 2 V give an overdrive of 8 V, but the load pulls the drain up to where the
    // transistor works in the linear region: with x = VSD the operating point solves x² − 26x + 100 = 0, so
    // VSD = 4.693 V and the drain sits at 5.307 V with 5.307 mA, which the level-1 equations confirm
    REQUIRE_THAT(CurrentOf(*run.Result, "M1.D"), WithinRel(-5.307e-3, 1e-3));
    REQUIRE_THAT(run.Result->NodeVoltages[2], WithinRel(5.307, 1e-3));
}
