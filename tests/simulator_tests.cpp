/**
 * @file    simulator_tests.cpp
 * @brief   Tests for the operating point simulation, run through the real ngspice library.
 */

#include "circuit.h"
#include "components/bjt.h"
#include "components/capacitor.h"
#include "components/clock.h"
#include "components/controlled_source.h"
#include "components/current_source.h"
#include "components/diode.h"
#include "components/ground.h"
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
#include "digital_models.h"
#include "simulator.h"
#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <complex>
#include <format>
#include <numbers>
#include <vector>

namespace {

/**
 * @struct  Divider
 * @brief   A 10 V supply on node 1 feeding 1k and 3k resistors in series to ground, with the middle on node 2.
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

/**
 * @struct  LowPass
 * @brief   An AC source on node 1 driving a 1k resistor and a 1u capacitor to ground, with the output on node 2.
 */
struct LowPass {
    Core::VoltageSource Source;
    Core::Resistor Series;
    Core::Capacitor Shunt;
    Core::Ground Reference;
    Core::Circuit Circuit;

    LowPass() {
        Source.SetName("Vin1");
        Source.SetSourceType(Core::VoltageSource::SourceType::AC);
        Series.SetName("R1");
        Shunt.SetName("C1");
        Circuit.Add(Source, {1, 0});
        Circuit.Add(Series, {1, 2});
        Circuit.Add(Shunt, {2, 0});
        Circuit.Add(Reference, {0});
    }
};

/**
 * @struct  DiodeLoop
 * @brief   A supply on node 1 feeding a resistor to node 2, where a diode part connects to ground.
 * @details The diode is forward biased from node 2 to ground, except a Zener, which is reversed into breakdown.
 */
struct DiodeLoop {
    Core::VCC Supply;
    Core::Resistor Series;
    Core::Diode Part;
    Core::Ground Reference;
    Core::Circuit Circuit;

    DiodeLoop(const Core::ComponentType type, const double supply, const double resistance) : Part(type) {
        Supply.SetName("V1");
        Supply.SetValue(supply);
        Series.SetName("R1");
        Series.SetValue(resistance);
        Part.SetName("D1");
        Circuit.Add(Supply, {1});
        Circuit.Add(Series, {1, 2});
        if (type == Core::ComponentType::ZENER_DIODE) {
            Circuit.Add(Part, {0, 2});
        } else {
            Circuit.Add(Part, {2, 0});
        }
        Circuit.Add(Reference, {0});
    }
};

/**
 * @struct  CommonEmitter
 * @brief   A bipolar transistor with its emitter on ground, its base fed from the supply through 470k and its
 *          collector through 1k, with the supply on node 1, the base on node 2 and the collector on node 3.
 * @details For a PNP the supply is negative, which mirrors every voltage and current of the NPN version.
 */
struct CommonEmitter {
    Core::VCC Supply;
    Core::Resistor BaseResistor;
    Core::Resistor CollectorResistor;
    Core::BJT Transistor;
    Core::Ground Reference;
    Core::Circuit Circuit;

    explicit CommonEmitter(const Core::ComponentType type) : Transistor(type) {
        Supply.SetName("V1");
        Supply.SetValue(type == Core::ComponentType::PNP ? -12.0 : 12.0);
        BaseResistor.SetName("R1");
        BaseResistor.SetValue(470e3);
        CollectorResistor.SetName("R2");
        Transistor.SetName("Q1");
        Circuit.Add(Supply, {1});
        Circuit.Add(BaseResistor, {1, 2});
        Circuit.Add(CollectorResistor, {1, 3});
        Circuit.Add(Transistor, {3, 2, 0});
        Circuit.Add(Reference, {0});
    }
};

/**
 * @struct  LowSideSwitch
 * @brief   A MOSFET with its source on ground, its drain fed from a 5 V supply through 1k and its gate held by
 *          a second supply, with the drain on node 2 and the gate on node 3.
 * @details For a PMOS both supplies are negative, which mirrors the NMOS version.
 */
struct LowSideSwitch {
    Core::VCC Supply;
    Core::VCC Gate;
    Core::Resistor Load;
    Core::MOSFET Transistor;
    Core::Ground Reference;
    Core::Circuit Circuit;

    LowSideSwitch(const Core::ComponentType type, const double gate_voltage) : Transistor(type) {
        const double sign = type == Core::ComponentType::PMOS ? -1.0 : 1.0;
        Supply.SetName("V1");
        Supply.SetValue(sign * 5.0);
        Gate.SetName("V2");
        Gate.SetValue(sign * gate_voltage);
        Load.SetName("R1");
        Transistor.SetName("M1");
        Circuit.Add(Supply, {1});
        Circuit.Add(Gate, {3});
        Circuit.Add(Load, {1, 2});
        Circuit.Add(Transistor, {2, 3, 0});
        Circuit.Add(Reference, {0});
    }
};

// Rebuilds a complex current from the decibels and degrees an AC sweep reports
std::complex<double> FromDecibels(const double decibels, const double degrees) {
    return std::polar(std::pow(10.0, decibels / 20.0), degrees * std::numbers::pi / 180.0);
}

// Finds a current by component name, failing the test when it is missing
template <typename Current> const Current &FindCurrent(const std::vector<Current> &currents, const std::string &name) {
    const auto found = std::ranges::find_if(currents, [&name](const Current &current) { return current.Name == name; });
    REQUIRE(found != currents.end());
    return *found;
}

/**
 * @struct  ControlledStage
 * @brief   A 1 V source on node 1 across a 1k resistor to ground, so 1 mA flows through R1, and a controlled source
 *          driving a 1k load on node 2.
 * @details A voltage-controlled source senses node 1; a current-controlled one follows I(R1).
 */
struct ControlledStage {
    Core::VoltageSource Input;
    Core::Resistor Sense;
    Core::ControlledSource Source;
    Core::Resistor Load;
    Core::Ground Reference;
    Core::Circuit Circuit;

    explicit ControlledStage(const Core::ComponentType type) : Source(type) {
        Input.SetName("Vin1");
        Input.SetValue(1.0);
        Sense.SetName("R1");
        Source.SetName(std::string(Source.GetNamePrefix()) + "1");
        Load.SetName("R2");
        Circuit.Add(Input, {1, 0});
        Circuit.Add(Sense, {1, 0});
        if (Core::IsCurrentControlled(type)) {
            Source.SetControllingCurrent("R1");
            Circuit.Add(Source, {2, 0});
        } else {
            Circuit.Add(Source, {2, 0, 1, 0});
        }
        Circuit.Add(Load, {2, 0});
        Circuit.Add(Reference, {0});
    }
};

/**
 * @struct  InvertingAmplifier
 * @brief   An op-amp with 1k in and 10k of feedback, so a gain of -10, powered from +15 V and -15 V.
 * @details Nodes: input 1, inverting input 2, output 3, V+ 4 and V- 5; the non-inverting input is grounded.
 */
struct InvertingAmplifier {
    Core::VoltageSource Input;
    Core::Resistor In;
    Core::Resistor Feedback;
    Core::OpAmp Amplifier;
    Core::VCC Positive;
    Core::VCC Negative;
    Core::Ground Reference;
    Core::Circuit Circuit;

    explicit InvertingAmplifier(const double input) {
        Input.SetName("Vin1");
        Input.SetValue(input);
        In.SetName("R1");
        Feedback.SetName("R2");
        Feedback.SetValue(10e3);
        Amplifier.SetName("U1");
        Positive.SetName("V1");
        Positive.SetValue(15.0);
        Negative.SetName("V2");
        Negative.SetValue(-15.0);
        Circuit.Add(Input, {1, 0});
        Circuit.Add(In, {1, 2});
        Circuit.Add(Feedback, {2, 3});
        Circuit.Add(Amplifier, {3, 0, 2, 4, 5});
        Circuit.Add(Positive, {4});
        Circuit.Add(Negative, {5});
        Circuit.Add(Reference, {0});
    }
};

} // namespace

TEST_CASE("The operating point of a resistive divider matches the hand calculation", "[simulator]") {
    const Divider divider;
    const Core::OperatingPointRun run = Core::RunOperatingPoint(divider.Circuit);
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    const std::vector<double> &voltages = run.Result->NodeVoltages;
    REQUIRE(voltages.size() == 3);
    CHECK(voltages[0] == 0.0);
    CHECK_THAT(voltages[1], Catch::Matchers::WithinRel(10.0, 1e-9));
    CHECK_THAT(voltages[2], Catch::Matchers::WithinRel(7.5, 1e-9));
    CHECK_FALSE(run.Messages.empty());
}

TEST_CASE("Runs are independent of each other", "[simulator]") {
    Divider divider;
    REQUIRE(Core::RunOperatingPoint(divider.Circuit).Result);

    divider.Supply.SetValue(4.0);
    const Core::OperatingPointRun run = Core::RunOperatingPoint(divider.Circuit);
    REQUIRE(run.Result);
    CHECK_THAT(run.Result->NodeVoltages[2], Catch::Matchers::WithinRel(3.0, 1e-9));
}

TEST_CASE("Circuits without ground or components are rejected before reaching ngspice", "[simulator]") {
    SECTION("empty") {
        const Core::Circuit circuit;
        const Core::OperatingPointRun run = Core::RunOperatingPoint(circuit);
        CHECK_FALSE(run.Result);
        CHECK(run.Messages.empty());
    }
    SECTION("no ground") {
        Core::Resistor resistor;
        resistor.SetName("R1");
        Core::Circuit circuit;
        circuit.Add(resistor, {1, 2});
        const Core::OperatingPointRun run = Core::RunOperatingPoint(circuit);
        REQUIRE_FALSE(run.Result);
        CHECK(run.Result.error().find("ground") != std::string::npos);
        CHECK(run.Messages.empty());
    }
}

TEST_CASE("A node with no DC path to ground still runs but reports warnings", "[simulator]") {
    Core::VCC supply;
    supply.SetName("V1");
    Core::Capacitor capacitor;
    capacitor.SetName("C1");
    Core::Resistor resistor;
    resistor.SetName("R1");
    const Core::Ground reference;
    Core::Circuit circuit;
    circuit.Add(supply, {1});
    circuit.Add(capacitor, {1, 2});
    circuit.Add(resistor, {2, 3});
    circuit.Add(reference, {0});

    const Core::OperatingPointRun run = Core::RunOperatingPoint(circuit);
    CHECK(run.Result);
    CHECK(std::ranges::any_of(run.Messages, [](const Core::SimulatorMessage &message) {
        return message.FromErrorStream && message.Text.find("singular matrix") != std::string::npos;
    }));
}

TEST_CASE("A transient run follows the sine of an AC source", "[simulator]") {
    Core::VoltageSource source;
    source.SetName("Vin1");
    source.SetSourceType(Core::VoltageSource::SourceType::AC);
    source.SetAC({.Amplitude = 2.0, .Frequency = 1e3, .Offset = 1.0});
    Core::Resistor load;
    load.SetName("R1");
    const Core::Ground reference;
    Core::Circuit circuit;
    circuit.Add(source, {1, 0});
    circuit.Add(load, {1, 0});
    circuit.Add(reference, {0});

    const Core::TransientRun run = Core::RunTransient(circuit, {.StopTime = 2e-3, .TimeStep = 10e-6});
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    const Core::Transient &transient = *run.Result;
    REQUIRE(transient.NodeVoltages.size() == 2);
    REQUIRE(transient.NodeVoltages[1].size() == transient.Times.size());
    CHECK_THAT(transient.Times.back(), Catch::Matchers::WithinRel(2e-3, 1e-9));
    for (std::size_t index = 0; index < transient.Times.size(); ++index) {
        const double expected = 1.0 + 2.0 * std::sin(2.0 * std::numbers::pi * 1e3 * transient.Times[index]);
        CHECK_THAT(transient.NodeVoltages[1][index], Catch::Matchers::WithinAbs(expected, 1e-3));
    }
}

TEST_CASE("An AC sweep of an RC low-pass matches its transfer function", "[simulator]") {
    const LowPass low_pass;
    const Core::ACSweepRun run =
        Core::RunACSweep(low_pass.Circuit, {.StartFrequency = 1.0, .StopFrequency = 1e6, .PointsPerDecade = 10});
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    const Core::ACSweep &sweep = *run.Result;
    REQUIRE(sweep.Frequencies.size() == 61);
    CHECK_THAT(sweep.Frequencies.front(), Catch::Matchers::WithinRel(1.0, 1e-9));
    CHECK_THAT(sweep.Frequencies.back(), Catch::Matchers::WithinRel(1e6, 1e-9));
    for (std::size_t index = 0; index < sweep.Frequencies.size(); ++index) {
        const double ratio = 2.0 * std::numbers::pi * sweep.Frequencies[index] * 1e3 * 1e-6;
        CHECK_THAT(sweep.NodeMagnitudesDecibels[1][index], Catch::Matchers::WithinAbs(0.0, 1e-6));
        CHECK_THAT(sweep.NodeMagnitudesDecibels[2][index],
                   Catch::Matchers::WithinAbs(-10.0 * std::log10(1.0 + ratio * ratio), 1e-6));
        CHECK_THAT(sweep.NodePhasesDegrees[2][index],
                   Catch::Matchers::WithinAbs(-std::atan(ratio) * 180.0 / std::numbers::pi, 1e-6));
    }
}

TEST_CASE("An AC sweep without an AC source is rejected before reaching ngspice", "[simulator]") {
    LowPass low_pass;
    for (const auto type : {Core::VoltageSource::SourceType::DC, Core::VoltageSource::SourceType::PULSE}) {
        low_pass.Source.SetSourceType(type);
        const Core::ACSweepRun run = Core::RunACSweep(low_pass.Circuit, {});
        REQUIRE_FALSE(run.Result);
        CHECK(run.Result.error().find("AC source") != std::string::npos);
        CHECK(run.Messages.empty());
    }
}

TEST_CASE("A pulse charges an RC low-pass with its time constant", "[simulator]") {
    LowPass low_pass;
    low_pass.Source.SetSourceType(Core::VoltageSource::SourceType::PULSE);
    // A single 1 V step at t = 0, far longer than the 1 ms time constant
    low_pass.Source.SetPulse(
        {.Low = 0.0, .High = 1.0, .RiseTime = 1e-9, .FallTime = 1e-9, .Width = 1.0, .Period = 2.0});

    const Core::TransientRun run = Core::RunTransient(low_pass.Circuit, {.StopTime = 5e-3, .TimeStep = 10e-6});
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    const Core::Transient &transient = *run.Result;
    CHECK(transient.NodeVoltages[2].front() == 0.0);
    for (std::size_t index = 0; index < transient.Times.size(); ++index) {
        const double expected = 1.0 - std::exp(-transient.Times[index] / 1e-3);
        CHECK_THAT(transient.NodeVoltages[2][index], Catch::Matchers::WithinAbs(expected, 5e-3));
    }
}

TEST_CASE("A transient run follows the exponential of an EXP source", "[simulator]") {
    Core::VoltageSource source;
    source.SetName("Vin1");
    source.SetSourceType(Core::VoltageSource::SourceType::EXP);
    // Rise from 0 with a 0.5 ms time constant at 1 ms, then decay from 5 V at 5 ms with a 1 ms time constant
    const Core::EXPParameters exp{.InitialValue = 0.0,
                                  .PeakValue = 5.0,
                                  .RiseDelay = 1e-3,
                                  .RiseTimeConstant = 0.5e-3,
                                  .FallDelay = 5e-3,
                                  .FallTimeConstant = 1e-3};
    source.SetEXP(exp);
    Core::Resistor load;
    load.SetName("R1");
    const Core::Ground reference;
    Core::Circuit circuit;
    circuit.Add(source, {1, 0});
    circuit.Add(load, {1, 0});
    circuit.Add(reference, {0});

    const Core::TransientRun run = Core::RunTransient(circuit, {.StopTime = 10e-3, .TimeStep = 10e-6});
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    const Core::Transient &transient = *run.Result;
    REQUIRE(transient.NodeVoltages[1].size() == transient.Times.size());
    for (std::size_t index = 0; index < transient.Times.size(); ++index) {
        const double t = transient.Times[index];
        double expected = exp.InitialValue;
        if (t > exp.RiseDelay) {
            expected +=
                (exp.PeakValue - exp.InitialValue) * (1.0 - std::exp(-(t - exp.RiseDelay) / exp.RiseTimeConstant));
        }
        if (t > exp.FallDelay) {
            expected +=
                (exp.InitialValue - exp.PeakValue) * (1.0 - std::exp(-(t - exp.FallDelay) / exp.FallTimeConstant));
        }
        CHECK_THAT(transient.NodeVoltages[1][index], Catch::Matchers::WithinAbs(expected, 5e-3));
    }
}

TEST_CASE("A transient run follows the single-frequency FM of an SFFM source", "[simulator]") {
    Core::VoltageSource source;
    source.SetName("Vin1");
    source.SetSourceType(Core::VoltageSource::SourceType::SFFM);
    const Core::SFFMParameters sffm{
        .Offset = 0.0, .Amplitude = 1.0, .CarrierFrequency = 1e3, .ModulationIndex = 5.0, .SignalFrequency = 100.0};
    source.SetSFFM(sffm);
    Core::Resistor load;
    load.SetName("R1");
    const Core::Ground reference;
    Core::Circuit circuit;
    circuit.Add(source, {1, 0});
    circuit.Add(load, {1, 0});
    circuit.Add(reference, {0});

    const Core::TransientRun run = Core::RunTransient(circuit, {.StopTime = 5e-3, .TimeStep = 5e-6});
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    const Core::Transient &transient = *run.Result;
    REQUIRE(transient.NodeVoltages[1].size() == transient.Times.size());
    for (std::size_t index = 0; index < transient.Times.size(); ++index) {
        const double t = transient.Times[index];
        // The carrier phase is swung by the modulation index times the signal sine
        const double expected =
            sffm.Offset + sffm.Amplitude * std::sin(2.0 * std::numbers::pi * sffm.CarrierFrequency * t +
                                                    sffm.ModulationIndex *
                                                        std::sin(2.0 * std::numbers::pi * sffm.SignalFrequency * t));
        CHECK_THAT(transient.NodeVoltages[1][index], Catch::Matchers::WithinAbs(expected, 1e-3));
    }
}

TEST_CASE("A transient run follows the amplitude modulation of an AM source", "[simulator]") {
    Core::VoltageSource source;
    source.SetName("Vin1");
    source.SetSourceType(Core::VoltageSource::SourceType::AM);
    // No offset, so the source at t = 0 matches the operating point, and no delay, so the sines start at once
    const Core::AMParameters am{
        .Amplitude = 1.0, .Offset = 0.0, .ModulatingFrequency = 100.0, .CarrierFrequency = 1e3, .Delay = 0.0};
    source.SetAM(am);
    Core::Resistor load;
    load.SetName("R1");
    const Core::Ground reference;
    Core::Circuit circuit;
    circuit.Add(source, {1, 0});
    circuit.Add(load, {1, 0});
    circuit.Add(reference, {0});

    const Core::TransientRun run = Core::RunTransient(circuit, {.StopTime = 5e-3, .TimeStep = 5e-6});
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    const Core::Transient &transient = *run.Result;
    REQUIRE(transient.NodeVoltages[1].size() == transient.Times.size());
    for (std::size_t index = 0; index < transient.Times.size(); ++index) {
        const double shifted = transient.Times[index] - am.Delay;
        const double expected =
            shifted <= 0.0
                ? 0.0
                : am.Offset + am.Amplitude * std::sin(2.0 * std::numbers::pi * am.ModulatingFrequency * shifted) *
                                  std::sin(2.0 * std::numbers::pi * am.CarrierFrequency * shifted);
        CHECK_THAT(transient.NodeVoltages[1][index], Catch::Matchers::WithinAbs(expected, 1e-3));
    }
}

TEST_CASE("A transient run follows the piecewise linear shape of a PWL source", "[simulator]") {
    Core::VoltageSource source;
    source.SetName("Vin1");
    source.SetSourceType(Core::VoltageSource::SourceType::PWL);
    const std::vector<Core::PWLPoint> points = {{0.0, 0.0}, {1e-3, 5.0}, {2e-3, 0.0}};
    source.SetPWL({.Points = points});
    Core::Resistor load;
    load.SetName("R1");
    const Core::Ground reference;
    Core::Circuit circuit;
    circuit.Add(source, {1, 0});
    circuit.Add(load, {1, 0});
    circuit.Add(reference, {0});

    const Core::TransientRun run = Core::RunTransient(circuit, {.StopTime = 3e-3, .TimeStep = 5e-6});
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    const Core::Transient &transient = *run.Result;
    REQUIRE(transient.NodeVoltages[1].size() == transient.Times.size());
    for (std::size_t index = 0; index < transient.Times.size(); ++index) {
        const double t = transient.Times[index];
        double expected = points.front().Value;
        for (std::size_t point = 1; point < points.size(); ++point) {
            if (t <= points[point].Time) {
                const double fraction = (t - points[point - 1].Time) / (points[point].Time - points[point - 1].Time);
                expected = points[point - 1].Value + fraction * (points[point].Value - points[point - 1].Value);
                break;
            }
            expected = points[point].Value;
        }
        CHECK_THAT(transient.NodeVoltages[1][index], Catch::Matchers::WithinAbs(expected, 1e-4));
    }
}

TEST_CASE("A capacitor with an initial condition discharges through a resistor", "[simulator]") {
    Core::Capacitor capacitor;
    capacitor.SetName("C1");
    capacitor.SetValue(1e-6);
    capacitor.SetInitialCondition(5.0);
    Core::Resistor resistor;
    resistor.SetName("R1");
    resistor.SetValue(1e3);
    const Core::Ground reference;
    Core::Circuit circuit;
    circuit.Add(capacitor, {1, 0});
    circuit.Add(resistor, {1, 0});
    circuit.Add(reference, {0});

    const Core::TransientRun run = Core::RunTransient(circuit, {.StopTime = 5e-3, .TimeStep = 5e-6});
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    const Core::Transient &transient = *run.Result;
    // uic skips the operating point, so the run says it started from the initial condition
    CHECK(std::ranges::any_of(transient.Warnings, [](const std::string &warning) {
        return warning.find("initial condition") != std::string::npos;
    }));
    for (std::size_t index = 0; index < transient.Times.size(); ++index) {
        const double expected = 5.0 * std::exp(-transient.Times[index] / 1e-3);
        CHECK_THAT(transient.NodeVoltages[1][index], Catch::Matchers::WithinAbs(expected, 5e-3));
    }
}

TEST_CASE("With uic, a capacitor without an initial condition starts uncharged, not at its operating point",
          "[simulator]") {
    // R1 and C1 form a divider that the operating point would settle at 10 V; C2 only exists to give the run an
    // initial condition, so uic applies to the whole circuit
    Core::VoltageSource source;
    source.SetName("V1");
    source.SetValue(10.0);
    Core::Resistor charging;
    charging.SetName("R1");
    charging.SetValue(1e3);
    Core::Capacitor plain;
    plain.SetName("C1");
    plain.SetValue(1e-6);
    Core::Capacitor charged;
    charged.SetName("C2");
    charged.SetValue(1e-6);
    charged.SetInitialCondition(5.0);
    Core::Resistor discharging;
    discharging.SetName("R2");
    discharging.SetValue(1e3);
    const Core::Ground reference;
    Core::Circuit circuit;
    circuit.Add(source, {1, 0});
    circuit.Add(charging, {1, 2});
    circuit.Add(plain, {2, 0});
    circuit.Add(charged, {3, 0});
    circuit.Add(discharging, {3, 0});
    circuit.Add(reference, {0});

    const Core::TransientRun run = Core::RunTransient(circuit, {.StopTime = 5e-3, .TimeStep = 5e-6});
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    const Core::Transient &transient = *run.Result;
    // C1 charges from 0 V toward 10 V with R1*C1 = 1 ms, instead of sitting at 10 V from the start
    for (std::size_t index = 0; index < transient.Times.size(); ++index) {
        const double expected = 10.0 * (1.0 - std::exp(-transient.Times[index] / 1e-3));
        CHECK_THAT(transient.NodeVoltages[2][index], Catch::Matchers::WithinAbs(expected, 5e-2));
    }
}

TEST_CASE("A transient with no initial condition keeps the operating point", "[simulator]") {
    Core::Capacitor capacitor;
    capacitor.SetName("C1");
    Core::Resistor resistor;
    resistor.SetName("R1");
    const Core::Ground reference;
    Core::Circuit circuit;
    circuit.Add(capacitor, {1, 0});
    circuit.Add(resistor, {1, 0});
    circuit.Add(reference, {0});

    const Core::TransientRun run = Core::RunTransient(circuit, {.StopTime = 1e-3, .TimeStep = 5e-6});
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    CHECK(run.Result->Warnings.empty());
}

TEST_CASE("Invalid analysis settings are rejected before reaching ngspice", "[simulator]") {
    const LowPass low_pass;
    SECTION("transient") {
        for (const Core::TransientSettings settings : {Core::TransientSettings{.StopTime = 0.0, .TimeStep = 1e-6},
                                                       Core::TransientSettings{.StopTime = 1e-3, .TimeStep = 0.0},
                                                       Core::TransientSettings{.StopTime = 1e-3, .TimeStep = 1e-2},
                                                       Core::TransientSettings{.StopTime = 1.0, .TimeStep = 1e-9}}) {
            const Core::TransientRun run = Core::RunTransient(low_pass.Circuit, settings);
            CHECK_FALSE(run.Result);
            CHECK(run.Messages.empty());
        }
    }
    SECTION("AC sweep") {
        for (const Core::ACSweepSettings settings :
             {Core::ACSweepSettings{.StartFrequency = 0.0, .StopFrequency = 1e3, .PointsPerDecade = 10},
              Core::ACSweepSettings{.StartFrequency = 1e3, .StopFrequency = 1e3, .PointsPerDecade = 10},
              Core::ACSweepSettings{.StartFrequency = 1.0, .StopFrequency = 1e3, .PointsPerDecade = 0}}) {
            const Core::ACSweepRun run = Core::RunACSweep(low_pass.Circuit, settings);
            CHECK_FALSE(run.Result);
            CHECK(run.Messages.empty());
        }
    }
}

TEST_CASE("The operating point has the current through every component, signed as in SPICE", "[simulator]") {
    const Divider divider;
    const Core::OperatingPointRun run = Core::RunOperatingPoint(divider.Circuit);
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    const std::vector<Core::ComponentCurrent> &currents = run.Result->Currents;
    // Ground carries no current
    CHECK(currents.size() == 3);
    CHECK_THAT(FindCurrent(currents, "R1").Current, Catch::Matchers::WithinRel(2.5e-3, 1e-9));
    CHECK_THAT(FindCurrent(currents, "R2").Current, Catch::Matchers::WithinRel(2.5e-3, 1e-9));
    // The supply delivers the current, so it flows through it from ground to its terminal
    CHECK_THAT(FindCurrent(currents, "V1").Current, Catch::Matchers::WithinRel(-2.5e-3, 1e-9));
}

TEST_CASE("A current source pushes its current into the node at its second terminal", "[simulator]") {
    Core::CurrentSource source;
    source.SetName("I1");
    source.SetValue(2e-3);
    Core::Resistor load;
    load.SetName("R1");
    const Core::Ground reference;
    Core::Circuit circuit;
    circuit.Add(source, {0, 1});
    circuit.Add(load, {1, 0});
    circuit.Add(reference, {0});

    const Core::OperatingPointRun run = Core::RunOperatingPoint(circuit);
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    CHECK_THAT(run.Result->NodeVoltages[1], Catch::Matchers::WithinRel(2.0, 1e-9));
    CHECK_THAT(FindCurrent(run.Result->Currents, "I1").Current, Catch::Matchers::WithinRel(2e-3, 1e-9));
    CHECK_THAT(FindCurrent(run.Result->Currents, "R1").Current, Catch::Matchers::WithinRel(2e-3, 1e-9));
}

TEST_CASE("A transient has the charging current of an RC low-pass", "[simulator]") {
    LowPass low_pass;
    low_pass.Source.SetSourceType(Core::VoltageSource::SourceType::PULSE);
    low_pass.Source.SetPulse(
        {.Low = 0.0, .High = 1.0, .RiseTime = 1e-9, .FallTime = 1e-9, .Width = 1.0, .Period = 2.0});

    const Core::TransientRun run = Core::RunTransient(low_pass.Circuit, {.StopTime = 5e-3, .TimeStep = 10e-6});
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    const Core::Transient &transient = *run.Result;
    const std::vector<double> &capacitor = FindCurrent(transient.Currents, "C1").Values;
    const std::vector<double> &source = FindCurrent(transient.Currents, "Vin1").Values;
    REQUIRE(capacitor.size() == transient.Times.size());
    for (std::size_t index = 0; index < transient.Times.size(); ++index) {
        // The first points sit on the step itself, where the current jumps from 0 to 1 mA
        if (transient.Times[index] < 20e-6) {
            continue;
        }
        const double expected = 1e-3 * std::exp(-transient.Times[index] / 1e-3);
        CHECK_THAT(capacitor[index], Catch::Matchers::WithinAbs(expected, 1e-5));
        CHECK_THAT(source[index], Catch::Matchers::WithinAbs(-expected, 1e-5));
    }
}

TEST_CASE("An AC sweep has the current through every component", "[simulator]") {
    const LowPass low_pass;
    const Core::ACSweepRun run =
        Core::RunACSweep(low_pass.Circuit, {.StartFrequency = 1.0, .StopFrequency = 1e6, .PointsPerDecade = 10});
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    const Core::ACSweep &sweep = *run.Result;
    const std::vector<double> &resistor_magnitudes = FindCurrent(sweep.CurrentMagnitudesDecibels, "R1").Values;
    const std::vector<double> &resistor_phases = FindCurrent(sweep.CurrentPhasesDegrees, "R1").Values;
    const std::vector<double> &capacitor_magnitudes = FindCurrent(sweep.CurrentMagnitudesDecibels, "C1").Values;
    const std::vector<double> &source_magnitudes = FindCurrent(sweep.CurrentMagnitudesDecibels, "Vin1").Values;
    const std::vector<double> &source_phases = FindCurrent(sweep.CurrentPhasesDegrees, "Vin1").Values;
    for (std::size_t index = 0; index < sweep.Frequencies.size(); ++index) {
        const double omega = 2.0 * std::numbers::pi * sweep.Frequencies[index];
        const std::complex<double> current = 1.0 / std::complex<double>(1e3, -1.0 / (omega * 1e-6));
        const double magnitude = 20.0 * std::log10(std::abs(current));
        const double phase = std::arg(current) * 180.0 / std::numbers::pi;
        CHECK_THAT(resistor_magnitudes[index], Catch::Matchers::WithinAbs(magnitude, 1e-6));
        CHECK_THAT(resistor_phases[index], Catch::Matchers::WithinAbs(phase, 1e-6));
        CHECK_THAT(capacitor_magnitudes[index], Catch::Matchers::WithinAbs(magnitude, 1e-6));
        // The source carries the same current the other way, half a turn apart
        CHECK_THAT(source_magnitudes[index], Catch::Matchers::WithinAbs(magnitude, 1e-6));
        CHECK_THAT(std::abs(std::remainder(source_phases[index] - phase, 360.0)),
                   Catch::Matchers::WithinAbs(180.0, 1e-6));
    }
}

TEST_CASE("An AC current source excites an AC sweep", "[simulator]") {
    Core::CurrentSource source;
    source.SetName("I1");
    source.SetSourceType(Core::CurrentSource::SourceType::AC);
    Core::Resistor load;
    load.SetName("R1");
    const Core::Ground reference;
    Core::Circuit circuit;
    circuit.Add(source, {0, 1});
    circuit.Add(load, {1, 0});
    circuit.Add(reference, {0});

    const Core::ACSweepRun run = Core::RunACSweep(circuit, {.StartFrequency = 1.0, .StopFrequency = 1e3});
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    // The default magnitude of 1 A into 1k is 1000 V, or 60 dB: the transimpedance of the load in V/A
    for (std::size_t index = 0; index < run.Result->Frequencies.size(); ++index) {
        CHECK_THAT(run.Result->NodeMagnitudesDecibels[1][index], Catch::Matchers::WithinAbs(60.0, 1e-6));
        CHECK_THAT(FindCurrent(run.Result->CurrentMagnitudesDecibels, "I1").Values[index],
                   Catch::Matchers::WithinAbs(0.0, 1e-6));
        CHECK_THAT(FindCurrent(run.Result->CurrentMagnitudesDecibels, "R1").Values[index],
                   Catch::Matchers::WithinAbs(0.0, 1e-6));
    }
}

TEST_CASE("A forward biased diode drops its forward voltage and carries the loop current", "[simulator]") {
    const DiodeLoop loop(Core::ComponentType::DIODE, 5.0, 1e3);
    const Core::OperatingPointRun run = Core::RunOperatingPoint(loop.Circuit);
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    const double forward_voltage = run.Result->NodeVoltages[2];
    CHECK(forward_voltage > 0.6);
    CHECK(forward_voltage < 0.8);
    // The diode current flows from anode to cathode, the same as through the resistor
    const double resistor_current = FindCurrent(run.Result->Currents, "R1").Current;
    CHECK_THAT(resistor_current, Catch::Matchers::WithinRel((5.0 - forward_voltage) / 1e3, 1e-6));
    CHECK_THAT(FindCurrent(run.Result->Currents, "D1").Current, Catch::Matchers::WithinRel(resistor_current, 1e-6));
}

TEST_CASE("A Zener diode in breakdown holds its voltage", "[simulator]") {
    const DiodeLoop loop(Core::ComponentType::ZENER_DIODE, 12.0, 1e3);
    const Core::OperatingPointRun run = Core::RunOperatingPoint(loop.Circuit);
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    CHECK_THAT(run.Result->NodeVoltages[2], Catch::Matchers::WithinAbs(5.1, 0.05));
    // The breakdown current flows from cathode to anode, against the forward direction
    CHECK(FindCurrent(run.Result->Currents, "D1").Current < 0.0);
    // Breakdown is how a Zener works, so it is not a problem
    CHECK(run.Result->Warnings.empty());
}

TEST_CASE("Each LED model drops its rated forward voltage at 20 mA", "[simulator]") {
    struct Rating {
        const char *Model;
        double ForwardVoltage;
    };
    const Rating rating = GENERATE(Rating{"Red", 1.8}, Rating{"Green", 2.1}, Rating{"Blue", 3.0});
    CAPTURE(rating.Model);
    // The resistor sets 20 mA when the LED drops its rated voltage
    DiodeLoop loop(Core::ComponentType::LED, 5.0, (5.0 - rating.ForwardVoltage) / 20e-3);
    const Core::DiodeModel *model = Core::FindDiodeModel(Core::ComponentType::LED, rating.Model);
    REQUIRE(model != nullptr);
    loop.Part.SetModel(*model);

    const Core::OperatingPointRun run = Core::RunOperatingPoint(loop.Circuit);
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    CHECK_THAT(run.Result->NodeVoltages[2], Catch::Matchers::WithinAbs(rating.ForwardVoltage, 0.02));
}

TEST_CASE("A half-wave rectifier passes only the positive half of a sine", "[simulator]") {
    Core::VoltageSource source;
    source.SetName("Vin1");
    source.SetSourceType(Core::VoltageSource::SourceType::AC);
    source.SetAC({.Amplitude = 5.0, .Frequency = 1e3, .Offset = 0.0});
    Core::Diode rectifier(Core::ComponentType::DIODE);
    rectifier.SetName("D1");
    rectifier.SetModel(*Core::FindDiodeModel(Core::ComponentType::DIODE, "1N4007"));
    Core::Resistor load;
    load.SetName("R1");
    const Core::Ground reference;
    Core::Circuit circuit;
    circuit.Add(source, {1, 0});
    circuit.Add(rectifier, {1, 2});
    circuit.Add(load, {2, 0});
    circuit.Add(reference, {0});

    const Core::TransientRun run = Core::RunTransient(circuit, {.StopTime = 2e-3, .TimeStep = 1e-6});
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    const std::vector<double> &output = run.Result->NodeVoltages[2];
    // Only leakage reaches the load in the negative half; the positive peak loses one forward drop
    CHECK(std::ranges::min(output) > -1e-3);
    CHECK(std::ranges::max(output) > 4.0);
    CHECK(std::ranges::max(output) < 4.5);
    CHECK(run.Result->Warnings.empty());
    const std::vector<double> &diode_current = FindCurrent(run.Result->Currents, "D1").Values;
    const std::vector<double> &load_current = FindCurrent(run.Result->Currents, "R1").Values;
    for (std::size_t index = 0; index < diode_current.size(); ++index) {
        CHECK_THAT(diode_current[index], Catch::Matchers::WithinAbs(load_current[index], 1e-9));
    }
}

TEST_CASE("An AC sweep has the small-signal current of a biased diode", "[simulator]") {
    Core::VoltageSource source;
    source.SetName("Vin1");
    source.SetSourceType(Core::VoltageSource::SourceType::AC);
    // The offset biases the diode, so it conducts around its operating point
    source.SetAC({.Amplitude = 1.0, .Frequency = 1e3, .Offset = 5.0});
    Core::Resistor series;
    series.SetName("R1");
    Core::Diode diode(Core::ComponentType::DIODE);
    diode.SetName("D1");
    const Core::Ground reference;
    Core::Circuit circuit;
    circuit.Add(source, {1, 0});
    circuit.Add(series, {1, 2});
    circuit.Add(diode, {2, 0});
    circuit.Add(reference, {0});

    const Core::ACSweepRun run = Core::RunACSweep(circuit, {.StartFrequency = 1e3, .StopFrequency = 1e6});
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    const std::vector<double> &diode_magnitudes = FindCurrent(run.Result->CurrentMagnitudesDecibels, "D1").Values;
    const std::vector<double> &diode_phases = FindCurrent(run.Result->CurrentPhasesDegrees, "D1").Values;
    const std::vector<double> &resistor_magnitudes = FindCurrent(run.Result->CurrentMagnitudesDecibels, "R1").Values;
    const std::vector<double> &resistor_phases = FindCurrent(run.Result->CurrentPhasesDegrees, "R1").Values;
    for (std::size_t index = 0; index < run.Result->Frequencies.size(); ++index) {
        // Nearly all of the 1 mA per volt goes through the conducting diode, which is in series with the resistor
        CHECK(diode_magnitudes[index] > -61.0);
        CHECK_THAT(diode_magnitudes[index], Catch::Matchers::WithinAbs(resistor_magnitudes[index], 1e-6));
        CHECK_THAT(diode_phases[index], Catch::Matchers::WithinAbs(resistor_phases[index], 1e-6));
    }
}

TEST_CASE("A reversed LED past its rating is reported in reverse breakdown", "[simulator]") {
    Core::VCC supply;
    supply.SetName("V1");
    supply.SetValue(12.0);
    Core::Resistor series;
    series.SetName("R1");
    Core::Diode led(Core::ComponentType::LED);
    led.SetName("D1");
    const Core::Ground reference;
    Core::Circuit circuit;
    circuit.Add(supply, {1});
    circuit.Add(series, {1, 2});
    circuit.Add(led, {0, 2});
    circuit.Add(reference, {0});

    const Core::OperatingPointRun run = Core::RunOperatingPoint(circuit);
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    REQUIRE(run.Result->Warnings.size() == 1);
    CAPTURE(run.Result->Warnings[0]);
    CHECK(run.Result->Warnings[0].starts_with("D1 (Red) goes into reverse breakdown"));
    CHECK(run.Result->Warnings[0].ends_with("rated for 5V"));
}

TEST_CASE("A transient reports a diode that breaks down at any time point", "[simulator]") {
    Core::VoltageSource source;
    source.SetName("Vin1");
    source.SetSourceType(Core::VoltageSource::SourceType::AC);
    source.SetAC({.Amplitude = 150.0, .Frequency = 1e3, .Offset = 0.0});
    Core::Diode rectifier(Core::ComponentType::DIODE);
    rectifier.SetName("D1");
    Core::Resistor load;
    load.SetName("R1");
    const Core::Ground reference;
    Core::Circuit circuit;
    circuit.Add(source, {1, 0});
    circuit.Add(rectifier, {1, 2});
    circuit.Add(load, {2, 0});
    circuit.Add(reference, {0});

    // The negative half of the sine puts 150 V across a 1N4148 rated for 100 V
    const Core::TransientRun run = Core::RunTransient(circuit, {.StopTime = 2e-3, .TimeStep = 1e-6});
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    REQUIRE(run.Result->Warnings.size() == 1);
    CAPTURE(run.Result->Warnings[0]);
    CHECK(run.Result->Warnings[0].starts_with("D1 (1N4148) goes into reverse breakdown"));
}

TEST_CASE("A custom Zener regulates at its own breakdown voltage", "[simulator]") {
    DiodeLoop loop(Core::ComponentType::ZENER_DIODE, 20.0, 1e3);
    loop.Part.SetCustom();
    Core::DiodeParameters parameters = loop.Part.GetParameters();
    parameters.BreakdownVoltage = 9.1;
    loop.Part.SetCustomParameters(parameters);

    const Core::OperatingPointRun run = Core::RunOperatingPoint(loop.Circuit);
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    CHECK_THAT(run.Result->NodeVoltages[2], Catch::Matchers::WithinAbs(9.1, 0.05));
}

TEST_CASE("The breakdown warning uses the voltage of a custom diode", "[simulator]") {
    // 12 V across a 1N4148 is fine, but not once its custom BV drops to 10 V
    DiodeLoop loop(Core::ComponentType::DIODE, 12.0, 1e3);
    Core::Circuit reversed;
    reversed.Add(loop.Supply, {1});
    reversed.Add(loop.Series, {1, 2});
    reversed.Add(loop.Part, {0, 2});
    reversed.Add(loop.Reference, {0});

    const Core::OperatingPointRun rated = Core::RunOperatingPoint(reversed);
    if (!rated.Result) {
        FAIL(rated.Result.error());
    }
    CHECK(rated.Result->Warnings.empty());

    Core::DiodeParameters parameters = loop.Part.GetParameters();
    parameters.BreakdownVoltage = 10.0;
    loop.Part.SetCustomParameters(parameters);
    const Core::OperatingPointRun custom = Core::RunOperatingPoint(reversed);
    if (!custom.Result) {
        FAIL(custom.Result.error());
    }
    REQUIRE(custom.Result->Warnings.size() == 1);
    CAPTURE(custom.Result->Warnings[0]);
    CHECK(custom.Result->Warnings[0].starts_with("D1 (Custom) goes into reverse breakdown"));
    CHECK(custom.Result->Warnings[0].ends_with("rated for 10V"));
}

TEST_CASE("A common-emitter NPN amplifies its base current", "[simulator]") {
    const CommonEmitter stage(Core::ComponentType::NPN);
    const Core::OperatingPointRun run = Core::RunOperatingPoint(stage.Circuit);
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    const std::vector<Core::ComponentCurrent> &currents = run.Result->Currents;
    const double collector = FindCurrent(currents, "Q1.C").Current;
    const double base = FindCurrent(currents, "Q1.B").Current;
    const double emitter = FindCurrent(currents, "Q1.E").Current;
    // Currents flow into the collector and base and out of the emitter, and add up to zero
    CHECK(collector > 0.0);
    CHECK(base > 0.0);
    // ngspice converges to within a picoampere, so the sum is only that close to zero
    CHECK_THAT(collector + base + emitter, Catch::Matchers::WithinAbs(0.0, 1e-9));
    CHECK_THAT(collector, Catch::Matchers::WithinRel(FindCurrent(currents, "R2").Current, 1e-6));
    CHECK_THAT(base, Catch::Matchers::WithinRel(FindCurrent(currents, "R1").Current, 1e-6));
    // The 2N3904 gain at a few milliamperes, in its datasheet range
    CHECK(collector / base > 100.0);
    CHECK(collector / base < 300.0);
    // Active region: the collector stays above the base
    CHECK(run.Result->NodeVoltages[3] > run.Result->NodeVoltages[2]);
}

TEST_CASE("A PNP mirrors the voltages and currents of the NPN stage", "[simulator]") {
    const CommonEmitter stage(Core::ComponentType::PNP);
    const Core::OperatingPointRun run = Core::RunOperatingPoint(stage.Circuit);
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    const std::vector<Core::ComponentCurrent> &currents = run.Result->Currents;
    const double collector = FindCurrent(currents, "Q1.C").Current;
    const double base = FindCurrent(currents, "Q1.B").Current;
    CHECK(collector < 0.0);
    CHECK(base < 0.0);
    CHECK(FindCurrent(currents, "Q1.E").Current > 0.0);
    CHECK(collector / base > 100.0);
    CHECK(run.Result->NodeVoltages[3] < run.Result->NodeVoltages[2]);
}

TEST_CASE("Each bipolar model has its datasheet gain", "[simulator]") {
    struct Rating {
        Core::ComponentType Type;
        const char *Model;
        double MinimumGain;
        double MaximumGain;
    };
    const Rating rating = GENERATE(Rating{Core::ComponentType::NPN, "2N3904", 100.0, 300.0},
                                   Rating{Core::ComponentType::NPN, "2N2222A", 100.0, 300.0},
                                   Rating{Core::ComponentType::NPN, "BC547B", 200.0, 450.0},
                                   Rating{Core::ComponentType::PNP, "2N3906", 100.0, 300.0},
                                   Rating{Core::ComponentType::PNP, "2N2907A", 100.0, 300.0},
                                   Rating{Core::ComponentType::PNP, "BC557B", 220.0, 475.0});
    CAPTURE(rating.Model);
    CommonEmitter stage(rating.Type);
    stage.Transistor.SetModel(*Core::FindBJTModel(rating.Type, rating.Model));
    // 47k sets a base current near 0.24 mA, so the collector runs at a few tens of milliamperes
    stage.BaseResistor.SetValue(47e3);
    stage.CollectorResistor.SetValue(10.0);

    const Core::OperatingPointRun run = Core::RunOperatingPoint(stage.Circuit);
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    const double gain =
        FindCurrent(run.Result->Currents, "Q1.C").Current / FindCurrent(run.Result->Currents, "Q1.B").Current;
    CHECK(gain > rating.MinimumGain);
    CHECK(gain < rating.MaximumGain);
}

TEST_CASE("An NMOS in saturation follows the level 1 square law", "[simulator]") {
    // 3 V on the gate of a 2N7000 (VTO = 2.1 V) sets about 30 mA; through 10 Ohm the drain stays near 5 V, well
    // above the 0.9 V overdrive, so the channel is saturated
    LowSideSwitch stage(Core::ComponentType::NMOS, 3.0);
    stage.Load.SetValue(10.0);
    const Core::OperatingPointRun run = Core::RunOperatingPoint(stage.Circuit);
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    const Core::MOSFETParameters &model = stage.Transistor.GetParameters();
    const double drain_voltage = run.Result->NodeVoltages[2];
    const double overdrive = 3.0 - model.ThresholdVoltage;
    REQUIRE(drain_voltage > overdrive);
    const double expected =
        model.Transconductance / 2.0 * overdrive * overdrive * (1.0 + model.ChannelModulation * drain_voltage);
    const std::vector<Core::ComponentCurrent> &currents = run.Result->Currents;
    CHECK_THAT(FindCurrent(currents, "M1.D").Current, Catch::Matchers::WithinRel(expected, 1e-4));
    CHECK_THAT(FindCurrent(currents, "M1.G").Current, Catch::Matchers::WithinAbs(0.0, 1e-12));
    CHECK_THAT(FindCurrent(currents, "M1.S").Current, Catch::Matchers::WithinRel(-expected, 1e-4));
}

TEST_CASE("Each MOSFET model has its datasheet on-resistance at 10 V of gate drive", "[simulator]") {
    struct Rating {
        Core::ComponentType Type;
        const char *Model;
        double OnResistance;
    };
    const Rating rating =
        GENERATE(Rating{Core::ComponentType::NMOS, "2N7000", 1.8}, Rating{Core::ComponentType::NMOS, "BS170", 1.2},
                 Rating{Core::ComponentType::NMOS, "IRF540N", 0.044}, Rating{Core::ComponentType::PMOS, "BS250", 14.0},
                 Rating{Core::ComponentType::PMOS, "IRF9540N", 0.117});
    CAPTURE(rating.Model);
    LowSideSwitch stage(rating.Type, 10.0);
    stage.Transistor.SetModel(*Core::FindMOSFETModel(rating.Type, rating.Model));

    const Core::OperatingPointRun run = Core::RunOperatingPoint(stage.Circuit);
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    // The drain sits close to the source, so the channel is in its linear region
    const double resistance = run.Result->NodeVoltages[2] / FindCurrent(run.Result->Currents, "M1.D").Current;
    CHECK_THAT(resistance, Catch::Matchers::WithinRel(rating.OnResistance, 0.05));
}

TEST_CASE("A PMOS high-side switch turns on when its gate is pulled low", "[simulator]") {
    Core::VCC supply;
    supply.SetName("V1");
    Core::VCC gate;
    gate.SetName("V2");
    Core::MOSFET transistor(Core::ComponentType::PMOS);
    transistor.SetName("M1");
    Core::Resistor load;
    load.SetName("R1");
    const Core::Ground reference;
    Core::Circuit circuit;
    circuit.Add(supply, {1});
    circuit.Add(gate, {2});
    circuit.Add(transistor, {3, 2, 1});
    circuit.Add(load, {3, 0});
    circuit.Add(reference, {0});

    gate.SetValue(0.0);
    const Core::OperatingPointRun on = Core::RunOperatingPoint(circuit);
    if (!on.Result) {
        FAIL(on.Result.error());
    }
    CHECK(on.Result->NodeVoltages[3] > 4.5);

    gate.SetValue(5.0);
    const Core::OperatingPointRun off = Core::RunOperatingPoint(circuit);
    if (!off.Result) {
        FAIL(off.Result.error());
    }
    CHECK(off.Result->NodeVoltages[3] < 0.01);
}

TEST_CASE("A transient keeps the transistor terminal currents balanced", "[simulator]") {
    CommonEmitter stage(Core::ComponentType::NPN);
    Core::VoltageSource input;
    input.SetName("Vin1");
    input.SetSourceType(Core::VoltageSource::SourceType::AC);
    input.SetAC({.Amplitude = 10e-3, .Frequency = 1e3, .Offset = 0.0});
    Core::Capacitor coupling;
    coupling.SetName("C1");
    coupling.SetValue(10e-6);
    stage.Circuit.Add(input, {4, 0});
    stage.Circuit.Add(coupling, {4, 2});

    const Core::TransientRun run = Core::RunTransient(stage.Circuit, {.StopTime = 2e-3, .TimeStep = 10e-6});
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    const std::vector<double> &collector = FindCurrent(run.Result->Currents, "Q1.C").Values;
    const std::vector<double> &base = FindCurrent(run.Result->Currents, "Q1.B").Values;
    const std::vector<double> &emitter = FindCurrent(run.Result->Currents, "Q1.E").Values;
    for (std::size_t index = 0; index < collector.size(); ++index) {
        CHECK_THAT(collector[index] + base[index] + emitter[index], Catch::Matchers::WithinAbs(0.0, 1e-9));
    }
    // The input sine shows up amplified and inverted on the collector
    const auto [lowest, highest] = std::ranges::minmax(run.Result->NodeVoltages[3]);
    CHECK(highest - lowest > 0.5);
}

TEST_CASE("An AC sweep gives the small-signal gain and currents of a common-emitter stage", "[simulator]") {
    CommonEmitter stage(Core::ComponentType::NPN);
    // Without base and collector resistance, high injection and leakage, the gain is the textbook gm * RC
    Core::BJTParameters ideal = stage.Transistor.GetParameters();
    ideal.BaseResistance = 0.0;
    ideal.CollectorResistance = 0.0;
    ideal.ForwardKneeCurrent = 0.0;
    ideal.LeakageSaturationCurrent = 0.0;
    stage.Transistor.SetCustomParameters(ideal);
    const Core::OperatingPointRun bias = Core::RunOperatingPoint(stage.Circuit);
    if (!bias.Result) {
        FAIL(bias.Result.error());
    }
    const double collector_current = FindCurrent(bias.Result->Currents, "Q1.C").Current;

    Core::VoltageSource input;
    input.SetName("Vin1");
    input.SetSourceType(Core::VoltageSource::SourceType::AC);
    input.SetAC({.Amplitude = 1e-3, .Frequency = 1e3, .Offset = 0.0});
    Core::Capacitor coupling;
    coupling.SetName("C1");
    coupling.SetValue(100e-6);
    stage.Circuit.Add(input, {4, 0});
    stage.Circuit.Add(coupling, {4, 2});

    const Core::ACSweepRun run = Core::RunACSweep(stage.Circuit, {.StartFrequency = 1e3, .StopFrequency = 1e4});
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    const Core::ACSweep &sweep = *run.Result;
    // Gain from base to collector is gm * RC, with gm = IC / VT, reduced a little by the Early effect output
    // resistance in parallel with RC
    const double thermal_voltage = 0.025865;
    const double early_voltage = stage.Transistor.GetParameters().EarlyVoltage;
    const double output_resistance = early_voltage / collector_current;
    const double load = 1e3 * output_resistance / (1e3 + output_resistance);
    const double expected_gain = collector_current / thermal_voltage * load;
    for (std::size_t index = 0; index < sweep.Frequencies.size(); ++index) {
        const double gain_decibels = sweep.NodeMagnitudesDecibels[3][index] - sweep.NodeMagnitudesDecibels[2][index];
        CHECK_THAT(std::pow(10.0, gain_decibels / 20.0), Catch::Matchers::WithinRel(expected_gain, 0.02));

        const auto current = [&](const std::string &name) {
            return FromDecibels(FindCurrent(sweep.CurrentMagnitudesDecibels, name).Values[index],
                                FindCurrent(sweep.CurrentPhasesDegrees, name).Values[index]);
        };
        CHECK(std::abs(current("Q1.C") + current("Q1.B") + current("Q1.E")) < 1e-9);
        CHECK(std::abs(current("Q1.C") - current("R2")) < 1e-9);
    }
}

// True when some warning starts with the text, so tests can check one rating at a time
bool HasWarning(const std::vector<std::string> &warnings, const std::string &start) {
    return std::ranges::any_of(warnings, [&start](const std::string &warning) { return warning.starts_with(start); });
}

TEST_CASE("A BJT above its VCEO rating is reported", "[simulator]") {
    // A small base current keeps the 2N3904 barely on, so nearly the whole 60 V supply sits across it
    CommonEmitter stage(Core::ComponentType::NPN);
    stage.Supply.SetValue(60.0);
    stage.BaseResistor.SetValue(10e6);
    const Core::OperatingPointRun run = Core::RunOperatingPoint(stage.Circuit);
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    REQUIRE(run.Result->Warnings.size() == 1);
    CAPTURE(run.Result->Warnings[0]);
    CHECK(run.Result->Warnings[0].starts_with("Q1 (2N3904) exceeds its VCE rating: "));
    CHECK(run.Result->Warnings[0].ends_with("against 40V"));
}

TEST_CASE("A BJT above its collector current and power ratings is reported", "[simulator]") {
    // About 11 mA into the base drives the 2N3904 past 200 mA, with several volts left across it
    CommonEmitter stage(Core::ComponentType::NPN);
    stage.BaseResistor.SetValue(1e3);
    stage.CollectorResistor.SetValue(10.0);
    const Core::OperatingPointRun run = Core::RunOperatingPoint(stage.Circuit);
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    CAPTURE(run.Result->Warnings);
    CHECK(HasWarning(run.Result->Warnings, "Q1 (2N3904) exceeds its IC rating"));
    CHECK(HasWarning(run.Result->Warnings, "Q1 (2N3904) exceeds its power rating"));
    CHECK_FALSE(HasWarning(run.Result->Warnings, "Q1 (2N3904) exceeds its VCE rating"));
}

TEST_CASE("A MOSFET above its power rating is reported, with its other ratings respected", "[simulator]") {
    // About 70 mA with nearly 20 V across the 2N7000 is about 1.4 W, over its 400 mW
    LowSideSwitch stage(Core::ComponentType::NMOS, 3.5);
    stage.Supply.SetValue(20.0);
    stage.Load.SetValue(10.0);
    const Core::OperatingPointRun run = Core::RunOperatingPoint(stage.Circuit);
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    REQUIRE(run.Result->Warnings.size() == 1);
    CAPTURE(run.Result->Warnings[0]);
    CHECK(run.Result->Warnings[0].starts_with("M1 (2N7000) exceeds its power rating: "));
    CHECK(run.Result->Warnings[0].ends_with("against 400mW"));
}

TEST_CASE("A MOSFET gate above its VGS rating is reported", "[simulator]") {
    LowSideSwitch stage(Core::ComponentType::PMOS, 25.0);
    const Core::OperatingPointRun run = Core::RunOperatingPoint(stage.Circuit);
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    CAPTURE(run.Result->Warnings);
    CHECK(HasWarning(run.Result->Warnings, "M1 (BS250) exceeds its VGS rating: 25V against 20V"));
}

TEST_CASE("A transient checks the average power, so short pulses above the rating pass", "[simulator]") {
    // The same 1.4 W as the operating point test, but only during a tenth of every period
    Core::VCC supply;
    supply.SetName("V1");
    supply.SetValue(20.0);
    Core::VoltageSource gate;
    gate.SetName("Vin1");
    gate.SetSourceType(Core::VoltageSource::SourceType::PULSE);
    gate.SetPulse({.Low = 0.0, .High = 3.5, .Width = 0.1e-3, .Period = 1e-3});
    Core::Resistor load;
    load.SetName("R1");
    load.SetValue(10.0);
    Core::MOSFET transistor(Core::ComponentType::NMOS);
    transistor.SetName("M1");
    const Core::Ground reference;
    Core::Circuit circuit;
    circuit.Add(supply, {1});
    circuit.Add(gate, {3, 0});
    circuit.Add(load, {1, 2});
    circuit.Add(transistor, {2, 3, 0});
    circuit.Add(reference, {0});

    const Core::TransientRun pulsed = Core::RunTransient(circuit, {.StopTime = 5e-3, .TimeStep = 1e-6});
    if (!pulsed.Result) {
        FAIL(pulsed.Result.error());
    }
    CAPTURE(pulsed.Result->Warnings);
    CHECK(pulsed.Result->Warnings.empty());
    // With picofarads of gate capacitance the drain current settles within microseconds of each gate edge
    const std::vector<double> &times = pulsed.Result->Times;
    const std::vector<double> &drain = FindCurrent(pulsed.Result->Currents, "M1.D").Values;
    const auto current_at = [&](const double time) {
        return drain[static_cast<std::size_t>(std::ranges::lower_bound(times, time) - times.begin())];
    };
    CHECK_THAT(current_at(10e-6), Catch::Matchers::WithinRel(current_at(95e-6), 0.01));

    gate.SetPulse({.Low = 0.0, .High = 3.5, .Width = 0.9e-3, .Period = 1e-3});
    const Core::TransientRun mostly_on = Core::RunTransient(circuit, {.StopTime = 5e-3, .TimeStep = 1e-6});
    if (!mostly_on.Result) {
        FAIL(mostly_on.Result.error());
    }
    CAPTURE(mostly_on.Result->Warnings);
    CHECK(HasWarning(mostly_on.Result->Warnings, "M1 (2N7000) exceeds its power rating"));
}

TEST_CASE("The rating warnings use the ratings of a custom transistor", "[simulator]") {
    CommonEmitter stage(Core::ComponentType::NPN);
    Core::BJTParameters parameters = stage.Transistor.GetParameters();
    parameters.MaxCollectorEmitterVoltage = 5.0;
    stage.Transistor.SetCustomParameters(parameters);
    const Core::OperatingPointRun run = Core::RunOperatingPoint(stage.Circuit);
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    CAPTURE(run.Result->Warnings);
    CHECK(HasWarning(run.Result->Warnings, "Q1 (Custom) exceeds its VCE rating"));
}

TEST_CASE("An NMOS that is off conducts in reverse through its body diode", "[simulator]") {
    // The drain is pulled below the source with the gate off, so only the body-drain junction can conduct
    LowSideSwitch stage(Core::ComponentType::NMOS, 0.0);
    stage.Supply.SetValue(-5.0);
    stage.Load.SetValue(100.0);
    const Core::OperatingPointRun run = Core::RunOperatingPoint(stage.Circuit);
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    const double drain_voltage = run.Result->NodeVoltages[2];
    CHECK(drain_voltage < -0.6);
    CHECK(drain_voltage > -0.9);
    // The current flows out of the drain, from source to drain through the diode
    CHECK_THAT(FindCurrent(run.Result->Currents, "M1.D").Current,
               Catch::Matchers::WithinRel((-5.0 - drain_voltage) / 100.0, 1e-6));
}

TEST_CASE("A DC sweep of a supply follows the divider ratio at every point", "[simulator]") {
    const Divider divider;
    const Core::DCSweepRun run =
        Core::RunDCSweep(divider.Circuit, {.Swept = {.Source = "V1", .Start = 0.0, .Stop = 10.0, .Step = 1.0}});
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    const Core::DCSweep &sweep = *run.Result;
    CHECK(sweep.SweptSource == "V1");
    CHECK(sweep.SweptUnit == "V");
    CHECK(sweep.SteppedSource.empty());
    REQUIRE(sweep.SweptValues.size() == 11);
    REQUIRE(sweep.Curves.size() == 1);
    const Core::DCSweepCurve &curve = sweep.Curves[0];
    for (std::size_t index = 0; index < sweep.SweptValues.size(); ++index) {
        const double supply = sweep.SweptValues[index];
        CHECK_THAT(supply, Catch::Matchers::WithinAbs(static_cast<double>(index), 1e-9));
        CHECK_THAT(curve.NodeVoltages[2][index], Catch::Matchers::WithinAbs(0.75 * supply, 1e-9));
        CHECK_THAT(FindCurrent(curve.Currents, "R1").Values[index], Catch::Matchers::WithinAbs(supply / 4e3, 1e-12));
    }
}

TEST_CASE("A DC sweep can drive a current source", "[simulator]") {
    Core::CurrentSource source;
    source.SetName("I1");
    Core::Resistor load;
    load.SetName("R1");
    const Core::Ground reference;
    Core::Circuit circuit;
    circuit.Add(source, {0, 1});
    circuit.Add(load, {1, 0});
    circuit.Add(reference, {0});

    const Core::DCSweepRun run =
        Core::RunDCSweep(circuit, {.Swept = {.Source = "I1", .Start = 0.0, .Stop = 2e-3, .Step = 0.5e-3}});
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    CHECK(run.Result->SweptUnit == "A");
    REQUIRE(run.Result->SweptValues.size() == 5);
    for (std::size_t index = 0; index < 5; ++index) {
        CHECK_THAT(run.Result->Curves[0].NodeVoltages[1][index],
                   Catch::Matchers::WithinAbs(run.Result->SweptValues[index] * 1e3, 1e-9));
    }
}

TEST_CASE("A DC sweep traces the I-V curve of a diode", "[simulator]") {
    DiodeLoop loop(Core::ComponentType::DIODE, 1.0, 10.0);
    const Core::DCSweepRun run =
        Core::RunDCSweep(loop.Circuit, {.Swept = {.Source = "V1", .Start = 0.0, .Stop = 1.0, .Step = 0.01}});
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    const std::vector<double> &current = FindCurrent(run.Result->Curves[0].Currents, "D1").Values;
    REQUIRE(current.size() == 101);
    // Barely any current at 0.2 V, below the knee, and well over a milliampere once the supply passes it
    CHECK(current[20] < 1e-6);
    CHECK(current[100] > 1e-3);
    CHECK(std::ranges::is_sorted(current));
}

TEST_CASE("A stepped DC sweep draws the collector characteristics of a BJT", "[simulator]") {
    // VCE swept on the collector, with the base current stepped from 10 uA to 30 uA
    Core::VCC collector;
    collector.SetName("V1");
    Core::CurrentSource base;
    base.SetName("I1");
    Core::BJT transistor(Core::ComponentType::NPN);
    transistor.SetName("Q1");
    const Core::Ground reference;
    Core::Circuit circuit;
    circuit.Add(collector, {1});
    circuit.Add(base, {0, 2});
    circuit.Add(transistor, {1, 2, 0});
    circuit.Add(reference, {0});

    const Core::DCSweepRun run = Core::RunDCSweep(
        circuit, {.Swept = {.Source = "V1", .Start = 0.0, .Stop = 5.0, .Step = 0.05},
                  .Stepped = Core::SweepRange{.Source = "I1", .Start = 10e-6, .Stop = 30e-6, .Step = 10e-6}});
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    const Core::DCSweep &sweep = *run.Result;
    CHECK(sweep.SteppedSource == "I1");
    CHECK(sweep.SteppedUnit == "A");
    REQUIRE(sweep.Curves.size() == 3);
    double previous_current = 0.0;
    for (std::size_t step = 0; step < sweep.Curves.size(); ++step) {
        const Core::DCSweepCurve &curve = sweep.Curves[step];
        CHECK_THAT(curve.StepValue, Catch::Matchers::WithinRel(10e-6 * static_cast<double>(step + 1), 1e-9));
        const std::vector<double> &collector_current = FindCurrent(curve.Currents, "Q1.C").Values;
        REQUIRE(collector_current.size() == sweep.SweptValues.size());
        // Each curve rises from saturation to the active region, where it sits at about BF times the base current
        CHECK(std::abs(collector_current.front()) < 0.1 * collector_current.back());
        const double gain = collector_current.back() / curve.StepValue;
        CHECK(gain > 100.0);
        CHECK(gain < 500.0);
        CHECK(collector_current.back() > previous_current);
        previous_current = collector_current.back();
    }
}

TEST_CASE("Invalid DC sweep settings are rejected before reaching ngspice", "[simulator]") {
    const Divider divider;
    const auto error_of = [&divider](const Core::DCSweepSettings &settings) {
        const Core::DCSweepRun run = Core::RunDCSweep(divider.Circuit, settings);
        REQUIRE_FALSE(run.Result);
        CHECK(run.Messages.empty());
        return run.Result.error();
    };
    CHECK(error_of({.Swept = {.Source = ""}}).starts_with("Pick the swept source"));
    CHECK(error_of({.Swept = {.Source = "R1"}}).contains("not a voltage source"));
    CHECK(error_of({.Swept = {.Source = "V9"}}).contains("not a voltage source"));
    CHECK(error_of({.Swept = {.Source = "V1", .Start = 1.0, .Stop = 1.0}}).contains("must differ"));
    CHECK(error_of({.Swept = {.Source = "V1", .Step = 0.0}}).contains("toward its stop value"));
    CHECK(error_of({.Swept = {.Source = "V1", .Step = -0.1}}).contains("toward its stop value"));
    CHECK(error_of({.Swept = {.Source = "V1", .Step = 1e-6}}).contains("more than"));
    CHECK(error_of({.Swept = {.Source = "V1"}, .Stepped = Core::SweepRange{.Source = "V1"}})
              .contains("must differ from the swept one"));
    // A downward sweep is fine
    const Core::DCSweepRun downward =
        Core::RunDCSweep(divider.Circuit, {.Swept = {.Source = "V1", .Start = 5.0, .Stop = 0.0, .Step = -1.0}});
    REQUIRE(downward.Result);
    CHECK(downward.Result->SweptValues.front() == 5.0);
}

TEST_CASE("Only supplies and independent sources can be swept", "[simulator]") {
    const Divider divider;
    CHECK(Core::GetSweepableSources(divider.Circuit) == std::vector<std::string>{"V1"});
    const LowPass low_pass;
    CHECK(Core::GetSweepableSources(low_pass.Circuit) == std::vector<std::string>{"Vin1"});
}

TEST_CASE("Controlled sources apply their gain with the SPICE signs", "[simulator]") {
    using Core::ComponentType;
    // Output voltage on the 1k load, and the source current, positive into its first output terminal. A VCVS or
    // CCVS sets the voltage and feeds the load; a VCCS or CCCS pushes its current from terminal 1 to terminal 2
    // through itself, so it pulls the load node below ground
    const auto [type, output, current] = GENERATE(table<ComponentType, double, double>({
        {ComponentType::VCVS, 10.0, -10e-3},
        {ComponentType::VCCS, -1.0, 1e-3},
        {ComponentType::CCCS, -10.0, 10e-3},
        {ComponentType::CCVS, 1.0, -1e-3},
    }));
    CAPTURE(Core::GetTypeName(type));
    const ControlledStage stage(type);
    const Core::OperatingPointRun run = Core::RunOperatingPoint(stage.Circuit);
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    CHECK_THAT(run.Result->NodeVoltages[2], Catch::Matchers::WithinRel(output, 1e-9));
    CHECK_THAT(FindCurrent(run.Result->Currents, stage.Source.GetName()).Current,
               Catch::Matchers::WithinRel(current, 1e-9));
    // The probe on the controlling resistor leaves its current as it was
    CHECK_THAT(FindCurrent(run.Result->Currents, "R1").Current, Catch::Matchers::WithinRel(1e-3, 1e-9));
}

TEST_CASE("A current-controlled source can follow a voltage source or a probed terminal", "[simulator]") {
    ControlledStage stage(Core::ComponentType::CCVS);
    // The input source delivers its current, so it reports -1 mA and the CCVS sets -1 V
    stage.Source.SetControllingCurrent("Vin1");
    const Core::OperatingPointRun run = Core::RunOperatingPoint(stage.Circuit);
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    CHECK_THAT(run.Result->NodeVoltages[2], Catch::Matchers::WithinRel(-1.0, 1e-9));
}

TEST_CASE("A current-controlled source needs a current of another part of the circuit", "[simulator]") {
    ControlledStage stage(Core::ComponentType::CCCS);
    const auto controlling = GENERATE(std::string(""), std::string("F1"), std::string("R9"));
    CAPTURE(controlling);
    stage.Source.SetControllingCurrent(controlling);
    const Core::OperatingPointRun run = Core::RunOperatingPoint(stage.Circuit);
    REQUIRE_FALSE(run.Result.has_value());
    // Rejected before reaching ngspice, with a reason
    CHECK(run.Messages.empty());
    CHECK(run.Result.error().contains("F1"));
}

TEST_CASE("An inverting op-amp amplifier has the gain its resistors set", "[simulator]") {
    const InvertingAmplifier amplifier(0.5);
    const Core::OperatingPointRun run = Core::RunOperatingPoint(amplifier.Circuit);
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    CHECK_THAT(run.Result->NodeVoltages[3], Catch::Matchers::WithinRel(-5.0, 1e-3));
    // The inverting input is held at virtual ground
    CHECK_THAT(run.Result->NodeVoltages[2], Catch::Matchers::WithinAbs(0.0, 1e-3));
    // The output sinks the current of both resistors: 0.5 mA into the output terminal
    CHECK_THAT(FindCurrent(run.Result->Currents, "U1").Current, Catch::Matchers::WithinRel(0.5e-3, 1e-3));
}

TEST_CASE("An op-amp output saturates at its supply pins", "[simulator]") {
    const double input = GENERATE(2.0, -2.0);
    CAPTURE(input);
    const InvertingAmplifier amplifier(input);
    const Core::OperatingPointRun run = Core::RunOperatingPoint(amplifier.Circuit);
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    // The gain of -10 would ask for -20 V or +20 V; the output stops within a few millivolts of the rail
    const double rail = input > 0.0 ? -15.0 : 15.0;
    CHECK_THAT(run.Result->NodeVoltages[3], Catch::Matchers::WithinAbs(rail, 0.01));
}

TEST_CASE("An AC sweep of an op-amp amplifier has its closed-loop gain", "[simulator]") {
    InvertingAmplifier amplifier(0.0);
    amplifier.Input.SetSourceType(Core::VoltageSource::SourceType::AC);
    const Core::ACSweepRun run =
        Core::RunACSweep(amplifier.Circuit, {.StartFrequency = 10.0, .StopFrequency = 1e3, .PointsPerDecade = 2});
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    for (std::size_t index = 0; index < run.Result->Frequencies.size(); ++index) {
        // A gain of 10 is 20 dB, inverted by 180 degrees; up to 1 kHz, the 1 MHz gain-bandwidth product of the
        // ideal op-amp shifts the phase by less than a degree
        CHECK_THAT(run.Result->NodeMagnitudesDecibels[3][index], Catch::Matchers::WithinAbs(20.0, 0.01));
        CHECK_THAT(std::abs(run.Result->NodePhasesDegrees[3][index]), Catch::Matchers::WithinAbs(180.0, 1.0));
    }
}

TEST_CASE("An ideal op-amp rolls off at its gain-bandwidth product", "[simulator]") {
    InvertingAmplifier amplifier(0.0);
    amplifier.Input.SetSourceType(Core::VoltageSource::SourceType::AC);
    // A noise gain of 11 leaves 1 MHz / 11 of closed-loop bandwidth
    const double corner = 1e6 / 11.0;
    const Core::ACSweepRun run = Core::RunACSweep(
        amplifier.Circuit, {.StartFrequency = corner, .StopFrequency = corner * 10.0, .PointsPerDecade = 1});
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    CHECK_THAT(run.Result->NodeMagnitudesDecibels[3][0], Catch::Matchers::WithinAbs(20.0 - 3.01, 0.1));
}

TEST_CASE("A uA741 macromodel amplifies like the ideal op-amp but swings 1 V short of its supplies", "[simulator]") {
    const double input = GENERATE(0.5, 2.0, -2.0);
    CAPTURE(input);
    InvertingAmplifier amplifier(input);
    amplifier.Amplifier.SetModel(*Core::FindOpAmpModel(Core::ComponentType::OP_AMP, "uA741"));
    const Core::OperatingPointRun run = Core::RunOperatingPoint(amplifier.Circuit);
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    // The limits stop the output about 1 V from each 15 V supply, as the datasheet swing
    const double expected = input == 0.5 ? -5.0 : (input > 0.0 ? -14.0 : 14.0);
    CHECK_THAT(run.Result->NodeVoltages[3], Catch::Matchers::WithinAbs(expected, 0.1));
}

TEST_CASE("A uA741 macromodel slews at its datasheet rate", "[simulator]") {
    // A unity-gain follower driven by a 10 V step, so the output can only follow at the slew rate
    Core::VoltageSource input;
    input.SetName("Vin1");
    input.SetSourceType(Core::VoltageSource::SourceType::PULSE);
    input.SetPulse(
        {.Low = -5.0, .High = 5.0, .Delay = 1e-6, .RiseTime = 1e-9, .FallTime = 1e-9, .Width = 1.0, .Period = 2.0});
    Core::OpAmp follower;
    follower.SetName("U1");
    follower.SetModel(*Core::FindOpAmpModel(Core::ComponentType::OP_AMP, "uA741"));
    Core::VCC positive;
    positive.SetName("V1");
    positive.SetValue(15.0);
    Core::VCC negative;
    negative.SetName("V2");
    negative.SetValue(-15.0);
    Core::Resistor load;
    load.SetName("R1");
    load.SetValue(10e3);
    const Core::Ground reference;
    Core::Circuit circuit;
    circuit.Add(input, {1, 0});
    circuit.Add(follower, {2, 1, 2, 3, 4});
    circuit.Add(positive, {3});
    circuit.Add(negative, {4});
    circuit.Add(load, {2, 0});
    circuit.Add(reference, {0});

    const Core::TransientRun run = Core::RunTransient(circuit, {.StopTime = 40e-6, .TimeStep = 10e-9});
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    // Time from -3 V to +3 V on the way up
    const std::vector<double> &times = run.Result->Times;
    const std::vector<double> &output = run.Result->NodeVoltages[2];
    const auto crossing = [&](const double level) {
        const auto index = std::ranges::find_if(output, [level](const double value) { return value >= level; });
        REQUIRE(index != output.end());
        return times[static_cast<std::size_t>(index - output.begin())];
    };
    const double slew_rate = 6.0 / (crossing(3.0) - crossing(-3.0));
    CHECK_THAT(slew_rate, Catch::Matchers::WithinRel(0.5e6, 0.05));
}

TEST_CASE("A potentiometer divides its supply by the position of its wiper", "[simulator]") {
    Core::VCC supply;
    supply.SetName("V1");
    supply.SetValue(10.0);
    Core::Potentiometer pot;
    pot.SetName("RV1");
    pot.SetWiper(0.25);
    const Core::Ground reference;
    // End 1 on the supply, end 2 on ground, the wiper on node 2
    Core::Circuit circuit;
    circuit.Add(supply, {1});
    circuit.Add(pot, {1, 0, 2});
    circuit.Add(reference, {0});

    const Core::OperatingPointRun run = Core::RunOperatingPoint(circuit);
    REQUIRE(run.Result.has_value());
    // A quarter of the way from the supply: 2.5k above the wiper and 7.5k below it
    CHECK_THAT(run.Result->NodeVoltages[2], Catch::Matchers::WithinAbs(7.5, 1e-6));
    const auto current = [&run](const std::string &name) {
        const auto found = std::ranges::find(run.Result->Currents, name, &Core::ComponentCurrent::Name);
        REQUIRE(found != run.Result->Currents.end());
        return found->Current;
    };
    // Positive into each terminal: 1 mA goes in at end 1 and out at end 2, and the unloaded wiper carries none
    CHECK_THAT(current("RV1.A"), Catch::Matchers::WithinAbs(1e-3, 1e-9));
    CHECK_THAT(current("RV1.B"), Catch::Matchers::WithinAbs(-1e-3, 1e-9));
    CHECK_THAT(current("RV1.W"), Catch::Matchers::WithinAbs(0.0, 1e-9));
}

TEST_CASE("A load on the wiper pulls its voltage down and takes its current", "[simulator]") {
    Core::VCC supply;
    supply.SetName("V1");
    supply.SetValue(10.0);
    Core::Potentiometer pot;
    pot.SetName("RV1");
    pot.SetValue(10e3);
    pot.SetWiper(0.5);
    Core::Resistor load;
    load.SetName("R1");
    load.SetValue(10e3);
    const Core::Ground reference;
    Core::Circuit circuit;
    circuit.Add(supply, {1});
    circuit.Add(pot, {1, 0, 2});
    circuit.Add(load, {2, 0});
    circuit.Add(reference, {0});

    const Core::OperatingPointRun run = Core::RunOperatingPoint(circuit);
    REQUIRE(run.Result.has_value());
    // 5k above the wiper, and 5k in parallel with the 10k load below it: 10 * (10/3) / (5 + 10/3)
    CHECK_THAT(run.Result->NodeVoltages[2], Catch::Matchers::WithinAbs(4.0, 1e-6));
    const auto wiper = std::ranges::find(run.Result->Currents, "RV1.W", &Core::ComponentCurrent::Name);
    REQUIRE(wiper != run.Result->Currents.end());
    // 0.4 mA out of the wiper into the load
    CHECK_THAT(wiper->Current, Catch::Matchers::WithinAbs(-0.4e-3, 1e-9));
}

TEST_CASE("A potentiometer's wiper moves its output in an AC sweep too", "[simulator]") {
    Core::VoltageSource source;
    source.SetName("Vin1");
    source.SetSourceType(Core::VoltageSource::SourceType::AC);
    Core::Potentiometer pot;
    pot.SetName("RV1");
    pot.SetWiper(0.8);
    const Core::Ground reference;
    Core::Circuit circuit;
    circuit.Add(source, {1, 0});
    circuit.Add(pot, {1, 0, 2});
    circuit.Add(reference, {0});

    const Core::ACSweepRun run = Core::RunACSweep(circuit, {.StartFrequency = 1e3, .StopFrequency = 1e4});
    REQUIRE(run.Result.has_value());
    // 20% of the input is left at the wiper, which is -13.98 dB
    CHECK_THAT(run.Result->NodeMagnitudesDecibels[2].front(), Catch::Matchers::WithinAbs(20.0 * std::log10(0.2), 1e-3));
}

TEST_CASE("A closed switch passes the current of its circuit and an open one stops it", "[simulator]") {
    Core::VCC supply;
    supply.SetName("V1");
    supply.SetValue(10.0);
    Core::Switch contact(Core::ComponentType::SWITCH);
    contact.SetName("SW1");
    Core::Resistor load;
    load.SetName("R1");
    load.SetValue(1e3);
    const Core::Ground reference;
    Core::Circuit circuit;
    circuit.Add(supply, {1});
    circuit.Add(contact, {1, 2});
    circuit.Add(load, {2, 0});
    circuit.Add(reference, {0});
    const auto switch_current = [](const Core::OperatingPointRun &run) {
        REQUIRE(run.Result.has_value());
        const auto found = std::ranges::find(run.Result->Currents, "SW1", &Core::ComponentCurrent::Name);
        REQUIRE(found != run.Result->Currents.end());
        return found->Current;
    };

    SECTION("open") {
        const Core::OperatingPointRun run = Core::RunOperatingPoint(circuit);
        CHECK_THAT(run.Result->NodeVoltages[2], Catch::Matchers::WithinAbs(0.0, 1e-6));
        CHECK_THAT(switch_current(run), Catch::Matchers::WithinAbs(0.0, 1e-9));
    }
    SECTION("closed") {
        contact.SetClosed(true);
        const Core::OperatingPointRun run = Core::RunOperatingPoint(circuit);
        // 1 mOhm in series with 1k: the load gets all but a millionth of the supply
        CHECK_THAT(run.Result->NodeVoltages[2], Catch::Matchers::WithinAbs(10.0, 1e-4));
        CHECK_THAT(switch_current(run), Catch::Matchers::WithinAbs(10e-3, 1e-6));
    }
}

TEST_CASE("A voltage-controlled switch closes above its threshold and opens below it", "[simulator]") {
    Core::VCC supply;
    supply.SetName("V1");
    supply.SetValue(10.0);
    Core::VCC control;
    control.SetName("V2");
    Core::VoltageControlledSwitch contact;
    contact.SetName("S1");
    Core::Resistor load;
    load.SetName("R1");
    load.SetValue(1e3);
    const Core::Ground reference;
    // The supply on node 1 reaches the load on node 3 through the switch, which node 2 controls
    Core::Circuit circuit;
    circuit.Add(supply, {1});
    circuit.Add(control, {2});
    circuit.Add(contact, {1, 3, 2, 0});
    circuit.Add(load, {3, 0});
    circuit.Add(reference, {0});

    control.SetValue(5.0);
    const Core::OperatingPointRun above = Core::RunOperatingPoint(circuit);
    REQUIRE(above.Result.has_value());
    // 10 mOhm in series with 1k
    CHECK_THAT(above.Result->NodeVoltages[3], Catch::Matchers::WithinAbs(10.0, 1e-3));

    control.SetValue(0.0);
    const Core::OperatingPointRun below = Core::RunOperatingPoint(circuit);
    REQUIRE(below.Result.has_value());
    CHECK_THAT(below.Result->NodeVoltages[3], Catch::Matchers::WithinAbs(0.0, 1e-4));
}

TEST_CASE("A voltage-controlled switch follows a pulse in a transient", "[simulator]") {
    Core::VCC supply;
    supply.SetName("V1");
    supply.SetValue(10.0);
    Core::VoltageSource control;
    control.SetName("Vin1");
    control.SetSourceType(Core::VoltageSource::SourceType::PULSE);
    control.SetPulse(
        {.Low = 0.0, .High = 5.0, .Delay = 0.0, .RiseTime = 1e-6, .FallTime = 1e-6, .Width = 0.5e-3, .Period = 1e-3});
    Core::VoltageControlledSwitch contact;
    contact.SetName("S1");
    Core::Resistor load;
    load.SetName("R1");
    load.SetValue(1e3);
    const Core::Ground reference;
    Core::Circuit circuit;
    circuit.Add(supply, {1});
    circuit.Add(control, {2, 0});
    circuit.Add(contact, {1, 3, 2, 0});
    circuit.Add(load, {3, 0});
    circuit.Add(reference, {0});

    const Core::TransientRun run = Core::RunTransient(circuit, {.StopTime = 2e-3, .TimeStep = 5e-6});
    REQUIRE(run.Result.has_value());
    const Core::Transient &transient = *run.Result;
    const auto voltage_at = [&transient](const double time) {
        const auto index =
            static_cast<std::size_t>(std::ranges::lower_bound(transient.Times, time) - transient.Times.begin());
        return transient.NodeVoltages[3][index];
    };
    // On for the first half of each millisecond, off for the second
    CHECK_THAT(voltage_at(0.25e-3), Catch::Matchers::WithinAbs(10.0, 1e-2));
    CHECK_THAT(voltage_at(0.75e-3), Catch::Matchers::WithinAbs(0.0, 1e-2));
    CHECK_THAT(voltage_at(1.25e-3), Catch::Matchers::WithinAbs(10.0, 1e-2));
    CHECK_THAT(voltage_at(1.75e-3), Catch::Matchers::WithinAbs(0.0, 1e-2));
}

TEST_CASE("A relay closes its contact once its coil current is high enough, and takes L over R to get there",
          "[simulator]") {
    Core::VCC supply;
    supply.SetName("V1");
    supply.SetValue(10.0);
    Core::VoltageSource coil_drive;
    coil_drive.SetName("Vin1");
    coil_drive.SetSourceType(Core::VoltageSource::SourceType::PULSE);
    coil_drive.SetPulse(
        {.Low = 0.0, .High = 5.0, .Delay = 0.0, .RiseTime = 1e-6, .FallTime = 1e-6, .Width = 5e-3, .Period = 10e-3});
    Core::Relay relay;
    relay.SetName("K1");
    Core::Resistor load;
    load.SetName("R1");
    load.SetValue(1e3);
    const Core::Ground reference;
    // The contact between the supply on node 1 and the load on node 3; the coil from node 2 to ground
    Core::Circuit circuit;
    circuit.Add(supply, {1});
    circuit.Add(coil_drive, {2, 0});
    circuit.Add(relay, {1, 3, 2, 0});
    circuit.Add(load, {3, 0});
    circuit.Add(reference, {0});

    const Core::TransientRun run = Core::RunTransient(circuit, {.StopTime = 3e-3, .TimeStep = 5e-6});
    REQUIRE(run.Result.has_value());
    const Core::Transient &transient = *run.Result;
    const std::vector<double> &output = transient.NodeVoltages[3];
    double closing_time = -1.0;
    for (std::size_t index = 0; index < output.size(); ++index) {
        if (output[index] > 5.0) {
            closing_time = transient.Times[index];
            break;
        }
    }
    // The resistance of the coil sees 5 V (1 - exp(-t / tau)) with tau = 50 mH / 70 Ohm, and the contact closes at 3.5
    // V
    const double tau = 50e-3 / 70.0;
    const double expected = -tau * std::log(1.0 - 3.5 / 5.0);
    INFO(std::format("The contact closed at {} s, against {} s expected", closing_time, expected));
    REQUIRE(closing_time > 0.0);
    CHECK_THAT(closing_time, Catch::Matchers::WithinRel(expected, 0.05));
    // Closed, the contact passes nearly all of the supply
    CHECK_THAT(output.back(), Catch::Matchers::WithinAbs(10.0, 0.1));
}

namespace {

/**
 * @struct  SteppedDown
 * @brief   An AC source on node 1 across the primary of a 2:1 transformer, with a load on its secondary on node 2.
 */
struct SteppedDown {
    Core::VoltageSource Source;
    Core::Transformer Transformer;
    Core::Resistor Load;
    Core::Ground Reference;
    Core::Circuit Circuit;

    explicit SteppedDown(const double load_resistance) {
        Source.SetName("Vin1");
        Source.SetSourceType(Core::VoltageSource::SourceType::AC);
        Source.SetAC({.Amplitude = 10.0, .Frequency = 1e3, .Offset = 0.0, .Magnitude = 1.0});
        Transformer.SetName("T1");
        Transformer.SetParameters({.TurnsRatio = 2.0, .Coupling = 0.999, .WindingResistance = 0.01});
        Load.SetName("R1");
        Load.SetValue(load_resistance);
        Circuit.Add(Source, {1, 0});
        Circuit.Add(Transformer, {1, 0, 2, 0});
        Circuit.Add(Load, {2, 0});
        Circuit.Add(Reference, {0});
    }
};

} // namespace

TEST_CASE("A 2:1 transformer halves the voltage of its primary on an open secondary", "[simulator]") {
    SteppedDown circuit(1e9);
    const Core::ACSweepRun run = Core::RunACSweep(circuit.Circuit, {.StartFrequency = 1e3, .StopFrequency = 1e4});
    REQUIRE(run.Result.has_value());
    // The coupling takes a little more: 0.999 / 2
    CHECK_THAT(run.Result->NodeMagnitudesDecibels[2].front(),
               Catch::Matchers::WithinAbs(20.0 * std::log10(0.4995), 1e-2));
    // The two windings are in phase through the dots
    CHECK_THAT(run.Result->NodePhasesDegrees[2].front(), Catch::Matchers::WithinAbs(0.0, 0.5));
}

TEST_CASE("A loaded 2:1 transformer draws twice the secondary current from its primary", "[simulator]") {
    SteppedDown circuit(10.0);
    const Core::TransientRun run = Core::RunTransient(circuit.Circuit, {.StopTime = 5e-3, .TimeStep = 5e-6});
    REQUIRE(run.Result.has_value());
    const Core::Transient &transient = *run.Result;
    const auto peak = [](const std::vector<double> &values) {
        return std::max(std::abs(std::ranges::min(values)), std::ranges::max(values));
    };
    const auto current = [&transient](const std::string &name) {
        const auto trace = std::ranges::find(transient.Currents, name, &Core::ComponentTrace::Name);
        REQUIRE(trace != transient.Currents.end());
        return trace->Values;
    };
    // Look at the last cycle, once the transient of the magnetizing current has settled
    const auto last_cycle = [&transient](const std::vector<double> &values) {
        const auto first =
            static_cast<std::size_t>(std::ranges::lower_bound(transient.Times, 4e-3) - transient.Times.begin());
        return std::vector<double>(values.begin() + static_cast<std::ptrdiff_t>(first), values.end());
    };
    const double secondary_voltage = peak(last_cycle(transient.NodeVoltages[2]));
    const double secondary_current = peak(last_cycle(current("T1.S")));
    const double primary_current = peak(last_cycle(current("T1.P")));
    // 10 V in, about 5 V out across 10 Ohm: 0.5 A in the secondary and about 0.25 A in the primary
    CHECK_THAT(secondary_voltage, Catch::Matchers::WithinRel(5.0, 0.05));
    CHECK_THAT(secondary_current, Catch::Matchers::WithinRel(0.5, 0.05));
    CHECK_THAT(primary_current, Catch::Matchers::WithinRel(0.25, 0.1));
}

namespace {

/**
 * @struct  GateTruthRow
 * @brief   One row of a truth table: the inputs of a two-input gate and the output it must give.
 */
struct GateTruthRow {
    Core::ComponentType Type;
    bool A;
    bool B;
    bool Expected;
};

} // namespace

TEST_CASE("Each logic gate gives its truth table at the operating point", "[simulator][digital]") {
    SKIP_WITHOUT_DIGITAL_MODELS();
    const auto row = GENERATE(GateTruthRow{Core::ComponentType::AND, false, false, false},
                              GateTruthRow{Core::ComponentType::AND, true, false, false},
                              GateTruthRow{Core::ComponentType::AND, true, true, true},
                              GateTruthRow{Core::ComponentType::OR, false, false, false},
                              GateTruthRow{Core::ComponentType::OR, true, false, true},
                              GateTruthRow{Core::ComponentType::OR, true, true, true},
                              GateTruthRow{Core::ComponentType::NAND, true, true, false},
                              GateTruthRow{Core::ComponentType::NAND, false, true, true},
                              GateTruthRow{Core::ComponentType::NOR, false, false, true},
                              GateTruthRow{Core::ComponentType::NOR, true, false, false},
                              GateTruthRow{Core::ComponentType::XOR, true, false, true},
                              GateTruthRow{Core::ComponentType::XOR, true, true, false},
                              GateTruthRow{Core::ComponentType::XNOR, true, true, true},
                              GateTruthRow{Core::ComponentType::XNOR, false, true, false});
    Core::VoltageSource input_a;
    input_a.SetName("Vin1");
    input_a.SetValue(row.A ? 5.0 : 0.0);
    Core::VoltageSource input_b;
    input_b.SetName("Vin2");
    input_b.SetValue(row.B ? 5.0 : 0.0);
    Core::LogicPart gate(row.Type);
    gate.SetName("U1");
    const Core::Ground reference;
    // The inputs on nodes 1 and 2, the output on node 3
    Core::Circuit circuit;
    circuit.Add(input_a, {1, 0});
    circuit.Add(input_b, {2, 0});
    circuit.Add(gate, {1, 2, 3});
    circuit.Add(reference, {0});

    const Core::OperatingPointRun run = Core::RunOperatingPoint(circuit);
    REQUIRE(run.Result.has_value());
    INFO(std::format("{} with A = {} and B = {}", Core::GetTypeName(row.Type), row.A, row.B));
    CHECK_THAT(run.Result->NodeVoltages[3], Catch::Matchers::WithinAbs(row.Expected ? 5.0 : 0.0, 0.05));
}

TEST_CASE("NOT inverts and the buffer repeats its input", "[simulator][digital]") {
    SKIP_WITHOUT_DIGITAL_MODELS();
    const auto type = GENERATE(Core::ComponentType::NOT, Core::ComponentType::BUFFER);
    const bool input_high = GENERATE(false, true);
    Core::VoltageSource input;
    input.SetName("Vin1");
    input.SetValue(input_high ? 5.0 : 0.0);
    Core::LogicPart gate(type);
    gate.SetName("U1");
    const Core::Ground reference;
    Core::Circuit circuit;
    circuit.Add(input, {1, 0});
    circuit.Add(gate, {1, 2});
    circuit.Add(reference, {0});

    const Core::OperatingPointRun run = Core::RunOperatingPoint(circuit);
    REQUIRE(run.Result.has_value());
    const bool expected = type == Core::ComponentType::NOT ? !input_high : input_high;
    CHECK_THAT(run.Result->NodeVoltages[2], Catch::Matchers::WithinAbs(expected ? 5.0 : 0.0, 0.05));
}

TEST_CASE("A logic input left unconnected reads as 0", "[simulator][digital]") {
    SKIP_WITHOUT_DIGITAL_MODELS();
    Core::VoltageSource input;
    input.SetName("Vin1");
    input.SetValue(5.0);
    Core::LogicPart or_gate(Core::ComponentType::OR);
    or_gate.SetName("U1");
    Core::LogicPart and_gate(Core::ComponentType::AND);
    and_gate.SetName("U2");
    const Core::Ground reference;
    // Input A of both gates on node 1 at 5 V, input B of each alone on nodes 2 and 3; outputs on nodes 4 and 5
    Core::Circuit circuit;
    circuit.Add(input, {1, 0});
    circuit.Add(or_gate, {1, 2, 4});
    circuit.Add(and_gate, {1, 3, 5});
    circuit.Add(reference, {0});

    const Core::OperatingPointRun run = Core::RunOperatingPoint(circuit);
    REQUIRE(run.Result.has_value());
    CHECK_THAT(run.Result->NodeVoltages[4], Catch::Matchers::WithinAbs(5.0, 0.05));
    CHECK_THAT(run.Result->NodeVoltages[5], Catch::Matchers::WithinAbs(0.0, 0.05));
}

TEST_CASE("A gate output carries the current of its load through its output resistance", "[simulator][digital]") {
    SKIP_WITHOUT_DIGITAL_MODELS();
    Core::VoltageSource input;
    input.SetName("Vin1");
    input.SetValue(5.0);
    Core::LogicPart buffer(Core::ComponentType::BUFFER);
    buffer.SetName("U1");
    Core::Resistor load;
    load.SetName("R1");
    load.SetValue(950.0);
    const Core::Ground reference;
    Core::Circuit circuit;
    circuit.Add(input, {1, 0});
    circuit.Add(buffer, {1, 2});
    circuit.Add(load, {2, 0});
    circuit.Add(reference, {0});

    const Core::OperatingPointRun run = Core::RunOperatingPoint(circuit);
    REQUIRE(run.Result.has_value());
    // 5 V across 50 Ohm of the output and 950 Ohm of the load
    CHECK_THAT(run.Result->NodeVoltages[2], Catch::Matchers::WithinAbs(4.75, 0.01));
    // Positive into the output terminal, so a part that drives its load reports a negative current
    CHECK_THAT(FindCurrent(run.Result->Currents, "U1").Current, Catch::Matchers::WithinAbs(-5e-3, 1e-5));
}

namespace {

/**
 * @struct  ClockedFlipFlop
 * @brief   A flip-flop of any kind with its clock on node 2 and its outputs on nodes 5 (Q) and 6 (QN).
 * @details The clock is a 1 ms pulse train with its first rising edge at 0.25 ms. The other inputs are on nodes 1, 3
 *          and 4, driven by what the test adds; for a type with two data inputs the second data input is on node 7.
 */
struct ClockedFlipFlop {
    Core::VoltageSource Clock;
    Core::LogicPart FlipFlop;
    Core::Ground Reference;
    Core::Circuit Circuit;

    explicit ClockedFlipFlop(const Core::ComponentType type, std::vector<int> terminals) : FlipFlop(type) {
        Clock.SetName("Vin1");
        Clock.SetSourceType(Core::VoltageSource::SourceType::PULSE);
        Clock.SetPulse({.Low = 0.0,
                        .High = 5.0,
                        .Delay = 0.25e-3,
                        .RiseTime = 1e-6,
                        .FallTime = 1e-6,
                        .Width = 0.5e-3,
                        .Period = 1e-3});
        FlipFlop.SetName("U1");
        Circuit.Add(Clock, {2, 0});
        Circuit.Add(FlipFlop, std::move(terminals));
        Circuit.Add(Reference, {0});
    }
};

std::size_t CountRisingEdges(const std::vector<double> &values, const double threshold) {
    std::size_t edges = 0;
    for (std::size_t index = 1; index < values.size(); ++index) {
        if (values[index - 1] < threshold && values[index] >= threshold) {
            ++edges;
        }
    }
    return edges;
}

double ValueAt(const Core::Transient &transient, const std::vector<double> &values, const double time) {
    const auto index =
        static_cast<std::size_t>(std::ranges::lower_bound(transient.Times, time) - transient.Times.begin());
    return values[index];
}

} // namespace

TEST_CASE("A D flip-flop takes its input only on the rising edge of the clock", "[simulator][digital]") {
    SKIP_WITHOUT_DIGITAL_MODELS();
    Core::VoltageSource data;
    data.SetName("Vin2");
    data.SetValue(5.0);
    ClockedFlipFlop circuit(Core::ComponentType::D_FLIP_FLOP, {1, 2, 3, 4, 5, 6});
    circuit.Circuit.Add(data, {1, 0});

    const Core::TransientRun run = Core::RunTransient(circuit.Circuit, {.StopTime = 1e-3, .TimeStep = 5e-6});
    REQUIRE(run.Result.has_value());
    const Core::Transient &transient = *run.Result;
    // D is high from the start, but Q waits for the edge, and QN is its opposite
    CHECK_THAT(ValueAt(transient, transient.NodeVoltages[5], 0.2e-3), Catch::Matchers::WithinAbs(0.0, 0.05));
    CHECK_THAT(ValueAt(transient, transient.NodeVoltages[6], 0.2e-3), Catch::Matchers::WithinAbs(5.0, 0.05));
    CHECK_THAT(ValueAt(transient, transient.NodeVoltages[5], 0.4e-3), Catch::Matchers::WithinAbs(5.0, 0.05));
    CHECK_THAT(ValueAt(transient, transient.NodeVoltages[6], 0.4e-3), Catch::Matchers::WithinAbs(0.0, 0.05));
}

TEST_CASE("A D flip-flop with QN fed back divides the clock by two", "[simulator][digital]") {
    SKIP_WITHOUT_DIGITAL_MODELS();
    ClockedFlipFlop circuit(Core::ComponentType::D_FLIP_FLOP, {1, 2, 3, 4, 5, 1});

    const Core::TransientRun run = Core::RunTransient(circuit.Circuit, {.StopTime = 8e-3, .TimeStep = 5e-6});
    REQUIRE(run.Result.has_value());
    const Core::Transient &transient = *run.Result;
    // Eight rising edges of the clock, so four of Q
    CHECK(CountRisingEdges(transient.NodeVoltages[2], 2.5) == 8);
    CHECK(CountRisingEdges(transient.NodeVoltages[5], 2.5) == 4);
}

TEST_CASE("The set and reset inputs of a flip-flop act without the clock", "[simulator][digital]") {
    SKIP_WITHOUT_DIGITAL_MODELS();
    Core::VoltageSource set;
    set.SetName("Vin2");
    set.SetSourceType(Core::VoltageSource::SourceType::PULSE);
    // SET high from 0.1 ms to 0.3 ms, before the first edge of the clock
    set.SetPulse({.Low = 0.0,
                  .High = 5.0,
                  .Delay = 0.1e-3,
                  .RiseTime = 1e-6,
                  .FallTime = 1e-6,
                  .Width = 0.2e-3,
                  .Period = 10e-3});
    ClockedFlipFlop circuit(Core::ComponentType::D_FLIP_FLOP, {1, 2, 3, 4, 5, 6});
    circuit.Circuit.Add(set, {3, 0});

    const Core::TransientRun run = Core::RunTransient(circuit.Circuit, {.StopTime = 0.6e-3, .TimeStep = 5e-6});
    REQUIRE(run.Result.has_value());
    const Core::Transient &transient = *run.Result;
    CHECK_THAT(ValueAt(transient, transient.NodeVoltages[5], 0.05e-3), Catch::Matchers::WithinAbs(0.0, 0.05));
    CHECK_THAT(ValueAt(transient, transient.NodeVoltages[5], 0.2e-3), Catch::Matchers::WithinAbs(5.0, 0.05));
    // D is low, so Q falls back at the first edge of the clock after SET let go
    CHECK_THAT(ValueAt(transient, transient.NodeVoltages[5], 0.5e-3), Catch::Matchers::WithinAbs(5.0, 0.05));
}

TEST_CASE("A T flip-flop toggles at each clock edge while T is high", "[simulator][digital]") {
    SKIP_WITHOUT_DIGITAL_MODELS();
    Core::VoltageSource toggle;
    toggle.SetName("Vin2");
    toggle.SetValue(5.0);
    ClockedFlipFlop circuit(Core::ComponentType::T_FLIP_FLOP, {1, 2, 3, 4, 5, 6});
    circuit.Circuit.Add(toggle, {1, 0});

    const Core::TransientRun run = Core::RunTransient(circuit.Circuit, {.StopTime = 8e-3, .TimeStep = 5e-6});
    REQUIRE(run.Result.has_value());
    CHECK(CountRisingEdges(run.Result->NodeVoltages[5], 2.5) == 4);
}

TEST_CASE("A JK flip-flop with J and K high toggles, and with J high alone it sets", "[simulator][digital]") {
    SKIP_WITHOUT_DIGITAL_MODELS();
    Core::VoltageSource high;
    high.SetName("Vin2");
    high.SetValue(5.0);
    SECTION("J and K high") {
        ClockedFlipFlop circuit(Core::ComponentType::JK_FLIP_FLOP, {1, 7, 2, 3, 4, 5, 6});
        circuit.Circuit.Add(high, {1, 0});
        Core::VoltageSource second;
        second.SetName("Vin3");
        second.SetValue(5.0);
        circuit.Circuit.Add(second, {7, 0});
        const Core::TransientRun run = Core::RunTransient(circuit.Circuit, {.StopTime = 8e-3, .TimeStep = 5e-6});
        REQUIRE(run.Result.has_value());
        CHECK(CountRisingEdges(run.Result->NodeVoltages[5], 2.5) == 4);
    }
    SECTION("J high alone") {
        ClockedFlipFlop circuit(Core::ComponentType::JK_FLIP_FLOP, {1, 7, 2, 3, 4, 5, 6});
        circuit.Circuit.Add(high, {1, 0});
        const Core::TransientRun run = Core::RunTransient(circuit.Circuit, {.StopTime = 3.5e-3, .TimeStep = 5e-6});
        REQUIRE(run.Result.has_value());
        CHECK_THAT(run.Result->NodeVoltages[5].back(), Catch::Matchers::WithinAbs(5.0, 0.05));
        CHECK(CountRisingEdges(run.Result->NodeVoltages[5], 2.5) == 1);
    }
}

TEST_CASE("An SR flip-flop sets with S, and resets with R, on the clock edge", "[simulator][digital]") {
    SKIP_WITHOUT_DIGITAL_MODELS();
    Core::VoltageSource set_input;
    set_input.SetName("Vin2");
    set_input.SetSourceType(Core::VoltageSource::SourceType::PULSE);
    // S high for the first clock edge only, R high for the third
    set_input.SetPulse(
        {.Low = 0.0, .High = 5.0, .Delay = 0.0, .RiseTime = 1e-6, .FallTime = 1e-6, .Width = 1.0e-3, .Period = 10e-3});
    Core::VoltageSource reset_input;
    reset_input.SetName("Vin3");
    reset_input.SetSourceType(Core::VoltageSource::SourceType::PULSE);
    reset_input.SetPulse({.Low = 0.0,
                          .High = 5.0,
                          .Delay = 2.0e-3,
                          .RiseTime = 1e-6,
                          .FallTime = 1e-6,
                          .Width = 1.0e-3,
                          .Period = 10e-3});
    ClockedFlipFlop circuit(Core::ComponentType::SR_FLIP_FLOP, {1, 7, 2, 3, 4, 5, 6});
    circuit.Circuit.Add(set_input, {1, 0});
    circuit.Circuit.Add(reset_input, {7, 0});

    const Core::TransientRun run = Core::RunTransient(circuit.Circuit, {.StopTime = 4e-3, .TimeStep = 5e-6});
    REQUIRE(run.Result.has_value());
    const Core::Transient &transient = *run.Result;
    CHECK_THAT(ValueAt(transient, transient.NodeVoltages[5], 0.2e-3), Catch::Matchers::WithinAbs(0.0, 0.05));
    CHECK_THAT(ValueAt(transient, transient.NodeVoltages[5], 0.5e-3), Catch::Matchers::WithinAbs(5.0, 0.05));
    // Both inputs low at the second edge: Q holds
    CHECK_THAT(ValueAt(transient, transient.NodeVoltages[5], 1.5e-3), Catch::Matchers::WithinAbs(5.0, 0.05));
    // R high at the third edge, at 2.25 ms
    CHECK_THAT(ValueAt(transient, transient.NodeVoltages[5], 2.4e-3), Catch::Matchers::WithinAbs(0.0, 0.05));
}

TEST_CASE("A DC sweep of an inverter input flips its output through an undefined band between 30% and 70% of the level",
          "[simulator][digital]") {
    SKIP_WITHOUT_DIGITAL_MODELS();
    Core::VoltageSource input;
    input.SetName("Vin1");
    Core::LogicPart inverter(Core::ComponentType::NOT);
    inverter.SetName("U1");
    const Core::Ground reference;
    Core::Circuit circuit;
    circuit.Add(input, {1, 0});
    circuit.Add(inverter, {1, 2});
    circuit.Add(reference, {0});

    const Core::DCSweepRun run =
        Core::RunDCSweep(circuit, {.Swept = {.Source = "Vin1", .Start = 0.0, .Stop = 5.0, .Step = 0.1}});
    REQUIRE(run.Result.has_value());
    const Core::DCSweepCurve &curve = run.Result->Curves.front();
    const std::vector<double> &output = curve.NodeVoltages[2];
    REQUIRE(output.size() == 51);
    CHECK_THAT(output.front(), Catch::Matchers::WithinAbs(5.0, 0.05));
    // 1 V reads as 0 and 4 V as 1; between 30% and 70% of the level the input is undefined, and so is the output
    CHECK_THAT(output[10], Catch::Matchers::WithinAbs(5.0, 0.05));
    CHECK_THAT(output[30], Catch::Matchers::WithinAbs(2.5, 0.05));
    CHECK_THAT(output[40], Catch::Matchers::WithinAbs(0.0, 0.05));
}

TEST_CASE("An AC sweep runs with a gate in the circuit", "[simulator][digital]") {
    SKIP_WITHOUT_DIGITAL_MODELS();
    Core::VoltageSource input;
    input.SetName("Vin1");
    input.SetSourceType(Core::VoltageSource::SourceType::AC);
    Core::LogicPart buffer(Core::ComponentType::BUFFER);
    buffer.SetName("U1");
    Core::Resistor load;
    load.SetName("R1");
    load.SetValue(1e3);
    const Core::Ground reference;
    Core::Circuit circuit;
    circuit.Add(input, {1, 0});
    circuit.Add(buffer, {1, 2});
    circuit.Add(load, {2, 0});
    circuit.Add(reference, {0});

    const Core::ACSweepRun run = Core::RunACSweep(circuit, {.StartFrequency = 10.0, .StopFrequency = 1e3});
    CHECK(run.Result.has_value());
}

namespace {

// Thermal noise density of a resistor at the 27 C ngspice simulates at, in V/sqrt(Hz)
double ThermalNoise(const double resistance) {
    constexpr double BOLTZMANN = 1.380649e-23;
    constexpr double TEMPERATURE = 300.15;
    return std::sqrt(4.0 * BOLTZMANN * TEMPERATURE * resistance);
}

const Core::NoiseContribution *FindContribution(const Core::Noise &noise, const std::string &name) {
    const auto found = std::ranges::find(noise.Contributions, name, &Core::NoiseContribution::Name);
    return found != noise.Contributions.end() ? &*found : nullptr;
}

} // namespace

TEST_CASE("The noise of an RC divider is the thermal noise of its resistor, shaped by the capacitor", "[simulator]") {
    LowPass low_pass;
    const Core::NoiseRun run = Core::RunNoise(
        low_pass.Circuit,
        {.OutputNode = 2, .InputSource = "Vin1", .StartFrequency = 1.0, .StopFrequency = 10.0, .PointsPerDecade = 1});
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    const Core::Noise &noise = *run.Result;
    REQUIRE(noise.Frequencies.size() == 2);
    CHECK(noise.InputSource == "Vin1");
    CHECK(noise.InputUnit == "V");
    // Far below the 159 Hz corner the capacitor is open, so the output sees all of R1 and the gain is 1
    CHECK_THAT(noise.OutputDensities[0], Catch::Matchers::WithinRel(ThermalNoise(1e3), 1e-3));
    CHECK_THAT(noise.InputDensities[0], Catch::Matchers::WithinRel(ThermalNoise(1e3), 1e-3));
    CHECK_THAT(noise.OutputTotal, Catch::Matchers::WithinRel(ThermalNoise(1e3) * 3.0, 2e-2));
    REQUIRE(noise.Contributions.size() == 1);
    CHECK(noise.Contributions[0].Name == "R1");
    CHECK_THAT(noise.Contributions[0].OutputTotal, Catch::Matchers::WithinRel(noise.OutputTotal, 1e-6));
}

TEST_CASE("The noise of an amplifier lists the share of each part, and the ideal op-amp adds next to none",
          "[simulator]") {
    InvertingAmplifier amplifier(0.0);
    amplifier.Input.SetSourceType(Core::VoltageSource::SourceType::AC);
    const Core::NoiseSettings settings{
        .OutputNode = 3, .InputSource = "Vin1", .StartFrequency = 10.0, .StopFrequency = 100.0, .PointsPerDecade = 1};
    const Core::NoiseRun run = Core::RunNoise(amplifier.Circuit, settings);
    WARN(amplifier.Circuit.ToSpiceNetlist(".options savecurrents", true));
    for (const auto &c : run.Result->Contributions) {
        WARN(c.Name << " " << c.OutputDensities[0]);
    }
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    const Core::Noise &noise = *run.Result;
    // R1 is amplified by the gain of 10, while R2 appears as it is
    const double from_input = ThermalNoise(1e3) * 10.0;
    const double from_feedback = ThermalNoise(10e3);
    CHECK_THAT(noise.OutputDensities[0], Catch::Matchers::WithinRel(std::hypot(from_input, from_feedback), 1e-3));
    CHECK_THAT(noise.InputDensities[0], Catch::Matchers::WithinRel(std::hypot(from_input, from_feedback) / 10.0, 1e-3));
    REQUIRE(noise.Contributions.size() == 3);
    CHECK(noise.Contributions[0].Name == "R1");
    CHECK(noise.Contributions[1].Name == "R2");
    CHECK_THAT(noise.Contributions[0].OutputDensities[0], Catch::Matchers::WithinRel(from_input, 1e-3));
    // Only the leakage of its clamp diodes, a thousandth of the rest
    const Core::NoiseContribution *ideal = FindContribution(noise, "U1");
    REQUIRE(ideal != nullptr);
    CHECK(ideal->OutputTotal < noise.OutputTotal * 1e-3);

    SECTION("a macromodel adds the noise of its own transistors and resistors") {
        amplifier.Amplifier.SetModel(*Core::FindOpAmpModel(Core::ComponentType::OP_AMP, "uA741"));
        const Core::NoiseRun macromodel_run = Core::RunNoise(amplifier.Circuit, settings);
        if (!macromodel_run.Result) {
            FAIL(macromodel_run.Result.error());
        }
        const Core::NoiseContribution *op_amp = FindContribution(*macromodel_run.Result, "U1");
        REQUIRE(op_amp != nullptr);
        CHECK(op_amp->OutputTotal > 0.0);
        CHECK(macromodel_run.Result->OutputTotal > noise.OutputTotal);
    }
}

TEST_CASE("A noise analysis refers its noise to a current source in A/sqrt(Hz)", "[simulator]") {
    Core::CurrentSource source;
    source.SetName("I1");
    source.SetSourceType(Core::CurrentSource::SourceType::AC);
    Core::Resistor load;
    load.SetName("R1");
    Core::Ground reference;
    Core::Circuit circuit;
    circuit.Add(source, {0, 1});
    circuit.Add(load, {1, 0});
    circuit.Add(reference, {0});
    CHECK(Core::GetNoiseInputSources(circuit) == std::vector<std::string>{"I1"});
    const Core::NoiseRun run = Core::RunNoise(
        circuit,
        {.OutputNode = 1, .InputSource = "I1", .StartFrequency = 1e3, .StopFrequency = 10e3, .PointsPerDecade = 1});
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    CHECK(run.Result->InputUnit == "A");
    // The transimpedance is 1k, so the input current noise is the voltage noise over 1k
    CHECK_THAT(run.Result->InputDensities[0], Catch::Matchers::WithinRel(ThermalNoise(1e3) / 1e3, 1e-3));
}

TEST_CASE("RunNoise rejects a missing input source, ground as output and a bad range", "[simulator]") {
    LowPass low_pass;
    const Core::NoiseSettings settings{.OutputNode = 2, .InputSource = "Vin1"};
    CHECK(Core::RunNoise(low_pass.Circuit, settings).Result);

    Core::NoiseSettings no_input = settings;
    no_input.InputSource.clear();
    CHECK_FALSE(Core::RunNoise(low_pass.Circuit, no_input).Result);
    Core::NoiseSettings ground_output = settings;
    ground_output.OutputNode = 0;
    CHECK_FALSE(Core::RunNoise(low_pass.Circuit, ground_output).Result);
    Core::NoiseSettings reversed = settings;
    reversed.StopFrequency = reversed.StartFrequency / 2.0;
    CHECK_FALSE(Core::RunNoise(low_pass.Circuit, reversed).Result);

    // A source that is not set to AC cannot be the input
    low_pass.Source.SetSourceType(Core::VoltageSource::SourceType::DC);
    CHECK(Core::GetNoiseInputSources(low_pass.Circuit).empty());
    const Core::NoiseRun run = Core::RunNoise(low_pass.Circuit, settings);
    REQUIRE_FALSE(run.Result);
    CHECK(run.Result.error().contains("Vin1"));
}

TEST_CASE("A stepped part runs at its step value, in the netlist and in the readings", "[simulator]") {
    LowPass low_pass;
    const Core::ACSweepSettings settings{.StartFrequency = 159.155, .StopFrequency = 1591.55, .PointsPerDecade = 1};
    const Core::ACSweepRun stepped = Core::RunACSweep(low_pass.Circuit, settings, Core::ParameterValue{"R1", 10e3});
    if (!stepped.Result) {
        FAIL(stepped.Result.error());
    }
    // With 10k the corner moves from 159 Hz to 15.9 Hz, so 159 Hz is a decade above it
    CHECK_THAT(stepped.Result->NodeMagnitudesDecibels[2][0], Catch::Matchers::WithinAbs(-20.04, 0.05));
    // The current of R1 is worked out from its value, which must be the stepped one: 1 V over about 10k
    CHECK_THAT(stepped.Result->CurrentMagnitudesDecibels[0].Values[0], Catch::Matchers::WithinAbs(-80.0, 0.1));
    // The part itself keeps its value
    CHECK(low_pass.Series.GetValue() == 1e3);
    CHECK(Core::GetSteppableParts(low_pass.Circuit) == std::vector<std::string>{"R1", "C1"});
}

TEST_CASE("A stepped temperature changes the forward voltage of a diode", "[simulator]") {
    Core::VCC supply;
    supply.SetName("V1");
    supply.SetValue(5.0);
    Core::Resistor series;
    series.SetName("R1");
    Core::Diode diode(Core::ComponentType::DIODE);
    diode.SetName("D1");
    Core::Ground reference;
    Core::Circuit circuit;
    circuit.Add(supply, {1});
    circuit.Add(series, {1, 2});
    circuit.Add(diode, {2, 0});
    circuit.Add(reference, {0});
    const Core::DCSweepSettings settings{.Swept = {.Source = "V1", .Start = 4.0, .Stop = 5.0, .Step = 1.0}};
    const auto forward_voltage = [&](const double temperature) {
        const Core::DCSweepRun run = Core::RunDCSweep(circuit, settings, Core::ParameterValue{"", temperature});
        REQUIRE(run.Result);
        return run.Result->Curves.front().NodeVoltages[2].back();
    };
    // About -2 mV per degree, as for any silicon junction
    const double cold = forward_voltage(0.0);
    const double hot = forward_voltage(100.0);
    CHECK(cold - hot > 0.1);
    CHECK(cold - hot < 0.3);
}

TEST_CASE("A stepped parameter rejects a part that cannot step and values out of range", "[simulator]") {
    LowPass low_pass;
    const Core::TransientSettings settings{.StopTime = 1e-3, .TimeStep = 1e-5};
    CHECK(Core::RunTransient(low_pass.Circuit, settings, Core::ParameterValue{"C1", 2e-6}).Result);
    CHECK_FALSE(Core::RunTransient(low_pass.Circuit, settings, Core::ParameterValue{"Vin1", 2.0}).Result);
    CHECK_FALSE(Core::RunTransient(low_pass.Circuit, settings, Core::ParameterValue{"R1", 0.0}).Result);
    CHECK_FALSE(Core::RunTransient(low_pass.Circuit, settings, Core::ParameterValue{"", -300.0}).Result);
}

TEST_CASE("A noise analysis runs beside logic parts, which add only the noise of their resistors", "[simulator]") {
    SKIP_WITHOUT_DIGITAL_MODELS();
    LowPass low_pass;
    Core::LogicPart gate(Core::ComponentType::NOT);
    gate.SetName("U1");
    low_pass.Circuit.Add(gate, {2, 3});
    const Core::NoiseRun run = Core::RunNoise(
        low_pass.Circuit,
        {.OutputNode = 2, .InputSource = "Vin1", .StartFrequency = 1.0, .StopFrequency = 10.0, .PointsPerDecade = 1});
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    // R1 still makes nearly all the noise; the gate adds that of its input pull-down, a thousandth of it
    CHECK_THAT(run.Result->OutputDensities[0], Catch::Matchers::WithinRel(ThermalNoise(1e3), 1e-2));
    REQUIRE_FALSE(run.Result->Contributions.empty());
    CHECK(run.Result->Contributions.front().Name == "R1");
}

namespace {

/**
 * @struct  ResistiveDivider
 * @brief   A DC voltage source on node 1 feeding 1k and 2k resistors in series to ground, with the middle on node 2.
 */
struct ResistiveDivider {
    Core::VoltageSource Source;
    Core::Resistor Top;
    Core::Resistor Bottom;
    Core::Ground Reference;
    Core::Circuit Circuit;

    ResistiveDivider() {
        Source.SetName("V1");
        Source.SetValue(5.0);
        Top.SetName("R1");
        Top.SetValue(1e3);
        Bottom.SetName("R2");
        Bottom.SetValue(2e3);
        Circuit.Add(Source, {1, 0});
        Circuit.Add(Top, {1, 2});
        Circuit.Add(Bottom, {2, 0});
        Circuit.Add(Reference, {0});
    }
};

} // namespace

TEST_CASE("The transfer function of a voltage divider matches its closed-form gain and resistances", "[simulator]") {
    ResistiveDivider divider;
    // A transfer function drives the circuit from an independent source of any waveform, not only an AC one
    CHECK(Core::GetTransferFunctionInputSources(divider.Circuit) == std::vector<std::string>{"V1"});
    const Core::TransferFunctionRun run =
        Core::RunTransferFunction(divider.Circuit, {.OutputNode = 2, .InputSource = "V1"});
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    const Core::TransferFunction &transfer = *run.Result;
    CHECK(transfer.InputSource == "V1");
    CHECK(transfer.InputUnit == "V");
    // The divider: gain R2/(R1+R2), input resistance R1+R2, output resistance R1 parallel R2
    CHECK_THAT(transfer.Gain, Catch::Matchers::WithinRel(2.0 / 3.0, 1e-6));
    CHECK_THAT(transfer.InputImpedance, Catch::Matchers::WithinRel(3e3, 1e-6));
    CHECK_THAT(transfer.OutputImpedance, Catch::Matchers::WithinRel(1e3 * 2e3 / (1e3 + 2e3), 1e-6));
}

TEST_CASE("A VCC supply rail is a transfer function input", "[simulator]") {
    // The Divider helper is a 10 V VCC feeding 1k and 3k resistors, with the middle on node 2
    Divider divider;
    CHECK(Core::GetTransferFunctionInputSources(divider.Circuit) == std::vector<std::string>{"V1"});
    const Core::TransferFunctionRun run =
        Core::RunTransferFunction(divider.Circuit, {.OutputNode = 2, .InputSource = "V1"});
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    const Core::TransferFunction &transfer = *run.Result;
    CHECK(transfer.InputUnit == "V");
    // The 1k/3k divider: gain 3k/(1k+3k), input 1k+3k, output 1k parallel 3k
    CHECK_THAT(transfer.Gain, Catch::Matchers::WithinRel(3e3 / (1e3 + 3e3), 1e-6));
    CHECK_THAT(transfer.InputImpedance, Catch::Matchers::WithinRel(4e3, 1e-6));
    CHECK_THAT(transfer.OutputImpedance, Catch::Matchers::WithinRel(1e3 * 3e3 / (1e3 + 3e3), 1e-6));
}

TEST_CASE("The transfer function from a current source is a transimpedance in V/A", "[simulator]") {
    Core::CurrentSource source;
    source.SetName("I1");
    source.SetValue(1e-3);
    Core::Resistor load;
    load.SetName("R1");
    load.SetValue(1e3);
    Core::Ground reference;
    Core::Circuit circuit;
    circuit.Add(source, {0, 1});
    circuit.Add(load, {1, 0});
    circuit.Add(reference, {0});
    CHECK(Core::GetTransferFunctionInputSources(circuit) == std::vector<std::string>{"I1"});
    const Core::TransferFunctionRun run = Core::RunTransferFunction(circuit, {.OutputNode = 1, .InputSource = "I1"});
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    const Core::TransferFunction &transfer = *run.Result;
    CHECK(transfer.InputUnit == "A");
    // A current I into R1 makes V = I*R1, so the gain is R1 (V/A) and both impedances are R1
    CHECK_THAT(transfer.Gain, Catch::Matchers::WithinRel(1e3, 1e-6));
    CHECK_THAT(transfer.InputImpedance, Catch::Matchers::WithinRel(1e3, 1e-6));
    CHECK_THAT(transfer.OutputImpedance, Catch::Matchers::WithinRel(1e3, 1e-6));
}

TEST_CASE("RunTransferFunction rejects a missing input source and an unknown output", "[simulator]") {
    ResistiveDivider divider;
    CHECK(Core::RunTransferFunction(divider.Circuit, {.OutputNode = 2, .InputSource = "V1"}).Result);

    // An input source that is not in the circuit, and one that cannot drive it, are named back
    const Core::TransferFunctionRun no_input =
        Core::RunTransferFunction(divider.Circuit, {.OutputNode = 2, .InputSource = "V9"});
    REQUIRE_FALSE(no_input.Result);
    CHECK(no_input.Result.error().contains("V9"));

    // Ground and a node past the circuit are not outputs
    CHECK_FALSE(Core::RunTransferFunction(divider.Circuit, {.OutputNode = 0, .InputSource = "V1"}).Result);
    CHECK_FALSE(Core::RunTransferFunction(divider.Circuit, {.OutputNode = 42, .InputSource = "V1"}).Result);
}

TEST_CASE("The impedances of an ideal source are finite, so the views can show them sensibly", "[simulator]") {
    Core::VoltageSource source;
    source.SetName("V1");
    source.SetValue(5.0);
    Core::Ground reference;
    Core::Circuit circuit;
    circuit.Add(source, {1, 0});
    circuit.Add(reference, {0});
    const Core::TransferFunctionRun run = Core::RunTransferFunction(circuit, {.OutputNode = 1, .InputSource = "V1"});
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    // The source drives an open load, which ngspice reports around 1e20 ohm; it stays finite so it reads as an open
    // circuit rather than nan or inf
    CHECK(std::isfinite(run.Result->InputImpedance));
    CHECK(run.Result->InputImpedance > 1e15);
    // The output sits on the ideal source, which holds it at 0 ohm
    CHECK(run.Result->OutputImpedance == 0.0);
    CHECK_THAT(run.Result->Gain, Catch::Matchers::WithinRel(1.0, 1e-9));
}

TEST_CASE("A clock is high for the share of the period its duty cycle gives", "[simulator]") {
    Core::Clock clock;
    clock.SetName("V1");
    clock.SetParameters({.Frequency = 1e3, .DutyCycle = 0.25, .LogicLevel = 5.0, .StartDelay = 0.0});
    Core::Resistor load;
    load.SetName("R1");
    load.SetValue(1e3);
    const Core::Ground reference;
    Core::Circuit circuit;
    circuit.Add(clock, {1});
    circuit.Add(load, {1, 0});
    circuit.Add(reference, {0});

    // Four 1 ms periods sampled every 0.1 us; the short edges contribute well under one percent
    const Core::TransientRun run = Core::RunTransient(circuit, {.StopTime = 4e-3, .TimeStep = 1e-7});
    REQUIRE(run.Result.has_value());
    const std::vector<double> &voltage = run.Result->NodeVoltages[1];
    std::size_t high_samples = 0;
    for (const double value : voltage) {
        if (value > 2.5) {
            ++high_samples;
        }
    }
    const double high_fraction = static_cast<double>(high_samples) / static_cast<double>(voltage.size());
    CHECK_THAT(high_fraction, Catch::Matchers::WithinAbs(0.25, 0.02));
}

TEST_CASE("A clock start delay holds it low until the first rising edge", "[simulator]") {
    Core::Clock clock;
    clock.SetName("V1");
    // 50% at 1 kHz, but quiet for the first 2 ms
    clock.SetParameters({.Frequency = 1e3, .DutyCycle = 0.5, .LogicLevel = 5.0, .StartDelay = 2e-3});
    const Core::Ground reference;
    Core::Circuit circuit;
    circuit.Add(clock, {1});
    circuit.Add(reference, {0});

    const Core::TransientRun run = Core::RunTransient(circuit, {.StopTime = 4e-3, .TimeStep = 1e-6});
    REQUIRE(run.Result.has_value());
    const Core::Transient &transient = *run.Result;
    const std::vector<double> &voltage = transient.NodeVoltages[1];
    const auto at = [&transient, &voltage](const double time) {
        return voltage[static_cast<std::size_t>(std::ranges::lower_bound(transient.Times, time) -
                                                transient.Times.begin())];
    };
    // Low through the delay, high in the first pulse at 2.25 ms, low again at 2.75 ms
    CHECK_THAT(at(1.0e-3), Catch::Matchers::WithinAbs(0.0, 0.05));
    CHECK_THAT(at(1.99e-3), Catch::Matchers::WithinAbs(0.0, 0.05));
    CHECK_THAT(at(2.25e-3), Catch::Matchers::WithinAbs(5.0, 0.05));
    CHECK_THAT(at(2.75e-3), Catch::Matchers::WithinAbs(0.0, 0.05));
    CHECK_THAT(at(3.25e-3), Catch::Matchers::WithinAbs(5.0, 0.05));
}

TEST_CASE("A clock into a D flip-flop divides it by two", "[simulator][digital]") {
    SKIP_WITHOUT_DIGITAL_MODELS();
    Core::Clock clock;
    clock.SetName("V1");
    // The first rising edge at 0.25 ms, the same last rising edge the other flip-flop tests use
    clock.SetParameters({.Frequency = 1e3, .DutyCycle = 0.5, .LogicLevel = 5.0, .StartDelay = 0.25e-3});
    Core::LogicPart flip_flop(Core::ComponentType::D_FLIP_FLOP);
    flip_flop.SetName("U1");
    Core::Circuit circuit;
    // QN on node 1 feeds D on node 1; CLK on node 2, SET and RESET on nodes 3 and 4, Q on node 5. The clock returns
    // to node 0 by itself, so the circuit needs no Ground part
    circuit.Add(clock, {2});
    circuit.Add(flip_flop, {1, 2, 3, 4, 5, 1});
    REQUIRE_FALSE(circuit.HasGround());

    const Core::TransientRun run = Core::RunTransient(circuit, {.StopTime = 4e-3, .TimeStep = 5e-6});
    REQUIRE(run.Result.has_value());
    const Core::Transient &transient = *run.Result;
    // Four rising edges from 0.25 ms to 3.25 ms, each toggling Q, so two rising edges of Q
    CHECK(CountRisingEdges(transient.NodeVoltages[2], 2.5) == 4);
    CHECK(CountRisingEdges(transient.NodeVoltages[5], 2.5) == 2);
}

TEST_CASE("A logic input drives a gate to the output its truth table gives", "[simulator][digital]") {
    SKIP_WITHOUT_DIGITAL_MODELS();
    Core::LogicInput input;
    input.SetName("V1");
    Core::LogicPart gate(Core::ComponentType::OR);
    gate.SetName("U1");
    const Core::Ground reference;
    Core::Circuit circuit;
    // A on node 1, B left alone on node 2 (it reads 0), Y on node 3
    circuit.Add(input, {1});
    circuit.Add(gate, {1, 2, 3});
    circuit.Add(reference, {0});

    // Low with B low gives 0, High with B low gives 1
    const Core::OperatingPointRun low = Core::RunOperatingPoint(circuit);
    REQUIRE(low.Result.has_value());
    CHECK_THAT(low.Result->NodeVoltages[3], Catch::Matchers::WithinAbs(0.0, 0.05));

    input.SetHigh(true);
    const Core::OperatingPointRun high = Core::RunOperatingPoint(circuit);
    REQUIRE(high.Result.has_value());
    CHECK_THAT(high.Result->NodeVoltages[3], Catch::Matchers::WithinAbs(5.0, 0.05));
}

TEST_CASE("A circuit of only digital parts runs without a ground", "[simulator][digital]") {
    SKIP_WITHOUT_DIGITAL_MODELS();
    Core::LogicInput input_a;
    input_a.SetName("V1");
    input_a.SetHigh(true);
    Core::LogicInput input_b;
    input_b.SetName("V2");
    Core::LogicPart gate(Core::ComponentType::AND);
    gate.SetName("U1");
    Core::Circuit circuit;
    // A on node 1 and B on node 2, with Y on node 3 and no Ground part, resistor or load anywhere
    circuit.Add(input_a, {1});
    circuit.Add(input_b, {2});
    circuit.Add(gate, {1, 2, 3});
    REQUIRE_FALSE(circuit.HasGround());

    // 1 AND 0 is 0, and 1 AND 1 is 1
    const Core::OperatingPointRun low = Core::RunOperatingPoint(circuit);
    REQUIRE(low.Result.has_value());
    CHECK_THAT(low.Result->NodeVoltages[3], Catch::Matchers::WithinAbs(0.0, 0.05));

    input_b.SetHigh(true);
    const Core::OperatingPointRun high = Core::RunOperatingPoint(circuit);
    REQUIRE(high.Result.has_value());
    CHECK_THAT(high.Result->NodeVoltages[1], Catch::Matchers::WithinAbs(5.0, 0.05));
    CHECK_THAT(high.Result->NodeVoltages[3], Catch::Matchers::WithinAbs(5.0, 0.05));
}

TEST_CASE("A truth table runs every combination of the chosen logic inputs in counting order", "[simulator][digital]") {
    SKIP_WITHOUT_DIGITAL_MODELS();
    Core::LogicInput input_a;
    input_a.SetName("V1");
    input_a.SetHigh(true);
    Core::LogicInput input_b;
    input_b.SetName("V2");
    Core::LogicPart gate(Core::ComponentType::AND);
    gate.SetName("U1");
    Core::Circuit circuit;
    // A on node 1, B on node 2, Y on node 3
    circuit.Add(input_a, {1});
    circuit.Add(input_b, {2});
    circuit.Add(gate, {1, 2, 3});

    CHECK(Core::GetTruthTableInputs(circuit) == std::vector<std::string>{"V1", "V2"});
    const Core::TruthTableRun run = Core::RunTruthTable(circuit, {.Inputs = {"V1", "V2"}, .OutputNodes = {3}});
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    const Core::TruthTable &table = *run.Result;
    CHECK(table.InputNames == std::vector<std::string>{"V1", "V2"});
    CHECK(table.InputLevels == std::vector<double>{5.0, 5.0});
    REQUIRE(table.Rows.size() == 4);
    // The first input is the most significant bit, so the rows read 00, 01, 10 and 11, and an AND is high only on 11
    constexpr std::array<std::array<bool, 2>, 4> COMBINATIONS = {
        {{false, false}, {false, true}, {true, false}, {true, true}}};
    for (std::size_t row = 0; row < table.Rows.size(); ++row) {
        CHECK(table.Rows[row].InputsHigh == std::vector<bool>{COMBINATIONS[row][0], COMBINATIONS[row][1]});
        REQUIRE(table.Rows[row].HasOperatingPoint());
        const double expected = COMBINATIONS[row][0] && COMBINATIONS[row][1] ? 5.0 : 0.0;
        CHECK_THAT(table.Rows[row].OutputVoltages[0], Catch::Matchers::WithinAbs(expected, 0.05));
    }
    CHECK(table.Warnings.empty());
    // The circuit itself is left as it was
    CHECK(input_a.IsHigh());
    CHECK_FALSE(input_b.IsHigh());
}

TEST_CASE("A truth table reads several outputs, as a half adder gives its sum and carry", "[simulator][digital]") {
    SKIP_WITHOUT_DIGITAL_MODELS();
    Core::LogicInput input_a;
    input_a.SetName("V1");
    Core::LogicInput input_b;
    input_b.SetName("V2");
    Core::LogicPart sum_gate(Core::ComponentType::XOR);
    sum_gate.SetName("U1");
    Core::LogicPart carry_gate(Core::ComponentType::AND);
    carry_gate.SetName("U2");
    Core::Circuit circuit;
    // A on node 1, B on node 2, sum on node 3, carry on node 4
    circuit.Add(input_a, {1});
    circuit.Add(input_b, {2});
    circuit.Add(sum_gate, {1, 2, 3});
    circuit.Add(carry_gate, {1, 2, 4});

    const Core::TruthTableRun run = Core::RunTruthTable(circuit, {.Inputs = {"V1", "V2"}, .OutputNodes = {3, 4}});
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    REQUIRE(run.Result->Rows.size() == 4);
    // Sum is A xor B and carry is A and B: 00 gives 0 0, 01 gives 1 0, 10 gives 1 0 and 11 gives 0 1
    constexpr std::array<std::array<double, 2>, 4> EXPECTED = {{{0.0, 0.0}, {5.0, 0.0}, {5.0, 0.0}, {0.0, 5.0}}};
    for (std::size_t row = 0; row < 4; ++row) {
        REQUIRE(run.Result->Rows[row].OutputVoltages.size() == 2);
        CHECK_THAT(run.Result->Rows[row].OutputVoltages[0], Catch::Matchers::WithinAbs(EXPECTED[row][0], 0.05));
        CHECK_THAT(run.Result->Rows[row].OutputVoltages[1], Catch::Matchers::WithinAbs(EXPECTED[row][1], 0.05));
    }
}

TEST_CASE("A truth table leaves a combination without an operating point empty and warns", "[simulator][digital]") {
    SKIP_WITHOUT_DIGITAL_MODELS();
    Core::LogicInput set_bar;
    set_bar.SetName("V1");
    Core::LogicInput reset_bar;
    reset_bar.SetName("V2");
    Core::LogicPart gate_q(Core::ComponentType::NAND);
    gate_q.SetName("U1");
    Core::LogicPart gate_qn(Core::ComponentType::NAND);
    gate_qn.SetName("U2");
    Core::Circuit circuit;
    // U1: A = S_BAR (1), B = QN (3), Y = Q (2); U2: A = R_BAR (4), B = Q (2), Y = QN (3)
    circuit.Add(set_bar, {1});
    circuit.Add(reset_bar, {4});
    circuit.Add(gate_q, {1, 3, 2});
    circuit.Add(gate_qn, {4, 2, 3});

    const Core::TruthTableRun run = Core::RunTruthTable(circuit, {.Inputs = {"V1", "V2"}, .OutputNodes = {2, 3}});
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    const Core::TruthTable &table = *run.Result;
    REQUIRE(table.Rows.size() == 4);
    // S_BAR low sets Q and R_BAR low resets it; both low drives both outputs high
    CHECK_THAT(table.Rows[0].OutputVoltages[0], Catch::Matchers::WithinAbs(5.0, 0.05));
    CHECK_THAT(table.Rows[0].OutputVoltages[1], Catch::Matchers::WithinAbs(5.0, 0.05));
    CHECK_THAT(table.Rows[1].OutputVoltages[0], Catch::Matchers::WithinAbs(5.0, 0.05));
    CHECK_THAT(table.Rows[1].OutputVoltages[1], Catch::Matchers::WithinAbs(0.0, 0.05));
    CHECK_THAT(table.Rows[2].OutputVoltages[0], Catch::Matchers::WithinAbs(0.0, 0.05));
    CHECK_THAT(table.Rows[2].OutputVoltages[1], Catch::Matchers::WithinAbs(5.0, 0.05));
    // Both high holds the state, so it depends on history and has no operating point
    CHECK_FALSE(table.Rows[3].HasOperatingPoint());
    REQUIRE(table.Warnings.size() == 1);
    CHECK(table.Warnings.front().contains("1 of 4"));
}

TEST_CASE("RunTruthTable rejects inputs and outputs it cannot use", "[simulator]") {
    Core::LogicInput input;
    input.SetName("V1");
    Core::Resistor load;
    load.SetName("R1");
    load.SetValue(1e3);
    const Core::Ground reference;
    Core::Circuit circuit;
    circuit.Add(input, {1});
    circuit.Add(load, {1, 0});
    circuit.Add(reference, {0});

    const auto message = [&circuit](const Core::TruthTableSettings &settings) {
        const Core::TruthTableRun run = Core::RunTruthTable(circuit, settings);
        REQUIRE_FALSE(run.Result.has_value());
        return run.Result.error();
    };
    CHECK(message({.Inputs = {}, .OutputNodes = {1}}).contains("at least one logic input"));
    CHECK(message({.Inputs = {"V9"}, .OutputNodes = {1}}).contains("V9"));
    // A resistor is not a logic input, and an input cannot be listed twice
    CHECK(message({.Inputs = {"R1"}, .OutputNodes = {1}}).contains("R1"));
    CHECK(message({.Inputs = {"V1", "V1"}, .OutputNodes = {1}}).contains("twice"));
    CHECK(message({.Inputs = {"V1"}, .OutputNodes = {}}).contains("at least one node"));
    CHECK(message({.Inputs = {"V1"}, .OutputNodes = {0}}).contains("ground"));
    CHECK(message({.Inputs = {"V1"}, .OutputNodes = {7}}).contains("ground"));
    // More inputs than the limit would take thousands of runs
    const std::vector<std::string> many(Core::MAX_TRUTH_TABLE_INPUTS + 1, "V1");
    CHECK(message({.Inputs = many, .OutputNodes = {1}}).contains("at most"));
}

TEST_CASE("Gates with three inputs follow their truth tables, an XOR giving odd parity", "[simulator][digital]") {
    SKIP_WITHOUT_DIGITAL_MODELS();
    struct GateCase {
        Core::ComponentType Type;
        // Whether the output is high for 0, 1, 2 and 3 high inputs
        std::array<bool, 4> HighForCount;
    };
    constexpr std::array<GateCase, 6> CASES = {{{Core::ComponentType::AND, {false, false, false, true}},
                                                {Core::ComponentType::OR, {false, true, true, true}},
                                                {Core::ComponentType::NAND, {true, true, true, false}},
                                                {Core::ComponentType::NOR, {true, false, false, false}},
                                                {Core::ComponentType::XOR, {false, true, false, true}},
                                                {Core::ComponentType::XNOR, {true, false, true, false}}}};
    for (const GateCase &gate_case : CASES) {
        Core::LogicInput input_a;
        input_a.SetName("V1");
        Core::LogicInput input_b;
        input_b.SetName("V2");
        Core::LogicInput input_c;
        input_c.SetName("V3");
        Core::LogicPart gate(gate_case.Type);
        gate.SetName("U1");
        gate.SetInputCount(3);
        Core::Circuit circuit;
        // The three inputs on nodes 1, 2 and 3 and the output on node 4
        circuit.Add(input_a, {1});
        circuit.Add(input_b, {2});
        circuit.Add(input_c, {3});
        circuit.Add(gate, {1, 2, 3, 4});

        const Core::TruthTableRun run =
            Core::RunTruthTable(circuit, {.Inputs = {"V1", "V2", "V3"}, .OutputNodes = {4}});
        INFO(gate.GetTypeName());
        if (!run.Result) {
            FAIL(run.Result.error());
        }
        REQUIRE(run.Result->Rows.size() == 8);
        for (const Core::TruthTableRow &row : run.Result->Rows) {
            const auto high_count = static_cast<std::size_t>(std::ranges::count(row.InputsHigh, true));
            REQUIRE(row.HasOperatingPoint());
            const double expected = gate_case.HighForCount[high_count] ? 5.0 : 0.0;
            CHECK_THAT(row.OutputVoltages[0], Catch::Matchers::WithinAbs(expected, 0.05));
        }
    }
}

TEST_CASE("An AND gate of eight inputs is high only when all of them are", "[simulator][digital]") {
    SKIP_WITHOUT_DIGITAL_MODELS();
    std::vector<Core::LogicInput> inputs(8);
    Core::LogicPart gate(Core::ComponentType::AND);
    gate.SetName("U1");
    gate.SetInputCount(8);
    Core::Circuit circuit;
    std::vector<int> nodes;
    for (std::size_t index = 0; index < inputs.size(); ++index) {
        inputs[index].SetName(std::format("V{}", index + 1));
        inputs[index].SetHigh(true);
        circuit.Add(inputs[index], {static_cast<int>(index) + 1});
        nodes.push_back(static_cast<int>(index) + 1);
    }
    nodes.push_back(9);
    circuit.Add(gate, nodes);

    const Core::OperatingPointRun all_high = Core::RunOperatingPoint(circuit);
    REQUIRE(all_high.Result.has_value());
    CHECK_THAT(all_high.Result->NodeVoltages[9], Catch::Matchers::WithinAbs(5.0, 0.05));
    // The gate reports its output current, which is the last of its nine terminals
    CHECK(Core::GetCurrentTerminals(gate) == std::vector<std::size_t>{8});

    // The last input alone going low is enough to bring the output down
    inputs[7].SetHigh(false);
    const Core::OperatingPointRun one_low = Core::RunOperatingPoint(circuit);
    REQUIRE(one_low.Result.has_value());
    CHECK_THAT(one_low.Result->NodeVoltages[9], Catch::Matchers::WithinAbs(0.0, 0.05));
}

TEST_CASE("A circuit with an analog part still needs a ground", "[simulator][digital]") {
    Core::LogicInput input;
    input.SetName("V1");
    Core::Resistor load;
    load.SetName("R1");
    load.SetValue(1e3);
    Core::Circuit circuit;
    circuit.Add(input, {1});
    circuit.Add(load, {1, 2});
    const Core::OperatingPointRun run = Core::RunOperatingPoint(circuit);
    REQUIRE_FALSE(run.Result.has_value());
    CHECK(run.Result.error().contains("no ground"));
}

TEST_CASE("A logic input gives a NAND latch a defined state", "[simulator][digital]") {
    SKIP_WITHOUT_DIGITAL_MODELS();
    Core::LogicInput set_bar;
    set_bar.SetName("V1");
    Core::LogicInput reset_bar;
    reset_bar.SetName("V2");
    reset_bar.SetHigh(true);
    Core::LogicPart gate_q(Core::ComponentType::NAND);
    gate_q.SetName("U1");
    Core::LogicPart gate_qn(Core::ComponentType::NAND);
    gate_qn.SetName("U2");
    const Core::Ground reference;
    Core::Circuit circuit;
    // U1: A = S_BAR (1), B = QN (3), Y = Q (2); U2: A = R_BAR (4), B = Q (2), Y = QN (3)
    circuit.Add(set_bar, {1});
    circuit.Add(reset_bar, {4});
    circuit.Add(gate_q, {1, 3, 2});
    circuit.Add(gate_qn, {4, 2, 3});
    circuit.Add(reference, {0});

    // S_BAR low sets Q regardless of the other input, so the latch has a state from the start
    const Core::OperatingPointRun set = Core::RunOperatingPoint(circuit);
    REQUIRE(set.Result.has_value());
    CHECK_THAT(set.Result->NodeVoltages[2], Catch::Matchers::WithinAbs(5.0, 0.05));
    CHECK_THAT(set.Result->NodeVoltages[3], Catch::Matchers::WithinAbs(0.0, 0.05));

    // Now R_BAR low resets it: Q low, QN high
    set_bar.SetHigh(true);
    reset_bar.SetHigh(false);
    const Core::OperatingPointRun reset = Core::RunOperatingPoint(circuit);
    REQUIRE(reset.Result.has_value());
    CHECK_THAT(reset.Result->NodeVoltages[2], Catch::Matchers::WithinAbs(0.0, 0.05));
    CHECK_THAT(reset.Result->NodeVoltages[3], Catch::Matchers::WithinAbs(5.0, 0.05));
}
