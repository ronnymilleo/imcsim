/**
 * @file    backend_comparison_tests.cpp
 * @brief   Parity tests between the ngspice and the native backend: the same circuit through both, and the results
 *          compared node by node and current by current.
 * @details This is the second validation layer of the plan (the first is the hand-checked theory of
 *          native_backend_tests.cpp, nonlinear_tests.cpp and op_amp_tests.cpp): the native backend must reproduce
 *          what ngspice produces today, within the solver tolerances. The operating point and the DC sweep compare
 *          at 1e-4 relative; the transient interpolates the ngspice trace at the native time points and compares at
 *          1e-3 relative plus an absolute floor, skipping the first points, where the first-step backward Euler of
 *          the transient start has not yet decayed into the trapezoidal trajectory.
 */

#include "components/bjt.h"
#include "components/capacitor.h"
#include "components/controlled_source.h"
#include "components/diode.h"
#include "components/ground.h"
#include "components/inductor.h"
#include "components/mosfet.h"
#include "components/op_amp.h"
#include "components/potentiometer.h"
#include "components/resistor.h"
#include "components/transformer.h"
#include "components/vcc.h"
#include "components/voltage_controlled_switch.h"
#include "components/voltage_source.h"
#include "native/native_backend.h"
#include "ngspice/ngspice_backend.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <cmath>
#include <complex>
#include <format>
#include <numbers>
#include <string>

namespace {

/// Relative plus absolute closeness, for values that may sit near zero.
bool Close(const double a, const double b, const double relative, const double absolute) {
    return std::fabs(a - b) <= relative * std::max(std::fabs(a), std::fabs(b)) + absolute;
}

const std::vector<double> *TraceOf(const std::vector<Core::ComponentTrace> &traces, const std::string &name) {
    const auto found = std::ranges::find(traces, name, &Core::ComponentTrace::Name);
    return found != traces.end() ? &found->Values : nullptr;
}

double CurrentOf(const Core::OperatingPoint &point, const std::string &name) {
    const auto found = std::ranges::find(point.Currents, name, &Core::ComponentCurrent::Name);
    REQUIRE(found != point.Currents.end());
    return found->Current;
}

/**
 * @brief   The ngspice backend of the whole test file: the library admits one instance per process, shared by
 *          every comparison.
 */
Core::NgspiceBackend &Ngspice() {
    static Core::NgspiceBackend backend;
    return backend;
}

/**
 * @brief   Runs the operating point through both backends and compares every node voltage and every current.
 */
void CheckOperatingPointParity(const Core::Circuit &circuit) {
    Core::NativeBackend native;
    const Core::OperatingPointRun expected = Ngspice().RunOperatingPoint(circuit);
    REQUIRE(expected.Result.has_value());
    const Core::OperatingPointRun actual = native.RunOperatingPoint(circuit);
    INFO("native: " + (actual.Result ? std::string("ok") : actual.Result.error()));
    REQUIRE(actual.Result.has_value());
    REQUIRE(actual.Result->NodeVoltages.size() == expected.Result->NodeVoltages.size());
    for (std::size_t node = 1; node < expected.Result->NodeVoltages.size(); ++node) {
        INFO(std::format("node {}: ngspice {:.4e} V, native {:.4e} V", node, expected.Result->NodeVoltages[node],
                         actual.Result->NodeVoltages[node]));
        REQUIRE(Close(actual.Result->NodeVoltages[node], expected.Result->NodeVoltages[node], 1e-4, 1e-6));
    }
    for (const Core::ComponentCurrent &current : expected.Result->Currents) {
        const auto found = std::ranges::find(actual.Result->Currents, current.Name, &Core::ComponentCurrent::Name);
        REQUIRE(found != actual.Result->Currents.end());
        INFO(std::format("current {}: ngspice {:.4e} A, native {:.4e} A", current.Name, current.Current,
                         found->Current));
        REQUIRE(Close(found->Current, current.Current, 1e-3, 1e-9));
    }
}

/**
 * @brief   Runs a DC sweep through both backends and compares every swept point, node voltage, and current.
 */
void CheckDCSweepParity(const Core::Circuit &circuit, const Core::DCSweepSettings &settings) {
    Core::NativeBackend native;
    const Core::DCSweepRun expected = Ngspice().RunDCSweep(circuit, settings, std::nullopt);
    REQUIRE(expected.Result.has_value());
    const Core::DCSweepRun actual = native.RunDCSweep(circuit, settings, std::nullopt);
    INFO("native: " + (actual.Result ? std::string("ok") : actual.Result.error()));
    REQUIRE(actual.Result.has_value());
    REQUIRE(actual.Result->SweptValues.size() == expected.Result->SweptValues.size());
    REQUIRE(actual.Result->Curves.size() == expected.Result->Curves.size());
    for (std::size_t c = 0; c < expected.Result->Curves.size(); ++c) {
        const Core::DCSweepCurve &exp_curve = expected.Result->Curves[c];
        const Core::DCSweepCurve &act_curve = actual.Result->Curves[c];
        REQUIRE(act_curve.NodeVoltages.size() == exp_curve.NodeVoltages.size());
        for (std::size_t node = 1; node < exp_curve.NodeVoltages.size(); ++node) {
            for (std::size_t i = 0; i < expected.Result->SweptValues.size(); ++i) {
                INFO(std::format("sweep[{}] node {}: ngspice {:.4e} V, native {:.4e} V", i, node,
                                 exp_curve.NodeVoltages[node][i], act_curve.NodeVoltages[node][i]));
                REQUIRE(Close(act_curve.NodeVoltages[node][i], exp_curve.NodeVoltages[node][i], 1e-3, 1e-6));
            }
        }
        for (const Core::ComponentTrace &current : exp_curve.Currents) {
            const std::vector<double> *act_values = TraceOf(act_curve.Currents, current.Name);
            REQUIRE(act_values != nullptr);
            for (std::size_t i = 0; i < expected.Result->SweptValues.size(); ++i) {
                INFO(std::format("sweep[{}] current {}: ngspice {:.4e} A, native {:.4e} A", i, current.Name,
                                 current.Values[i], (*act_values)[i]));
                REQUIRE(Close((*act_values)[i], current.Values[i], 1e-3, 1e-9));
            }
        }
    }
}

/**
 * @brief   Linear interpolation of a trace at an arbitrary time.
 */
double Interpolate(const Core::Transient &trace, const std::vector<double> &values, const double time) {
    const auto next = std::ranges::lower_bound(trace.Times, time);
    if (next == trace.Times.begin()) {
        return values.front();
    }
    if (next == trace.Times.end()) {
        return values.back();
    }
    const std::size_t right = static_cast<std::size_t>(next - trace.Times.begin());
    const double t0 = trace.Times[right - 1];
    const double t1 = trace.Times[right];
    const double share = (time - t0) / (t1 - t0);
    return values[right - 1] + share * (values[right] - values[right - 1]);
}

/**
 * @brief   Runs the transient through both backends and compares every trace at the native time points.
 * @param[in] skip  Points to skip at the start, while the first-step backward Euler error decays.
 */
void CheckTransientParity(const Core::Circuit &circuit, const Core::TransientSettings &settings,
                          const std::size_t skip = 2) {
    Core::NativeBackend native;
    const Core::TransientRun expected = Ngspice().RunTransient(circuit, settings, std::nullopt, {});
    INFO("ngspice: " + (expected.Result ? std::string("ok") : expected.Result.error()));
    REQUIRE(expected.Result.has_value());
    const Core::TransientRun actual = native.RunTransient(circuit, settings, std::nullopt, {});
    INFO("native: " + (actual.Result ? std::string("ok") : actual.Result.error()));
    REQUIRE(actual.Result.has_value());
    REQUIRE(actual.Result->IsComplete);
    REQUIRE(actual.Result->NodeVoltages.size() == expected.Result->NodeVoltages.size());
    for (std::size_t index = skip; index < actual.Result->Times.size(); ++index) {
        const double time = actual.Result->Times[index];
        for (std::size_t node = 1; node < expected.Result->NodeVoltages.size(); ++node) {
            const double wanted = Interpolate(*expected.Result, expected.Result->NodeVoltages[node], time);
            INFO(std::format("node {} at t = {:.3g}: ngspice {:.4e} V, native {:.4e} V", node, time, wanted,
                             actual.Result->NodeVoltages[node][index]));
            REQUIRE(Close(actual.Result->NodeVoltages[node][index], wanted, 1e-3, 1e-3));
        }
    }
    for (const Core::ComponentTrace &current : expected.Result->Currents) {
        const std::vector<double> *actual_values = TraceOf(actual.Result->Currents, current.Name);
        INFO(std::format("current {}", current.Name));
        REQUIRE(actual_values != nullptr);
        for (std::size_t index = skip; index < actual.Result->Times.size(); ++index) {
            const double wanted = Interpolate(*expected.Result, current.Values, actual.Result->Times[index]);
            INFO(std::format("current {} at t = {:.3g}: ngspice {:.4e} A, native {:.4e} A", current.Name,
                             actual.Result->Times[index], wanted, (*actual_values)[index]));
            REQUIRE(Close((*actual_values)[index], wanted, 1e-3, 1e-6));
        }
    }
}

/**
 * @brief   Runs an AC sweep through both backends and compares frequencies, node magnitudes/phases, and currents.
 */
void CheckACSweepParity(const Core::Circuit &circuit, const Core::ACSweepSettings &settings) {
    Core::NativeBackend native;
    const Core::ACSweepRun expected = Ngspice().RunACSweep(circuit, settings, std::nullopt);
    INFO("ngspice: " + (expected.Result ? std::string("ok") : expected.Result.error()));
    REQUIRE(expected.Result.has_value());
    const Core::ACSweepRun actual = native.RunACSweep(circuit, settings, std::nullopt);
    INFO("native: " + (actual.Result ? std::string("ok") : actual.Result.error()));
    REQUIRE(actual.Result.has_value());
    REQUIRE(actual.Result->Frequencies.size() == expected.Result->Frequencies.size());
    REQUIRE(actual.Result->NodeMagnitudesDecibels.size() == expected.Result->NodeMagnitudesDecibels.size());
    for (std::size_t i = 0; i < expected.Result->Frequencies.size(); ++i) {
        REQUIRE(Close(actual.Result->Frequencies[i], expected.Result->Frequencies[i], 1e-9, 1e-12));
        for (std::size_t node = 1; node < expected.Result->NodeMagnitudesDecibels.size(); ++node) {
            const double exp_mag = expected.Result->NodeMagnitudesDecibels[node][i];
            const double act_mag = actual.Result->NodeMagnitudesDecibels[node][i];
            INFO(std::format("f = {:.3g} Hz node {} mag: ngspice {:.4f} dB, native {:.4f} dB",
                             expected.Result->Frequencies[i], node, exp_mag, act_mag));
            if (exp_mag > -200.0) {
                REQUIRE(Close(act_mag, exp_mag, 1e-3, 0.05));
                double phase_diff = std::fmod(
                    actual.Result->NodePhasesDegrees[node][i] - expected.Result->NodePhasesDegrees[node][i], 360.0);
                if (phase_diff > 180.0) {
                    phase_diff -= 360.0;
                } else if (phase_diff < -180.0) {
                    phase_diff += 360.0;
                }
                INFO(std::format("f = {:.3g} Hz node {} phase: ngspice {:.4f} deg, native {:.4f} deg",
                                 expected.Result->Frequencies[i], node, expected.Result->NodePhasesDegrees[node][i],
                                 actual.Result->NodePhasesDegrees[node][i]));
                REQUIRE(std::fabs(phase_diff) <= 0.5);
            }
        }
    }
    for (const Core::ComponentTrace &exp_cur : expected.Result->CurrentMagnitudesDecibels) {
        const std::vector<double> *act_mag = TraceOf(actual.Result->CurrentMagnitudesDecibels, exp_cur.Name);
        INFO(std::format("AC current {}", exp_cur.Name));
        REQUIRE(act_mag != nullptr);
        for (std::size_t i = 0; i < expected.Result->Frequencies.size(); ++i) {
            if (exp_cur.Values[i] > -200.0) {
                INFO(std::format("f = {:.3g} Hz current {} mag: ngspice {:.4f} dB, native {:.4f} dB",
                                 expected.Result->Frequencies[i], exp_cur.Name, exp_cur.Values[i], (*act_mag)[i]));
                REQUIRE(Close((*act_mag)[i], exp_cur.Values[i], 1e-3, 0.05));
            }
        }
    }
}

} // namespace

TEST_CASE("Both backends agree on the operating point of a divider") {
    Core::VCC supply;
    supply.SetName("V1");
    supply.SetValue(12.0);
    Core::Resistor top;
    top.SetName("R1");
    top.SetValue(1e3);
    Core::Resistor bottom;
    bottom.SetName("R2");
    bottom.SetValue(2.2e3);
    Core::Ground ground;
    Core::Circuit circuit;
    circuit.Add(supply, {1});
    circuit.Add(top, {1, 2});
    circuit.Add(bottom, {2, 0});
    circuit.Add(ground, {0});
    CheckOperatingPointParity(circuit);
    CheckDCSweepParity(circuit, {.Swept = {.Source = "V1", .Start = 0.0, .Stop = 12.0, .Step = 2.0}});
}

TEST_CASE("Both backends agree on the operating point and transient of an RC charge") {
    Core::VCC supply;
    supply.SetName("V1");
    supply.SetValue(5.0);
    Core::Resistor series;
    series.SetName("R1");
    series.SetValue(1e3);
    Core::Capacitor load;
    load.SetName("C1");
    load.SetValue(1e-6);
    Core::Ground ground;
    Core::Circuit circuit;
    circuit.Add(supply, {1});
    circuit.Add(series, {1, 2});
    circuit.Add(load, {2, 0});
    circuit.Add(ground, {0});
    CheckOperatingPointParity(circuit);
    CheckTransientParity(circuit, {.StopTime = 5e-3, .TimeStep = 20e-6});
}

TEST_CASE("Both backends agree on the operating point and transient of an RL step") {
    Core::VoltageSource input;
    input.SetName("V1");
    input.SetValue(1.0); // DC 1 V step for the transient
    Core::Resistor series;
    series.SetName("R1");
    series.SetValue(10.0);
    Core::Inductor coil;
    coil.SetName("L1");
    coil.SetValue(10e-3);
    Core::Ground ground;
    Core::Circuit circuit;
    circuit.Add(input, {1, 0});
    circuit.Add(series, {1, 2});
    circuit.Add(coil, {2, 0});
    circuit.Add(ground, {0});
    CheckOperatingPointParity(circuit);
    CheckTransientParity(circuit, {.StopTime = 10e-3, .TimeStep = 20e-6});
}

TEST_CASE("Both backends agree on the operating point and transient of a diode rectifier") {
    Core::VoltageSource input;
    input.SetName("V1");
    input.SetSourceType(Core::VoltageSource::SourceType::AC);
    input.SetAC({.Amplitude = 5.0, .Frequency = 1e3, .Offset = 0.0});
    Core::Resistor series;
    series.SetName("R1");
    series.SetValue(1e3);
    Core::Diode diode(Core::ComponentType::DIODE);
    diode.SetName("D1");
    Core::Ground ground;
    Core::Circuit circuit;
    circuit.Add(input, {1, 0});
    circuit.Add(series, {1, 2});
    circuit.Add(diode, {2, 0});
    circuit.Add(ground, {0});
    CheckOperatingPointParity(circuit);
    CheckDCSweepParity(circuit, {.Swept = {.Source = "V1", .Start = -2.0, .Stop = 2.0, .Step = 0.25}});
    // A 2 us step bounds the linear-interpolation chord error of the ngspice reference across the exponential knee
    CheckTransientParity(circuit, {.StopTime = 5e-3, .TimeStep = 2e-6});
}

TEST_CASE("Both backends agree on the operating point and transient of a common-emitter amplifier") {
    // A 2N3904 with stiff divider bias and a sine input within its linear range
    Core::BJT transistor(Core::ComponentType::NPN);
    transistor.SetName("Q1");
    Core::VCC supply;
    supply.SetName("V1");
    supply.SetValue(10.0);
    Core::Resistor collector;
    collector.SetName("RC");
    collector.SetValue(4.7e3);
    Core::Resistor base_top;
    base_top.SetName("RB1");
    base_top.SetValue(47e3);
    Core::Resistor base_bottom;
    base_bottom.SetName("RB2");
    base_bottom.SetValue(10e3);
    Core::Resistor emitter;
    emitter.SetName("RE");
    emitter.SetValue(1e3);
    Core::VoltageSource input;
    input.SetName("Vin1");
    input.SetSourceType(Core::VoltageSource::SourceType::AC);
    input.SetAC({.Amplitude = 10e-3, .Frequency = 1e3, .Offset = 0.0});
    Core::Capacitor coupling;
    coupling.SetName("C1");
    coupling.SetValue(1e-6);
    Core::Ground ground;
    Core::Circuit circuit;
    circuit.Add(supply, {1});
    circuit.Add(collector, {1, 2});
    circuit.Add(transistor, {2, 3, 4});
    circuit.Add(base_top, {1, 3});
    circuit.Add(base_bottom, {3, 0});
    circuit.Add(emitter, {4, 0});
    circuit.Add(input, {5, 0});
    circuit.Add(coupling, {5, 3});
    circuit.Add(ground, {0});
    CheckOperatingPointParity(circuit);
    CheckTransientParity(circuit, {.StopTime = 5e-3, .TimeStep = 10e-6});
}

TEST_CASE("Both backends agree on the operating point of a common-source MOSFET amplifier") {
    Core::MOSFET transistor(Core::ComponentType::NMOS);
    transistor.SetName("M1");
    Core::VCC supply;
    supply.SetName("V1");
    supply.SetValue(10.0);
    Core::Resistor drain;
    drain.SetName("RD");
    drain.SetValue(1e3);
    Core::Resistor gate_top;
    gate_top.SetName("RG1");
    gate_top.SetValue(100e3);
    Core::Resistor gate_bottom;
    gate_bottom.SetName("RG2");
    gate_bottom.SetValue(50e3);
    Core::Ground ground;
    Core::Circuit circuit;
    circuit.Add(supply, {1});
    circuit.Add(drain, {1, 2});
    circuit.Add(transistor, {2, 3, 0});
    circuit.Add(gate_top, {1, 3});
    circuit.Add(gate_bottom, {3, 0});
    circuit.Add(ground, {0});
    CheckOperatingPointParity(circuit);
    CheckDCSweepParity(circuit, {.Swept = {.Source = "V1", .Start = 0.0, .Stop = 12.0, .Step = 2.0}});
}

TEST_CASE("Both backends agree on the operating point and transient of an ideal op-amp amplifier") {
    Core::VoltageSource input;
    input.SetName("Vin1");
    input.SetValue(0.4);
    Core::Resistor in;
    in.SetName("R1");
    in.SetValue(1e3);
    Core::Resistor feedback;
    feedback.SetName("R2");
    feedback.SetValue(10e3);
    Core::OpAmp amplifier;
    amplifier.SetName("U1");
    Core::VCC positive;
    positive.SetName("V1");
    positive.SetValue(15.0);
    Core::VCC negative;
    negative.SetName("V2");
    negative.SetValue(-15.0);
    Core::Ground ground;
    Core::Circuit circuit;
    circuit.Add(input, {1, 0});
    circuit.Add(in, {1, 2});
    circuit.Add(feedback, {2, 3});
    circuit.Add(amplifier, {3, 0, 2, 4, 5});
    circuit.Add(positive, {4});
    circuit.Add(negative, {5});
    circuit.Add(ground, {0});
    CheckOperatingPointParity(circuit);
    CheckTransientParity(circuit, {.StopTime = 1e-3, .TimeStep = 2e-6});
}

TEST_CASE("Both backends agree on the operating point and transient of a uA741 follower") {
    Core::VoltageSource input;
    input.SetName("Vin1");
    input.SetSourceType(Core::VoltageSource::SourceType::AC);
    input.SetAC({.Amplitude = 1.0, .Frequency = 1e3, .Offset = 0.0});
    Core::OpAmp follower;
    follower.SetName("U1");
    follower.SetModel(*Core::FindOpAmpModel(Core::ComponentType::OP_AMP, "uA741"));
    Core::VCC positive;
    positive.SetName("V1");
    positive.SetValue(15.0);
    Core::VCC negative;
    negative.SetName("V2");
    negative.SetValue(-15.0);
    Core::Ground ground;
    Core::Circuit circuit;
    circuit.Add(input, {1, 0});
    circuit.Add(follower, {2, 1, 2, 3, 4});
    circuit.Add(positive, {3});
    circuit.Add(negative, {4});
    circuit.Add(ground, {0});
    CheckOperatingPointParity(circuit);
    CheckTransientParity(circuit, {.StopTime = 2e-3, .TimeStep = 5e-6});
}

TEST_CASE("Both backends agree on the operating point and transient of a transformer") {
    Core::VoltageSource source;
    source.SetName("V1");
    source.SetSourceType(Core::VoltageSource::SourceType::AC);
    source.SetAC({.Amplitude = 10.0, .Frequency = 1e3, .Offset = 0.0});
    Core::Transformer transformer;
    transformer.SetName("T1");
    transformer.SetValue(1.0);
    Core::TransformerParameters figures;
    figures.TurnsRatio = 2.0;
    figures.Coupling = 0.999;
    figures.WindingResistance = 0.1;
    transformer.SetParameters(figures);
    Core::Resistor load;
    load.SetName("R1");
    load.SetValue(1e3);
    Core::Ground ground;
    Core::Circuit circuit;
    circuit.Add(source, {1, 0});
    circuit.Add(transformer, {1, 0, 2, 0});
    circuit.Add(load, {2, 0});
    circuit.Add(ground, {0});
    CheckOperatingPointParity(circuit);
    CheckTransientParity(circuit, {.StopTime = 5e-3, .TimeStep = 2e-6});
}

TEST_CASE("Both backends agree on the operating point of controlled sources") {
    Core::VCC supply;
    supply.SetName("V1");
    supply.SetValue(5.0);
    Core::Resistor control;
    control.SetName("R1");
    control.SetValue(1e3);
    Core::Resistor load;
    load.SetName("R2");
    load.SetValue(1e3);
    Core::VoltageSource vcvs_input;
    vcvs_input.SetName("V2");
    vcvs_input.SetValue(1.0);
    Core::ControlledSource vcvs(Core::ComponentType::VCVS);
    vcvs.SetName("E1");
    vcvs.SetValue(2.0);
    Core::ControlledSource vccs(Core::ComponentType::VCCS);
    vccs.SetName("G1");
    vccs.SetValue(1e-3);
    Core::Resistor sink;
    sink.SetName("R3");
    sink.SetValue(2.2e3); // gives the VCCS current a DC path
    Core::Ground ground;
    Core::Circuit circuit;
    circuit.Add(supply, {1});
    circuit.Add(control, {1, 2}); // v(2) = 5 V, the control of both sources
    circuit.Add(vcvs_input, {3, 0});
    circuit.Add(vcvs, {4, 0, 3, 0}); // v(4) = 2 V
    circuit.Add(vccs, {0, 5, 2, 0}); // pulls 5 mA out of node 5... positive into node 0
    circuit.Add(sink, {5, 0});
    circuit.Add(load, {4, 0});
    circuit.Add(ground, {0});
    CheckOperatingPointParity(circuit);
    CheckDCSweepParity(circuit, {.Swept = {.Source = "V1", .Start = 0.0, .Stop = 12.0, .Step = 2.0}});
}

TEST_CASE("Both backends agree on the operating point of a voltage-controlled switch") {
    const double control = GENERATE(0.0, 5.0);
    CAPTURE(control);
    Core::VCC supply;
    supply.SetName("V1");
    supply.SetValue(10.0);
    Core::Resistor load;
    load.SetName("R1");
    load.SetValue(1e3);
    Core::VoltageSource control_source;
    control_source.SetName("V2");
    control_source.SetValue(control);
    Core::VoltageControlledSwitch switch_part;
    switch_part.SetName("S1");
    Core::SwitchParameters parameters;
    parameters.Threshold = 2.5;
    parameters.Hysteresis = 0.1;
    parameters.OnResistance = 0.01;
    switch_part.SetParameters(parameters);
    Core::Ground ground;
    Core::Circuit circuit;
    circuit.Add(supply, {1});
    circuit.Add(load, {1, 2});
    circuit.Add(switch_part, {2, 0, 3, 0});
    circuit.Add(control_source, {3, 0});
    circuit.Add(ground, {0});
    CheckOperatingPointParity(circuit);
    CheckDCSweepParity(circuit, {.Swept = {.Source = "V1", .Start = 0.0, .Stop = 12.0, .Step = 2.0}});
}

TEST_CASE("Both backends agree on the operating point of a potentiometer divider") {
    Core::VCC supply;
    supply.SetName("V1");
    supply.SetValue(10.0);
    Core::Potentiometer pot;
    pot.SetName("RV1");
    pot.SetValue(10e3);
    pot.SetWiper(0.7);
    Core::Resistor load;
    load.SetName("R1");
    load.SetValue(10e3);
    Core::Ground ground;
    Core::Circuit circuit;
    circuit.Add(supply, {1});
    circuit.Add(pot, {1, 0, 2});
    circuit.Add(load, {2, 0});
    circuit.Add(ground, {0});
    CheckOperatingPointParity(circuit);
    CheckDCSweepParity(circuit, {.Swept = {.Source = "V1", .Start = 0.0, .Stop = 12.0, .Step = 2.0}});
}

TEST_CASE("Both backends agree on the AC sweep of passive, semiconductor, and op-amp circuits") {
    const Core::ACSweepSettings sweep_settings{.StartFrequency = 10.0, .StopFrequency = 1e6, .PointsPerDecade = 10};

    SECTION("RC low-pass and series RLC filters") {
        Core::VoltageSource source;
        source.SetName("V1");
        source.SetSourceType(Core::Source::SourceType::AC);
        source.SetAC({.Amplitude = 1.0, .Frequency = 1e3, .Offset = 0.0, .Magnitude = 1.0});
        Core::Resistor r1;
        r1.SetName("R1");
        r1.SetValue(100.0);
        Core::Inductor l1;
        l1.SetName("L1");
        l1.SetValue(10e-3);
        Core::Capacitor c1;
        c1.SetName("C1");
        c1.SetValue(100e-9);
        Core::Ground ground;
        Core::Circuit circuit;
        circuit.Add(source, {1, 0});
        circuit.Add(r1, {1, 2});
        circuit.Add(l1, {2, 3});
        circuit.Add(c1, {3, 0});
        circuit.Add(ground, {0});
        CheckACSweepParity(circuit, sweep_settings);
    }

    SECTION("Common-emitter BJT amplifier with junction capacitances") {
        Core::VCC vcc;
        vcc.SetName("V1");
        vcc.SetValue(12.0);
        Core::VoltageSource ac_in;
        ac_in.SetName("V2");
        ac_in.SetSourceType(Core::Source::SourceType::AC);
        ac_in.SetAC({.Amplitude = 10e-3, .Frequency = 1e3, .Offset = 1.5, .Magnitude = 1.0});
        Core::Resistor rb;
        rb.SetName("R1");
        rb.SetValue(10e3);
        Core::Resistor rc;
        rc.SetName("R2");
        rc.SetValue(2.2e3);
        Core::Resistor re;
        re.SetName("R3");
        re.SetValue(470.0);
        Core::BJT q1(Core::ComponentType::NPN);
        q1.SetName("Q1");
        Core::Ground ground;
        Core::Circuit circuit;
        circuit.Add(vcc, {1});
        circuit.Add(ac_in, {2, 0});
        circuit.Add(rb, {2, 3});
        circuit.Add(rc, {1, 4});
        circuit.Add(re, {5, 0});
        circuit.Add(q1, {4, 3, 5});
        circuit.Add(ground, {0});
        CheckACSweepParity(circuit, {.StartFrequency = 100.0, .StopFrequency = 100e6, .PointsPerDecade = 5});
    }

    SECTION("Non-inverting uA741 op-amp amplifier") {
        Core::VCC vcc;
        vcc.SetName("V1");
        vcc.SetValue(15.0);
        Core::VoltageSource vee;
        vee.SetName("V2");
        vee.SetValue(-15.0);
        Core::VoltageSource ac_in;
        ac_in.SetName("V3");
        ac_in.SetSourceType(Core::Source::SourceType::AC);
        ac_in.SetAC({.Amplitude = 0.1, .Frequency = 1e3, .Offset = 0.0, .Magnitude = 1.0});
        Core::Resistor r1;
        r1.SetName("R1");
        r1.SetValue(1e3);
        Core::Resistor r2;
        r2.SetName("R2");
        r2.SetValue(9e3);
        Core::OpAmp op_amp;
        op_amp.SetName("U1");
        op_amp.SetModel(*Core::FindOpAmpModel(Core::ComponentType::OP_AMP, "uA741"));
        Core::Ground ground;
        Core::Circuit circuit;
        circuit.Add(vcc, {1});
        circuit.Add(vee, {2, 0});
        circuit.Add(ac_in, {3, 0});
        circuit.Add(r1, {4, 0});
        circuit.Add(r2, {5, 4});
        circuit.Add(op_amp, {5, 3, 4, 1, 2});
        circuit.Add(ground, {0});
        CheckACSweepParity(circuit, sweep_settings);
    }
}

TEST_CASE("Both backends agree on the small-signal transfer function and impedances") {
    auto check_tf_parity = [](const Core::Circuit &circuit, const Core::TransferFunctionSettings &settings) {
        Core::NativeBackend native;
        const Core::TransferFunctionRun expected = Ngspice().RunTransferFunction(circuit, settings);
        REQUIRE(expected.Result.has_value());
        const Core::TransferFunctionRun actual = native.RunTransferFunction(circuit, settings);
        INFO("native: " + (actual.Result ? std::string("ok") : actual.Result.error()));
        REQUIRE(actual.Result.has_value());
        REQUIRE(actual.Result->InputUnit == expected.Result->InputUnit);
        INFO(std::format("Gain: ngspice {:.6e}, native {:.6e}", expected.Result->Gain, actual.Result->Gain));
        REQUIRE(Close(actual.Result->Gain, expected.Result->Gain, 1e-4, 1e-6));
        INFO(std::format("Zin: ngspice {:.6e}, native {:.6e}", expected.Result->InputImpedance,
                         actual.Result->InputImpedance));
        REQUIRE(Close(actual.Result->InputImpedance, expected.Result->InputImpedance, 1e-4, 1e-6));
        INFO(std::format("Zout: ngspice {:.6e}, native {:.6e}", expected.Result->OutputImpedance,
                         actual.Result->OutputImpedance));
        REQUIRE(Close(actual.Result->OutputImpedance, expected.Result->OutputImpedance, 1e-4, 5e-5));
    };

    SECTION("Resistive divider") {
        Core::VCC supply;
        supply.SetName("V1");
        supply.SetValue(12.0);
        Core::Resistor r1;
        r1.SetName("R1");
        r1.SetValue(1e3);
        Core::Resistor r2;
        r2.SetName("R2");
        r2.SetValue(2.2e3);
        Core::Ground ground;
        Core::Circuit circuit;
        circuit.Add(supply, {1});
        circuit.Add(r1, {1, 2});
        circuit.Add(r2, {2, 0});
        circuit.Add(ground, {0});
        check_tf_parity(circuit, {.OutputNode = 2, .InputSource = "V1"});
    }

    SECTION("BJT emitter follower") {
        Core::VCC vcc;
        vcc.SetName("V1");
        vcc.SetValue(12.0);
        Core::VoltageSource vin;
        vin.SetName("V2");
        vin.SetValue(5.0);
        Core::Resistor rb;
        rb.SetName("R1");
        rb.SetValue(10e3);
        Core::Resistor re;
        re.SetName("R2");
        re.SetValue(1e3);
        Core::BJT q1(Core::ComponentType::NPN);
        q1.SetName("Q1");
        Core::Ground ground;
        Core::Circuit circuit;
        circuit.Add(vcc, {1});
        circuit.Add(vin, {2, 0});
        circuit.Add(rb, {2, 3});
        circuit.Add(re, {4, 0});
        circuit.Add(q1, {1, 3, 4});
        circuit.Add(ground, {0});
        check_tf_parity(circuit, {.OutputNode = 4, .InputSource = "V2"});
    }

    SECTION("Non-inverting uA741 op-amp amplifier") {
        Core::VCC vcc;
        vcc.SetName("V1");
        vcc.SetValue(15.0);
        Core::VoltageSource vee;
        vee.SetName("V2");
        vee.SetValue(-15.0);
        Core::VoltageSource vin;
        vin.SetName("V3");
        vin.SetValue(0.1);
        Core::Resistor r1;
        r1.SetName("R1");
        r1.SetValue(1e3);
        Core::Resistor r2;
        r2.SetName("R2");
        r2.SetValue(9e3);
        Core::OpAmp op_amp;
        op_amp.SetName("U1");
        op_amp.SetModel(*Core::FindOpAmpModel(Core::ComponentType::OP_AMP, "uA741"));
        Core::Ground ground;
        Core::Circuit circuit;
        circuit.Add(vcc, {1});
        circuit.Add(vee, {2, 0});
        circuit.Add(vin, {3, 0});
        circuit.Add(r1, {4, 0});
        circuit.Add(r2, {5, 4});
        circuit.Add(op_amp, {5, 3, 4, 1, 2});
        circuit.Add(ground, {0});
        check_tf_parity(circuit, {.OutputNode = 5, .InputSource = "V3"});
    }
}

TEST_CASE("Both backends agree on the small-signal noise spectral densities, totals and device contributions") {
    auto check_noise_parity = [](const Core::Circuit &circuit, const Core::NoiseSettings &settings) {
        Core::NativeBackend native;
        const Core::NoiseRun expected = Ngspice().RunNoise(circuit, settings);
        INFO("ngspice: " + (expected.Result ? std::string("ok") : expected.Result.error()));
        REQUIRE(expected.Result.has_value());
        const Core::NoiseRun actual = native.RunNoise(circuit, settings);
        INFO("native: " + (actual.Result ? std::string("ok") : actual.Result.error()));
        REQUIRE(actual.Result.has_value());

        REQUIRE(actual.Result->OutputNode == expected.Result->OutputNode);
        REQUIRE(actual.Result->InputSource == expected.Result->InputSource);
        REQUIRE(actual.Result->InputUnit == expected.Result->InputUnit);
        REQUIRE(actual.Result->Frequencies.size() == expected.Result->Frequencies.size());

        for (std::size_t i = 0; i < expected.Result->Frequencies.size(); ++i) {
            const double f = expected.Result->Frequencies[i];
            REQUIRE(Close(actual.Result->Frequencies[i], f, 1e-9, 1e-12));
            INFO(std::format("f = {:.4g} Hz onoise: ngspice {:.6e}, native {:.6e}", f,
                             expected.Result->OutputDensities[i], actual.Result->OutputDensities[i]));
            REQUIRE(Close(actual.Result->OutputDensities[i], expected.Result->OutputDensities[i], 1e-3, 1e-15));
            INFO(std::format("f = {:.4g} Hz inoise: ngspice {:.6e}, native {:.6e}", f,
                             expected.Result->InputDensities[i], actual.Result->InputDensities[i]));
            REQUIRE(Close(actual.Result->InputDensities[i], expected.Result->InputDensities[i], 1e-3, 1e-15));
        }

        INFO(std::format("OutputTotal: ngspice {:.6e}, native {:.6e}", expected.Result->OutputTotal,
                         actual.Result->OutputTotal));
        REQUIRE(Close(actual.Result->OutputTotal, expected.Result->OutputTotal, 1e-3, 1e-15));
        INFO(std::format("InputTotal: ngspice {:.6e}, native {:.6e}", expected.Result->InputTotal,
                         actual.Result->InputTotal));
        REQUIRE(Close(actual.Result->InputTotal, expected.Result->InputTotal, 1e-3, 1e-15));

        REQUIRE(actual.Result->Contributions.size() == expected.Result->Contributions.size());
        for (std::size_t c = 0; c < expected.Result->Contributions.size(); ++c) {
            const Core::NoiseContribution &exp_c = expected.Result->Contributions[c];
            const auto found =
                std::ranges::find(actual.Result->Contributions, exp_c.Name, &Core::NoiseContribution::Name);
            INFO(std::format("contribution {}", exp_c.Name));
            REQUIRE(found != actual.Result->Contributions.end());
            INFO(std::format("contribution {} OutputTotal: ngspice {:.6e}, native {:.6e}", exp_c.Name,
                             exp_c.OutputTotal, found->OutputTotal));
            REQUIRE(Close(found->OutputTotal, exp_c.OutputTotal, 1e-3, 1e-15));
            for (std::size_t i = 0; i < expected.Result->Frequencies.size(); ++i) {
                REQUIRE(Close(found->OutputDensities[i], exp_c.OutputDensities[i], 1e-3, 1e-15));
            }
        }
    };

    SECTION("RC low-pass filter") {
        Core::VoltageSource vin;
        vin.SetName("V1");
        vin.SetSourceType(Core::Source::SourceType::AC);
        vin.SetAC({.Amplitude = 1.0, .Frequency = 1e3, .Offset = 0.0, .Magnitude = 1.0});
        Core::Resistor r1;
        r1.SetName("R1");
        r1.SetValue(1e3);
        Core::Capacitor c1;
        c1.SetName("C1");
        c1.SetValue(100e-9);
        Core::Ground ground;
        Core::Circuit circuit;
        circuit.Add(vin, {1, 0});
        circuit.Add(r1, {1, 2});
        circuit.Add(c1, {2, 0});
        circuit.Add(ground, {0});
        check_noise_parity(circuit, {.OutputNode = 2,
                                     .InputSource = "V1",
                                     .StartFrequency = 10.0,
                                     .StopFrequency = 100e3,
                                     .PointsPerDecade = 10});
    }

    SECTION("Inverting ideal op-amp amplifier") {
        Core::VCC vcc;
        vcc.SetName("V1");
        vcc.SetValue(15.0);
        Core::VoltageSource vee;
        vee.SetName("V2");
        vee.SetValue(-15.0);
        Core::VoltageSource vin;
        vin.SetName("V3");
        vin.SetSourceType(Core::Source::SourceType::AC);
        vin.SetAC({.Amplitude = 1.0, .Frequency = 1e3, .Offset = 0.0, .Magnitude = 1.0});
        Core::Resistor r1;
        r1.SetName("R1");
        r1.SetValue(1e3);
        Core::Resistor r2;
        r2.SetName("R2");
        r2.SetValue(10e3);
        Core::OpAmp op_amp;
        op_amp.SetName("U1");
        Core::Ground ground;
        Core::Circuit circuit;
        circuit.Add(vcc, {4});
        circuit.Add(vee, {5, 0});
        circuit.Add(vin, {1, 0});
        circuit.Add(r1, {1, 2});
        circuit.Add(r2, {2, 3});
        circuit.Add(op_amp, {3, 0, 2, 4, 5});
        circuit.Add(ground, {0});
        check_noise_parity(circuit, {.OutputNode = 3,
                                     .InputSource = "V3",
                                     .StartFrequency = 10.0,
                                     .StopFrequency = 100e3,
                                     .PointsPerDecade = 5});
    }

    SECTION("Common-emitter BJT amplifier") {
        Core::VCC vcc;
        vcc.SetName("V1");
        vcc.SetValue(12.0);
        Core::VoltageSource vin;
        vin.SetName("V2");
        vin.SetSourceType(Core::Source::SourceType::AC);
        vin.SetAC({.Amplitude = 0.01, .Frequency = 1e3, .Offset = 2.0, .Magnitude = 1.0});
        Core::Resistor rb;
        rb.SetName("R1");
        rb.SetValue(10e3);
        Core::Resistor rc;
        rc.SetName("R2");
        rc.SetValue(2.2e3);
        Core::Resistor re;
        re.SetName("R3");
        re.SetValue(470.0);
        Core::BJT q1(Core::ComponentType::NPN);
        q1.SetName("Q1");
        Core::Ground ground;
        Core::Circuit circuit;
        circuit.Add(vcc, {1});
        circuit.Add(vin, {2, 0});
        circuit.Add(rb, {2, 3});
        circuit.Add(rc, {1, 4});
        circuit.Add(re, {5, 0});
        circuit.Add(q1, {4, 3, 5});
        circuit.Add(ground, {0});
        check_noise_parity(circuit, {.OutputNode = 4,
                                     .InputSource = "V2",
                                     .StartFrequency = 100.0,
                                     .StopFrequency = 1e6,
                                     .PointsPerDecade = 5});
    }

    SECTION("Common-source NMOS amplifier") {
        Core::VCC vdd;
        vdd.SetName("V1");
        vdd.SetValue(10.0);
        Core::VoltageSource vin;
        vin.SetName("V2");
        vin.SetSourceType(Core::Source::SourceType::AC);
        vin.SetAC({.Amplitude = 0.01, .Frequency = 1e3, .Offset = 3.0, .Magnitude = 1.0});
        Core::Resistor rd;
        rd.SetName("R1");
        rd.SetValue(2e3);
        Core::MOSFET m1(Core::ComponentType::NMOS);
        m1.SetName("M1");
        Core::Ground ground;
        Core::Circuit circuit;
        circuit.Add(vdd, {1});
        circuit.Add(vin, {2, 0});
        circuit.Add(rd, {1, 3});
        circuit.Add(m1, {3, 2, 0});
        circuit.Add(ground, {0});
        check_noise_parity(circuit, {.OutputNode = 3,
                                     .InputSource = "V2",
                                     .StartFrequency = 10.0,
                                     .StopFrequency = 100e3,
                                     .PointsPerDecade = 5});
    }

    SECTION("Non-inverting uA741 op-amp amplifier") {
        Core::VCC vcc;
        vcc.SetName("V1");
        vcc.SetValue(15.0);
        Core::VoltageSource vee;
        vee.SetName("V2");
        vee.SetValue(-15.0);
        Core::VoltageSource vin;
        vin.SetName("V3");
        vin.SetSourceType(Core::Source::SourceType::AC);
        vin.SetAC({.Amplitude = 0.1, .Frequency = 1e3, .Offset = 0.0, .Magnitude = 1.0});
        Core::Resistor r1;
        r1.SetName("R1");
        r1.SetValue(1e3);
        Core::Resistor r2;
        r2.SetName("R2");
        r2.SetValue(9e3);
        Core::OpAmp op_amp;
        op_amp.SetName("U1");
        op_amp.SetModel(*Core::FindOpAmpModel(Core::ComponentType::OP_AMP, "uA741"));
        Core::Ground ground;
        Core::Circuit circuit;
        circuit.Add(vcc, {1});
        circuit.Add(vee, {2, 0});
        circuit.Add(vin, {3, 0});
        circuit.Add(r1, {4, 0});
        circuit.Add(r2, {5, 4});
        circuit.Add(op_amp, {5, 3, 4, 1, 2});
        circuit.Add(ground, {0});
        check_noise_parity(circuit, {.OutputNode = 5,
                                     .InputSource = "V3",
                                     .StartFrequency = 10.0,
                                     .StopFrequency = 100e3,
                                     .PointsPerDecade = 5});
    }
}
