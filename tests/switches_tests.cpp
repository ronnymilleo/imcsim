/**
 * @file    switches_tests.cpp
 * @brief   Theory checks for the native switch, relay and transformer: the hysteresis thresholds, the relay
 *          pull-in behavior and the transformer turns ratio.
 */

#include "components/ground.h"
#include "components/relay.h"
#include "components/resistor.h"
#include "components/transformer.h"
#include "components/vcc.h"
#include "components/voltage_controlled_switch.h"
#include "components/voltage_source.h"
#include "model/circuit.h"
#include "native/native_backend.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <format>

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
        Core::VCC supply;
        Core::Resistor load;
        Core::VoltageSource control;
        Core::VoltageControlledSwitch switch_part;
        Core::Ground ground;
        Core::Circuit circuit;
    };
    auto build = [](const double control_voltage) {
        auto bundle = std::make_unique<Bundle>();
        bundle->supply.SetName("V1");
        bundle->supply.SetValue(10.0);
        bundle->load.SetName("R1");
        bundle->load.SetValue(1e3);
        bundle->control.SetName("V2");
        bundle->control.SetValue(control_voltage);
        bundle->switch_part.SetName("S1");
        Core::SwitchParameters parameters;
        parameters.Threshold = 2.5;
        parameters.Hysteresis = 0.1;
        parameters.OnResistance = 0.01;
        bundle->switch_part.SetParameters(parameters);
        // S1 sits between node 2 and ground, controlled from node 3
        bundle->circuit.Add(bundle->supply, {1});
        bundle->circuit.Add(bundle->load, {1, 2});
        bundle->circuit.Add(bundle->switch_part, {2, 0, 3, 0});
        bundle->circuit.Add(bundle->control, {3, 0});
        bundle->circuit.Add(bundle->ground, {0});
        return bundle;
    };

    Core::NativeBackend backend;
    const auto open_circuit = build(0.0);
    const Core::OperatingPointRun open = backend.RunOperatingPoint(open_circuit->circuit);
    REQUIRE(open.Result.has_value());
    REQUIRE_THAT(open.Result->NodeVoltages[2], WithinRel(10.0, 1e-5));
    // Open, the switch leaks the supply across its off resistance (giga-ohms): nano-amperes, not zero
    REQUIRE_THAT(CurrentOf(*open.Result, "S1"), WithinAbs(0.0, 1e-7));

    const auto closed_circuit = build(5.0);
    const Core::OperatingPointRun closed = backend.RunOperatingPoint(closed_circuit->circuit);
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

    Core::NativeBackend backend;
    const Core::OperatingPointRun run = backend.RunOperatingPoint(circuit);
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

    Core::NativeBackend backend;
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
