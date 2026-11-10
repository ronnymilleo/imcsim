/**
 * @file    nodalys_backend_tests.cpp
 * @brief   Tests for the Nodalys simulation backend: its capabilities, the sign conventions of its results and the
 *          operating point, DC sweep and transient against hand-checked theory.
 * @details The reference values are analytic: the divider rule, the RC/RL exponential responses and the sign
 *          conventions documented in simulation_results.h. Each case cites the computation it checks.
 */

#include "components/capacitor.h"
#include "components/clock.h"
#include "components/ground.h"
#include "components/inductor.h"
#include "components/logic_input.h"
#include "components/logic_part.h"
#include "components/potentiometer.h"
#include "components/resistor.h"
#include "components/subcircuit.h"
#include "components/switch.h"
#include "components/vcc.h"
#include "components/voltage_source.h"
#include "model/subcircuit.h"
#include "nodalys/nodalys_backend.h"
#include "simulation/simulation_backend.h"
#include "simulation/simulator.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <numbers>
#include <set>
#include <string>

namespace {

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

/**
 * @struct  Divider
 * @brief   A 10 V supply on node 1 feeding 1k and 3k resistors in series to ground, with the middle on node 2.
 * @details Theory (Vlach & Singhal, ch. 2): v(2) = 10 V · 3k / (1k + 3k) = 7.5 V, and the loop current is
 *          10 V / 4k = 2.5 mA everywhere.
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

double CurrentOf(const Core::OperatingPoint &point, const std::string &name) {
    const auto found = std::ranges::find(point.Currents, name, &Core::ComponentCurrent::Name);
    REQUIRE(found != point.Currents.end());
    return found->Current;
}

Core::Transient RunTransient(Core::SimulationBackend &backend, const Core::Circuit &circuit, double stop, double step) {
    Core::TransientRun run = backend.RunTransient(circuit, {.StopTime = stop, .TimeStep = step}, std::nullopt, {});
    REQUIRE(run.Result.has_value());
    return std::move(*run.Result);
}

} // namespace

TEST_CASE("Nodalys names itself and lists what it runs") {
    const Core::NodalysBackend backend;
    REQUIRE(backend.GetName() == "Nodalys");
    const Core::BackendCapabilities &capabilities = backend.GetCapabilities();
    REQUIRE(capabilities.Analyses.contains(Core::AnalysisKind::OPERATING_POINT));
    REQUIRE(capabilities.Analyses.contains(Core::AnalysisKind::TRANSIENT));
    REQUIRE(capabilities.Analyses.contains(Core::AnalysisKind::DC_SWEEP));
    REQUIRE(capabilities.Analyses.contains(Core::AnalysisKind::AC_SWEEP));
    REQUIRE(capabilities.Analyses.contains(Core::AnalysisKind::NOISE));
    REQUIRE(capabilities.Analyses.contains(Core::AnalysisKind::TRANSFER_FUNCTION));
}

TEST_CASE("The Nodalys backend reports logic parts and analog parts as supported") {
    const Core::NodalysBackend backend;
    const std::optional<std::string> gate = Core::FindUnsupported(
        backend, std::set<Core::ComponentType>{Core::ComponentType::AND}, Core::AnalysisKind::OPERATING_POINT);
    REQUIRE(!gate.has_value());
    const std::optional<std::string> flip_flop = Core::FindUnsupported(
        backend, std::set<Core::ComponentType>{Core::ComponentType::JK_FLIP_FLOP}, Core::AnalysisKind::TRANSIENT);
    REQUIRE(!flip_flop.has_value());
    // Resistors, sources and the other analog parts raise no objection
    const std::optional<std::string> analog = Core::FindUnsupported(
        backend,
        std::set<Core::ComponentType>{Core::ComponentType::RESISTOR, Core::ComponentType::CAPACITOR,
                                      Core::ComponentType::VOLTAGE_SOURCE, Core::ComponentType::DIODE,
                                      Core::ComponentType::NPN, Core::ComponentType::NMOS, Core::ComponentType::OP_AMP,
                                      Core::ComponentType::TRANSFORMER},
        Core::AnalysisKind::DC_SWEEP);
    REQUIRE(!analog.has_value());
}

TEST_CASE("The operating point of a divider follows the divider rule") {
    const Divider divider;
    Core::NodalysBackend backend;
    const Core::OperatingPointRun run = backend.RunOperatingPoint(divider.Circuit);
    REQUIRE(run.Result.has_value());
    REQUIRE_THAT(run.Result->NodeVoltages[2], WithinRel(7.5, 1e-5));
    // The currents follow the results convention: positive from the first terminal to the second, so the supply
    // delivering power shows a negative current
    REQUIRE_THAT(CurrentOf(*run.Result, "R1"), WithinRel(2.5e-3, 1e-5));
    REQUIRE_THAT(CurrentOf(*run.Result, "R2"), WithinRel(2.5e-3, 1e-5));
    REQUIRE_THAT(CurrentOf(*run.Result, "V1"), WithinRel(-2.5e-3, 1e-5));
}

TEST_CASE("A capacitor is open and an inductor is a short at the operating point") {
    Core::VCC supply;
    supply.SetName("V1");
    supply.SetValue(10.0);
    Core::Resistor series;
    series.SetName("R1");
    Core::Capacitor shunt;
    shunt.SetName("C1");
    shunt.SetValue(1e-6);
    Core::Inductor inductor;
    inductor.SetName("L1");
    inductor.SetValue(1e-3);
    Core::Ground ground;
    Core::Circuit circuit;
    // Supply on 1, R from 1 to 2, C from 2 to ground, and L from 2 to the output node 3
    circuit.Add(supply, {1});
    circuit.Add(series, {1, 2});
    circuit.Add(shunt, {2, 0});
    circuit.Add(inductor, {2, 3});
    circuit.Add(ground, {0});
    Core::NodalysBackend backend;
    const Core::OperatingPointRun run = backend.RunOperatingPoint(circuit);
    REQUIRE(run.Result.has_value());
    // With the capacitor open no current flows, so the inductor drops no voltage
    REQUIRE_THAT(run.Result->NodeVoltages[3], WithinRel(10.0, 1e-5));
    REQUIRE_THAT(CurrentOf(*run.Result, "C1"), WithinAbs(0.0, 1e-12));
}

TEST_CASE("The DC sweep of a divider scales the operating point") {
    const Divider divider;
    Core::NodalysBackend backend;
    const Core::DCSweepRun run = backend.RunDCSweep(
        divider.Circuit, {.Swept = {.Source = "V1", .Start = 0.0, .Stop = 5.0, .Step = 1.0}}, std::nullopt);
    REQUIRE(run.Result.has_value());
    REQUIRE(run.Result->SweptValues == std::vector<double>{0.0, 1.0, 2.0, 3.0, 4.0, 5.0});
    REQUIRE(run.Result->SweptUnit == "V");
    REQUIRE(run.Result->Curves.size() == 1);
    for (std::size_t index = 0; index < run.Result->SweptValues.size(); ++index) {
        INFO(std::format("sweep point {}", index));
        REQUIRE_THAT(run.Result->Curves[0].NodeVoltages[2][index],
                     WithinRel(0.75 * run.Result->SweptValues[index], 1e-5));
    }
}

TEST_CASE("The transient of an RC circuit follows the exponential charge curve") {
    Core::VoltageSource source;
    source.SetName("V1");
    source.SetSourceType(Core::VoltageSource::SourceType::PULSE);
    Core::PulseParameters pulse;
    pulse.Low = 0.0;
    pulse.High = 5.0;
    pulse.Delay = 0.0;
    pulse.RiseTime = 1e-9;
    pulse.FallTime = 1e-9;
    pulse.Width = 1.0;
    pulse.Period = 2.0;
    source.SetPulse(pulse);
    Core::Resistor resistor;
    resistor.SetName("R1");
    resistor.SetValue(1e3);
    Core::Capacitor capacitor;
    capacitor.SetName("C1");
    capacitor.SetValue(1e-6);
    Core::Ground ground;
    Core::Circuit circuit;
    circuit.Add(source, {1, 0});
    circuit.Add(resistor, {1, 2});
    circuit.Add(capacitor, {2, 0});
    circuit.Add(ground, {0});

    Core::NodalysBackend backend;
    const Core::Transient result = RunTransient(backend, circuit, /*stop=*/5e-3, /*step=*/5e-6);
    REQUIRE(result.IsComplete);
    // Theory: v(t) = 5 V (1 − e^(−t/RC)) with RC = 1 ms
    const double tau = 1e-3;
    REQUIRE(result.Times.size() == 1001);
    for (std::size_t index = 1; index < result.Times.size(); index += 97) {
        INFO(std::format("t = {}", result.Times[index]));
        const double expected = 5.0 * (1.0 - std::exp(-result.Times[index] / tau));
        REQUIRE_THAT(result.NodeVoltages[2][index], WithinRel(expected, 5e-3));
    }
    // The capacitor current is the operating-point current (0, the capacitor is open) at t = 0, as ngspice reports,
    // and right after takes the backward-Euler value of the first step (Vladimirescu, "The SPICE Book"), which
    // sits 0.5% low here exactly as in ngspice; both decay with the time constant
    const auto current = std::ranges::find(result.Currents, "C1", &Core::ComponentTrace::Name);
    REQUIRE(current != result.Currents.end());
    REQUIRE_THAT(current->Values[0], WithinAbs(0.0, 1e-12));
    REQUIRE_THAT(current->Values[1], WithinRel(5e-3, 1e-2));
    REQUIRE_THAT(current->Values.back(), WithinRel(5e-3 * std::exp(-5.0), 2e-2));
}

TEST_CASE("The transient of an RL circuit follows the exponential current curve") {
    Core::VoltageSource source;
    source.SetName("V1");
    source.SetSourceType(Core::VoltageSource::SourceType::PULSE);
    Core::PulseParameters pulse;
    pulse.Low = 0.0;
    pulse.High = 5.0;
    pulse.Delay = 0.0;
    pulse.RiseTime = 1e-9;
    pulse.FallTime = 1e-9;
    pulse.Width = 1.0;
    pulse.Period = 2.0;
    source.SetPulse(pulse);
    Core::Resistor resistor;
    resistor.SetName("R1");
    resistor.SetValue(100.0);
    Core::Inductor inductor;
    inductor.SetName("L1");
    inductor.SetValue(10e-3);
    Core::Ground ground;
    Core::Circuit circuit;
    circuit.Add(source, {1, 0});
    circuit.Add(resistor, {1, 2});
    circuit.Add(inductor, {2, 0});
    circuit.Add(ground, {0});

    Core::NodalysBackend backend;
    const Core::Transient result = RunTransient(backend, circuit, /*stop=*/1e-3, /*step=*/1e-6);
    REQUIRE(result.IsComplete);
    // Theory: i(t) = V/R (1 − e^(−tR/L)) with L/R = 100 us. The first integration step starts with backward Euler
    // (Vladimirescu, "The SPICE Book"), whose error at t = h decays with e^(−t/τ), so the comparison starts once
    // that transient of the integrator has faded
    const double tau = 100e-6;
    const auto current = std::ranges::find(result.Currents, "L1", &Core::ComponentTrace::Name);
    REQUIRE(current != result.Currents.end());
    for (std::size_t index = 300; index < result.Times.size(); index += 89) {
        INFO(std::format("t = {}", result.Times[index]));
        const double expected = 5.0 / 100.0 * (1.0 - std::exp(-result.Times[index] / tau));
        REQUIRE_THAT(current->Values[index], WithinRel(expected, 5e-3));
    }
}

TEST_CASE("A sine source gives the sine amplitude in a transient") {
    Core::VoltageSource source;
    source.SetName("V1");
    source.SetSourceType(Core::VoltageSource::SourceType::AC);
    Core::ACParameters ac;
    ac.Amplitude = 2.0;
    ac.Frequency = 1e3;
    ac.Offset = 1.0;
    source.SetAC(ac);
    Core::Resistor resistor;
    resistor.SetName("R1");
    Core::Ground ground;
    Core::Circuit circuit;
    circuit.Add(source, {1, 0});
    circuit.Add(resistor, {1, 0});
    circuit.Add(ground, {0});

    Core::NodalysBackend backend;
    const Core::Transient result = RunTransient(backend, circuit, /*stop=*/2e-3, /*step=*/5e-6);
    REQUIRE(result.IsComplete);
    // Steady state on the resistor: v(t) = offset + amplitude sin(2π f t)
    for (std::size_t index = 200; index < result.Times.size(); index += 173) {
        INFO(std::format("t = {}", result.Times[index]));
        const double expected = 1.0 + 2.0 * std::sin(2.0 * std::numbers::pi * 1e3 * result.Times[index]);
        REQUIRE_THAT(result.NodeVoltages[1][index], WithinRel(expected, 1e-3));
    }
}

TEST_CASE("A transient with initial conditions starts from them (uic)") {
    Core::Capacitor capacitor;
    capacitor.SetName("C1");
    capacitor.SetValue(1e-6);
    capacitor.SetInitialCondition(5.0);
    Core::Resistor resistor;
    resistor.SetName("R1");
    resistor.SetValue(1e3);
    Core::Ground ground;
    Core::Circuit circuit;
    circuit.Add(capacitor, {1, 0});
    circuit.Add(resistor, {1, 0});
    circuit.Add(ground, {0});

    Core::NodalysBackend backend;
    const Core::TransientRun run =
        backend.RunTransient(circuit, {.StopTime = 5e-3, .TimeStep = 5e-6}, std::nullopt, {});
    REQUIRE(run.Result.has_value());
    // The charged capacitor discharges through the resistor: v(t) = 5 V e^(−t/RC)
    REQUIRE_THAT(run.Result->NodeVoltages[1].front(), WithinRel(5.0, 1e-4));
    const double tau = 1e-3;
    for (std::size_t index = 1; index < run.Result->Times.size(); index += 97) {
        INFO(std::format("t = {}", run.Result->Times[index]));
        const double expected = 5.0 * std::exp(-run.Result->Times[index] / tau);
        REQUIRE_THAT(run.Result->NodeVoltages[1][index], WithinRel(expected, 5e-3));
    }
    REQUIRE(!run.Result->Warnings.empty());
}

TEST_CASE("A stopped transient returns what it made, incomplete") {
    const Divider divider;
    Core::NodalysBackend backend;
    Core::RunControl control;
    std::size_t chunks = 0;
    const Core::RunMonitor monitor{
        .Control = &control,
        .OnChunk =
            [&](Core::Transient) {
                ++chunks;
                control.Cancel();
            },
    };
    // A long, fine transient the run cannot finish before the cancel of the first chunk
    Core::TransientRun run =
        backend.RunTransient(divider.Circuit, {.StopTime = 10.0, .TimeStep = 1e-6}, std::nullopt, monitor);
    REQUIRE(run.Result.has_value());
    REQUIRE(!run.Result->IsComplete);
    REQUIRE(chunks >= 1);
    REQUIRE(run.Result->Times.size() < 10'000'000);
}

TEST_CASE("A potentiometer reads its three terminal currents") {
    Core::VCC supply;
    supply.SetName("V1");
    supply.SetValue(10.0);
    Core::Potentiometer pot;
    pot.SetName("RV1");
    pot.SetValue(4e3);
    pot.SetWiper(0.25);
    Core::Resistor load;
    load.SetName("R1");
    load.SetValue(1e3);
    Core::Ground ground;
    Core::Circuit circuit;
    circuit.Add(supply, {1});
    circuit.Add(pot, {1, 0, 2});
    circuit.Add(load, {2, 0});
    circuit.Add(ground, {0});

    Core::NodalysBackend backend;
    const Core::OperatingPointRun run = backend.RunOperatingPoint(circuit);
    REQUIRE(run.Result.has_value());
    // The track splits into 1k (end A to wiper) and 3k (wiper to end B); the wiper loaded by 1k to ground sits
    // at 10 V · (3k || 1k) / (1k + 3k || 1k) = 10 · 750 / 1750
    REQUIRE_THAT(run.Result->NodeVoltages[2], WithinRel(10.0 * 750.0 / 1750.0, 1e-5));
    // Kirchhoff over the whole part: the sum of the currents into all three terminals is zero
    const double into_a = CurrentOf(*run.Result, "RV1.A");
    const double into_b = CurrentOf(*run.Result, "RV1.B");
    const double into_w = CurrentOf(*run.Result, "RV1.W");
    REQUIRE_THAT(into_w, WithinRel(-(into_a + into_b), 1e-5));
}

TEST_CASE("A subcircuit instance flattens with its parameters") {
    // A parameterized divider: value parameter scales the bottom resistor
    Core::SubcircuitDefinition definition;
    definition.Name = "Divider";
    Core::SubcircuitPin in;
    in.Name = "in";
    in.Node = 1;
    Core::SubcircuitPin out;
    out.Name = "out";
    out.Node = 2;
    definition.Pins = {in, out};
    definition.Parameters.push_back({"RATIO", 1.0});
    Core::Resistor top;
    top.SetName("R1");
    Core::Resistor bottom;
    bottom.SetName("R2");
    bottom.SetValueExpression("{RATIO}");
    definition.Body.Add(top, {1, 2});
    definition.Body.Add(bottom, {2, 0});

    Core::VCC supply;
    supply.SetName("V1");
    supply.SetValue(12.0);
    Core::SubcircuitInstance instance("Divider");
    instance.SetName("X1");
    instance.SetParameter("RATIO", 3e3);
    Core::Ground ground;
    Core::Circuit circuit;
    circuit.AddSubcircuit(std::make_shared<Core::SubcircuitDefinition>(definition));
    circuit.Add(supply, {1});
    circuit.Add(instance, {1, 2});
    circuit.Add(ground, {0});

    Core::NodalysBackend backend;
    const Core::OperatingPointRun run = backend.RunOperatingPoint(circuit);
    REQUIRE(run.Result.has_value());
    // 12 V over 1k (default top) and 3k (parameter): the output sits at 9 V
    REQUIRE_THAT(run.Result->NodeVoltages[2], WithinRel(9.0, 1e-5));
}

TEST_CASE("A circuit with a digital part simulates its operating point on Nodalys") {
    Core::VCC supply;
    supply.SetName("V1");
    supply.SetValue(5.0);
    Core::LogicPart gate(Core::ComponentType::AND);
    gate.SetName("U1");
    Core::Ground ground;
    Core::Circuit circuit;
    circuit.Add(supply, {1});
    circuit.Add(gate, {1, 1, 2});
    circuit.Add(ground, {0});
    Core::NodalysBackend backend;
    const Core::OperatingPointRun run = backend.RunOperatingPoint(circuit);
    REQUIRE(run.Result.has_value());
    REQUIRE(run.Result->NodeVoltages.size() >= 3);
    CHECK(run.Result->NodeVoltages[2] > 4.8);
}

TEST_CASE("The AC sweep of an RC low-pass filter hits -3 dB and -45 deg at the corner frequency") {
    // f_c = 1 / (2*pi*R*C) with R = 1 kOhm, C = 1 / (2*pi * 1e6) F -> f_c = 1 kHz exactly
    Core::VoltageSource source;
    source.SetName("V1");
    source.SetSourceType(Core::Source::SourceType::AC);
    source.SetAC({.Amplitude = 1.0, .Frequency = 1e3, .Offset = 0.0, .Magnitude = 1.0});
    Core::Resistor resistor;
    resistor.SetName("R1");
    resistor.SetValue(1e3);
    Core::Capacitor capacitor;
    capacitor.SetName("C1");
    capacitor.SetValue(1.0 / (2.0 * std::numbers::pi * 1e6));
    Core::Ground ground;
    Core::Circuit circuit;
    circuit.Add(source, {1, 0});
    circuit.Add(resistor, {1, 2});
    circuit.Add(capacitor, {2, 0});
    circuit.Add(ground, {0});

    Core::NodalysBackend backend;
    const Core::ACSweepRun run = backend.RunACSweep(
        circuit, {.StartFrequency = 10.0, .StopFrequency = 100e3, .PointsPerDecade = 10}, std::nullopt);
    REQUIRE(run.Result.has_value());
    const Core::ACSweep &ac = *run.Result;
    // Index 20 is 10 * 10^(20/10) = 1000 Hz
    REQUIRE(ac.Frequencies.size() == 41);
    REQUIRE_THAT(ac.Frequencies[20], WithinRel(1e3, 1e-12));
    REQUIRE_THAT(ac.NodeMagnitudesDecibels[2][20], WithinAbs(-10.0 * std::log10(2.0), 1e-6));
    REQUIRE_THAT(ac.NodePhasesDegrees[2][20], WithinAbs(-45.0, 1e-5));
}

TEST_CASE("The transfer function of a divider matches its closed-form gain and impedances") {
    const Divider divider;
    Core::NodalysBackend backend;
    const Core::TransferFunctionRun run =
        backend.RunTransferFunction(divider.Circuit, {.OutputNode = 2, .InputSource = "V1"});
    REQUIRE(run.Result.has_value());
    REQUIRE(run.Result->InputUnit == "V");
    // 1k and 3k divider: Gain = 3k / 4k = 0.75, Zin = 1k + 3k = 4k, Zout = 1k || 3k = 750 Ohm
    REQUIRE_THAT(run.Result->Gain, WithinRel(0.75, 1e-6));
    REQUIRE_THAT(run.Result->InputImpedance, WithinRel(4e3, 1e-6));
    REQUIRE_THAT(run.Result->OutputImpedance, WithinRel(750.0, 1e-6));
}

TEST_CASE("The noise analysis of an RC low-pass filter matches the Johnson-Nyquist kT/C integral") {
    // R = 1 kOhm, C = 1 uF -> f_c = 1 / (2*pi*1e-3) ≈ 159.155 Hz
    // Low-frequency thermal noise density of 1 kOhm at 300.15 K: sqrt(4 * k * T * R) ≈ 4.07152 nV/sqrt(Hz)
    // Total integrated RMS noise from 0.1 Hz to 10 MHz approaches sqrt(k * T / C) ≈ 64.375 uV RMS
    Core::VoltageSource source;
    source.SetName("V1");
    source.SetSourceType(Core::Source::SourceType::AC);
    source.SetAC({.Amplitude = 1.0, .Frequency = 1e3, .Offset = 0.0, .Magnitude = 1.0});
    Core::Resistor resistor;
    resistor.SetName("R1");
    resistor.SetValue(1e3);
    Core::Capacitor capacitor;
    capacitor.SetName("C1");
    capacitor.SetValue(1e-6);
    Core::Ground ground;
    Core::Circuit circuit;
    circuit.Add(source, {1, 0});
    circuit.Add(resistor, {1, 2});
    circuit.Add(capacitor, {2, 0});
    circuit.Add(ground, {0});

    Core::NodalysBackend backend;
    const Core::NoiseRun run = backend.RunNoise(
        circuit,
        {.OutputNode = 2, .InputSource = "V1", .StartFrequency = 0.1, .StopFrequency = 10e6, .PointsPerDecade = 20});
    REQUIRE(run.Result.has_value());
    const Core::Noise &noise = *run.Result;
    REQUIRE(noise.InputUnit == "V");
    constexpr double k_t = 1.380649e-23 * 300.15;
    const double expected_low_freq_density = std::sqrt(4.0 * k_t * 1e3);
    REQUIRE_THAT(noise.OutputDensities.front(), WithinRel(expected_low_freq_density, 1e-5));
    REQUIRE_THAT(noise.InputDensities.front(), WithinRel(expected_low_freq_density, 1e-5));
    const double expected_kt_over_c = std::sqrt(k_t / 1e-6);
    REQUIRE_THAT(noise.OutputTotal, WithinRel(expected_kt_over_c, 1e-3));
    REQUIRE(noise.Contributions.size() == 1);
    REQUIRE(noise.Contributions[0].Name == "R1");
    REQUIRE_THAT(noise.Contributions[0].OutputTotal, WithinRel(noise.OutputTotal, 1e-9));
}

TEST_CASE("A D flip-flop with QN fed back runs on Nodalys", "[nodalys]") {
    Core::LogicPart dff(Core::ComponentType::D_FLIP_FLOP);
    dff.SetName("U1");
    Core::Clock clk;
    clk.SetName("V1");
    Core::Ground gnd;
    Core::Circuit circuit;
    circuit.Add(clk, {2, 0});
    circuit.Add(dff, {1, 2, 3, 4, 5, 1});
    circuit.Add(gnd, {0});
    Core::NodalysBackend backend;
    auto op = backend.RunOperatingPoint(circuit);
    const std::string msg = op.Result.has_value() ? std::string("OK") : op.Result.error();
    INFO(msg);
    CHECK(op.Result.has_value());
}

TEST_CASE("The Nodalys backend computes digital truth tables directly", "[nodalys][digital]") {
    struct BackendRestorer {
        ~BackendRestorer() { Core::SetBackend(nullptr); }
    } restorer;

    Core::SetBackend(std::make_unique<Core::NodalysBackend>());
    REQUIRE(Core::GetBackend().GetName() == "Nodalys");

    Core::LogicInput input_a;
    input_a.SetName("V1");
    input_a.SetHigh(true);
    Core::LogicInput input_b;
    input_b.SetName("V2");
    input_b.SetHigh(false);

    Core::LogicPart xor_gate(Core::ComponentType::XOR);
    xor_gate.SetName("U1");
    Core::LogicPart and_gate(Core::ComponentType::AND);
    and_gate.SetName("U2");

    // Half adder: inputs on 1, 2; XOR sum on 3; AND carry on 4; ground on 0
    Core::Ground ground;
    Core::Circuit circuit;
    circuit.Add(input_a, {1});
    circuit.Add(input_b, {2});
    circuit.Add(xor_gate, {1, 2, 3});
    circuit.Add(and_gate, {1, 2, 4});
    circuit.Add(ground, {0});

    const Core::TruthTableRun run = Core::RunTruthTable(circuit, {.Inputs = {"V1", "V2"}, .OutputNodes = {3, 4}});
    REQUIRE(run.Result.has_value());
    const Core::TruthTable &table = *run.Result;
    REQUIRE(table.Rows.size() == 4);

    // Row 0: 00 -> Sum = 0, Carry = 0
    CHECK(table.Rows[0].OutputVoltages[0] < 0.5);
    CHECK(table.Rows[0].OutputVoltages[1] < 0.5);

    // Row 1: 01 -> Sum = 1, Carry = 0
    CHECK(table.Rows[1].OutputVoltages[0] > 4.5);
    CHECK(table.Rows[1].OutputVoltages[1] < 0.5);

    // Row 2: 10 -> Sum = 1, Carry = 0
    CHECK(table.Rows[2].OutputVoltages[0] > 4.5);
    CHECK(table.Rows[2].OutputVoltages[1] < 0.5);

    // Row 3: 11 -> Sum = 0, Carry = 1
    CHECK(table.Rows[3].OutputVoltages[0] < 0.5);
    CHECK(table.Rows[3].OutputVoltages[1] > 4.5);
}
