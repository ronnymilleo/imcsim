/**
 * @file    switches_tests.cpp
 * @brief   Theory checks for the Nodalys switch, relay and transformer: the hysteresis thresholds, the relay
 *          pull-in behavior and the transformer turns ratio.
 */

#include "backends/nodalys/nodalys_backend.h"
#include "circuit/circuit.h"
#include "circuit/components/ground.h"
#include "circuit/components/relay.h"
#include "circuit/components/resistor.h"
#include "circuit/components/transformer.h"
#include "circuit/components/vcc.h"
#include "circuit/components/voltage_controlled_switch.h"
#include "circuit/components/voltage_source.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <format>
#include <numbers>

namespace {

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

double CurrentOf(const Core::OperatingPoint &point, const std::string &name) {
    const auto found = std::ranges::find(point.Currents, name, &Core::ComponentCurrent::Name);
    REQUIRE(found != point.Currents.end());
    return found->Current;
}

} // namespace

TEST_CASE("A voltage-controlled switch opens and closes with its control voltage") {
    // A circuit keeps raw pointers to its parts (Circuit::Add), so the parts must outlive it; the bundle
    // lives on the heap, which keeps the pointers stable even when the builder lambda returns
    struct Bundle {
        Core::VCC Supply;
        Core::Resistor Load;
        Core::VoltageSource Control;
        Core::VoltageControlledSwitch SwitchPart;
        Core::Ground Ground;
        Core::Circuit Circuit;
    };
    auto build = [](const double control_voltage) {
        auto bundle = std::make_unique<Bundle>();
        bundle->Supply.SetName("V1");
        bundle->Supply.SetValue(10.0);
        bundle->Load.SetName("R1");
        bundle->Load.SetValue(1e3);
        bundle->Control.SetName("V2");
        bundle->Control.SetValue(control_voltage);
        bundle->SwitchPart.SetName("S1");
        Core::SwitchParameters parameters;
        parameters.Threshold = 2.5;
        parameters.Hysteresis = 0.1;
        parameters.OnResistance = 0.01;
        bundle->SwitchPart.SetParameters(parameters);
        // S1 sits between node 2 and ground, controlled from node 3
        bundle->Circuit.Add(bundle->Supply, {1});
        bundle->Circuit.Add(bundle->Load, {1, 2});
        bundle->Circuit.Add(bundle->SwitchPart, {2, 0, 3, 0});
        bundle->Circuit.Add(bundle->Control, {3, 0});
        bundle->Circuit.Add(bundle->Ground, {0});
        return bundle;
    };

    Core::NodalysBackend backend;
    const auto open_circuit = build(0.0);
    const Core::OperatingPointRun open = backend.RunOperatingPoint(open_circuit->Circuit, std::nullopt);
    REQUIRE(open.Result.has_value());
    REQUIRE_THAT(open.Result->NodeVoltages[2], WithinRel(10.0, 1e-5));
    // Open, the switch leaks the supply across its off resistance (giga-ohms): nano-amperes, not zero
    REQUIRE_THAT(CurrentOf(*open.Result, "S1"), WithinAbs(0.0, 1e-7));

    const auto closed_circuit = build(5.0);
    const Core::OperatingPointRun closed = backend.RunOperatingPoint(closed_circuit->Circuit, std::nullopt);
    REQUIRE(closed.Result.has_value());
    REQUIRE_THAT(closed.Result->NodeVoltages[2], WithinRel(10.0 * 0.01 / (1e3 + 0.01), 1e-3));
    REQUIRE_THAT(CurrentOf(*closed.Result, "S1"), WithinRel(10.0 / (1e3 + 0.01), 1e-4));
}

TEST_CASE("A relay closes its contact when the coil current pulls it in") {
    Core::VCC coil_supply;
    coil_supply.SetName("V1");
    coil_supply.SetValue(5.0);
    Core::VCC contact_supply;
    contact_supply.SetName("V2");
    contact_supply.SetValue(12.0);
    Core::Resistor load;
    load.SetName("R1");
    load.SetValue(1e3);
    Core::Relay relay;
    relay.SetName("K1");
    Core::Ground ground;
    Core::Circuit circuit;
    // Contact 1→2 in series with the load on the 12 V supply; coil 3→0 on the 5 V supply (the second coil
    // terminal returns to ground, like the netlist the ngspice backend writes)
    circuit.Add(contact_supply, {1});
    circuit.Add(relay, {1, 2, 3, 0});
    circuit.Add(load, {2, 0});
    circuit.Add(coil_supply, {3});
    circuit.Add(ground, {0});

    Core::NodalysBackend backend;
    const Core::OperatingPointRun run = backend.RunOperatingPoint(circuit, std::nullopt);
    REQUIRE(run.Result.has_value());
    // The coil takes 5 V / 70 Ohm = 71 mA, well past the 3.5 V pull-in, so the contact is closed and the load
    // sees the full 12 V
    REQUIRE_THAT(run.Result->NodeVoltages[2], WithinRel(12.0, 1e-3));
    REQUIRE_THAT(CurrentOf(*run.Result, "K1.C"), WithinRel(5.0 / 70.0, 1e-4));
    REQUIRE_THAT(CurrentOf(*run.Result, "K1.S"), WithinRel(12e-3, 1e-3));
}

TEST_CASE("A transformer steps voltage by its turns ratio in a transient") {
    Core::VoltageSource source;
    source.SetName("V1");
    source.SetSourceType(Core::VoltageSource::SourceType::AC);
    Core::ACParameters ac;
    ac.Amplitude = 10.0;
    ac.Frequency = 1e3;
    source.SetAC(ac);
    Core::Transformer transformer;
    transformer.SetName("T1");
    transformer.SetValue(1.0); // 1 H primary
    Core::TransformerParameters figures;
    figures.TurnsRatio = 2.0; // 2:1 steps down to half
    figures.Coupling = 0.999;
    figures.WindingResistance = 0.1;
    transformer.SetParameters(figures);
    Core::Resistor load;
    load.SetName("R1");
    load.SetValue(1e3);
    Core::Ground ground;
    Core::Circuit circuit;
    // Primary on 1→0; secondary on 2→3, loaded to ground; the + terminals are 1 and 2
    circuit.Add(source, {1, 0});
    circuit.Add(transformer, {1, 0, 2, 0});
    circuit.Add(load, {2, 0});
    circuit.Add(ground, {0});

    Core::NodalysBackend backend;
    const Core::TransientRun run =
        backend.RunTransient(circuit, {.StopTime = 20e-3, .TimeStep = 10e-6}, std::nullopt, {});
    if (!run.Result) {
        for (const Core::SimulatorMessage &message : run.Messages) {
            INFO(std::format("message: {}", message.Text));
        }
        INFO(run.Result.error());
    }
    REQUIRE(run.Result.has_value());
    REQUIRE(run.Result->IsComplete);
    // In steady state an ideal 2:1 transformer gives v(2) = v(1)/2 in phase; the 0.1 Ohm windings and the
    // magnetizing current shift that by well under a percent at 1 kHz into 1k. Compare amplitudes and phase
    // by correlation over the settled last half of the run instead of pointwise samples, which are
    // meaningless at the zero crossings of the sine
    const std::size_t begin = run.Result->Times.size() / 2;
    std::size_t peak_index = begin;
    double expected_peak = 0.0;
    double secondary_peak = 0.0;
    double correlation = 0.0;
    double secondary_energy = 0.0;
    double expected_energy = 0.0;
    for (std::size_t index = begin; index < run.Result->Times.size(); ++index) {
        const double t = run.Result->Times[index];
        const double expected = 10.0 / 2.0 * std::sin(2.0 * std::numbers::pi * 1e3 * t);
        const double actual = run.Result->NodeVoltages[2][index];
        if (std::fabs(actual) > std::fabs(secondary_peak)) {
            secondary_peak = actual;
            peak_index = index;
        }
        expected_peak = std::max(expected_peak, std::fabs(expected));
        correlation += actual * expected;
        secondary_energy += actual * actual;
        expected_energy += expected * expected;
    }
    INFO(std::format("secondary peak {} V at index {}", secondary_peak, peak_index));
    REQUIRE_THAT(std::fabs(secondary_peak), WithinRel(expected_peak, 2e-2));
    // Normalized correlation cos(phi) between the secondary and the ideal sine stays near 1 in phase
    REQUIRE(correlation > 0.99 * std::sqrt(secondary_energy * expected_energy));
}
