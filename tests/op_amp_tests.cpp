/**
 * @file    op_amp_tests.cpp
 * @brief   Tests for the op-amps and the library models of the Nodalys simulation backend.
 * @details The op-amp expectations mirror the ngspice-backed simulator tests (tests/simulator_tests.cpp): the ideal
 *          amplifier computes -R2/R1 with the finite open-loop gain, saturates a diode drop from its supplies, and
 *          the uA741 Boyle macromodel swings 1 V short of its supplies and slews at its datasheet rate. The library
 *          model cases parse a vendor ".model" line and check the same circuit against the theory the model
 *          encodes (Shockley for the diode, the level-1 MOSFET equations).
 */

#include "components/bjt.h"
#include "components/diode.h"
#include "components/ground.h"
#include "components/mosfet.h"
#include "components/op_amp.h"
#include "components/resistor.h"
#include "components/vcc.h"
#include "components/voltage_source.h"
#include "model/library_model.h"
#include "nodalys/nodalys_backend.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <numbers>
#include <string>

namespace {

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

double CurrentOf(const Core::OperatingPoint &point, const std::string &name) {
    const auto found = std::ranges::find(point.Currents, name, &Core::ComponentCurrent::Name);
    REQUIRE(found != point.Currents.end());
    return found->Current;
}

/**
 * @struct  InvertingAmplifier
 * @brief   A Nodalys inverting amplifier: 1k input and 10k feedback resistors around an op-amp on ±15 V supplies.
 * @details Theory: v(out) = -R2/R1 · v(in) · A/(A + 11) with the open-loop gain A (Sedra & Smith, ch. 2), which the
 *          1e5 of the ideal op-amp limits to 10 ppm of error.
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

TEST_CASE("The ideal op-amp amplifies inverting by the resistor ratio") {
    const InvertingAmplifier amplifier(0.5);
    Core::NodalysBackend backend;
    const Core::OperatingPointRun run = backend.RunOperatingPoint(amplifier.Circuit);
    REQUIRE(run.Result.has_value());
    const double gain = 1e5;
    const double expected = -5.0 * gain / (gain + 11.0);
    REQUIRE_THAT(run.Result->NodeVoltages[3], WithinAbs(expected, 1e-3));
}

TEST_CASE("The ideal op-amp output stops at its supply pins") {
    const InvertingAmplifier amplifier(2.0); // -20 V wanted, beyond both 15 V rails
    Core::NodalysBackend backend;
    const Core::OperatingPointRun run = backend.RunOperatingPoint(amplifier.Circuit);
    REQUIRE(run.Result.has_value());
    // The clamp diodes with N=0.01 stop the gain stage within about 10 mV of the rail: at the milliampere the
    // feedback network draws, their drop is n·Vt·ln(I/IS) ≈ 7 mV (Shockley)
    REQUIRE_THAT(run.Result->NodeVoltages[3], WithinAbs(-15.0, 1e-2));
}

TEST_CASE("A uA741 macromodel amplifies like the ideal op-amp but swings 1 V short of its supplies") {
    const double input = GENERATE(0.5, 2.0, -2.0);
    CAPTURE(input);
    InvertingAmplifier amplifier(input);
    amplifier.Amplifier.SetModel(*Core::FindOpAmpModel(Core::ComponentType::OP_AMP, "uA741"));
    Core::NodalysBackend backend;
    const Core::OperatingPointRun run = backend.RunOperatingPoint(amplifier.Circuit);
    REQUIRE(run.Result.has_value());
    // The limits stop the output about 1 V from each 15 V supply, as the datasheet swing
    const double expected = input == 0.5 ? -5.0 : (input > 0.0 ? -14.0 : 14.0);
    REQUIRE_THAT(run.Result->NodeVoltages[3], WithinAbs(expected, 0.1));
}

TEST_CASE("A uA741 macromodel slews at its datasheet rate") {
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

    Core::NodalysBackend backend;
    const Core::TransientRun run =
        backend.RunTransient(circuit, {.StopTime = 40e-6, .TimeStep = 10e-9}, std::nullopt, {});
    REQUIRE(run.Result.has_value());
    REQUIRE(run.Result->IsComplete);
    // Time from -3 V to +3 V on the way up
    const std::vector<double> &times = run.Result->Times;
    const std::vector<double> &output = run.Result->NodeVoltages[2];
    const auto crossing = [&](const double level) {
        const auto index = std::ranges::find_if(output, [level](const double value) { return value >= level; });
        REQUIRE(index != output.end());
        return times[static_cast<std::size_t>(index - output.begin())];
    };
    const double slew_rate = 6.0 / (crossing(3.0) - crossing(-3.0));
    REQUIRE_THAT(slew_rate, WithinRel(0.5e6, 0.05));
}

TEST_CASE("A diode with a library model conducts at its model saturation current") {
    // Theory (Shockley, as SPICE evaluates it): a forward current I sets the junction voltage to n·Vt·ln(I/IS).
    // With the 1N4148 model IS=2.52 nA and N=1.752, 1 mA sits at about 0.62 V
    Core::LibraryModel model;
    model.Name = "1N4148";
    model.Kind = Core::ComponentType::DIODE;
    model.Text = ".model 1N4148 D(IS=2.52n N=1.752 RS=0.568 BV=100 IBV=100u CJO=4p VJ=0.75 M=0.4 TT=20n)\n";
    Core::VCC supply;
    supply.SetName("V1");
    supply.SetValue(5.0);
    Core::Resistor series;
    series.SetName("R1");
    series.SetValue(4.4e3); // (5 V − 0.62 V) / 1 mA
    Core::Diode diode(Core::ComponentType::DIODE);
    diode.SetName("D1");
    diode.SetLibraryModel("1N4148");
    Core::Ground ground;
    Core::Circuit circuit;
    circuit.AddLibraryModel(model);
    circuit.Add(supply, {1});
    circuit.Add(series, {1, 2});
    circuit.Add(diode, {2, 0});
    circuit.Add(ground, {0});

    Core::NodalysBackend backend;
    const Core::OperatingPointRun run = backend.RunOperatingPoint(circuit);
    REQUIRE(run.Result.has_value());
    const double thermal = 8.617087399994e-5 * 300.15;
    const double expected = 1.752 * thermal * std::log(1e-3 / 2.52e-9) + 1e-3 * 0.568;
    REQUIRE_THAT(run.Result->NodeVoltages[2], WithinAbs(expected, 0.02));
}

TEST_CASE("A transistor with a library model amplifies with its model beta") {
    // Theory: a common-emitter stage biased into its active region gives IC = BF·IB (Sedra & Smith, ch. 6), here a
    // model with BF=416 alone, so the relation holds without the low-current leakage a full model adds
    Core::LibraryModel model;
    model.Name = "2N3904";
    model.Kind = Core::ComponentType::NPN;
    model.Text = ".model 2N3904 NPN(IS=1e-14 BF=416 RB=10 RC=1)\n";
    Core::VCC supply;
    supply.SetName("V1");
    supply.SetValue(10.0);
    Core::Resistor collector;
    collector.SetName("RC");
    collector.SetValue(1e3);
    Core::Resistor base;
    base.SetName("RB");
    base.SetValue(1e6);
    Core::BJT transistor(Core::ComponentType::NPN);
    transistor.SetName("Q1");
    transistor.SetLibraryModel("2N3904");
    Core::Ground ground;
    Core::Circuit circuit;
    circuit.AddLibraryModel(model);
    circuit.Add(supply, {1});
    circuit.Add(collector, {1, 2});
    circuit.Add(base, {1, 3});
    circuit.Add(transistor, {2, 3, 0});
    circuit.Add(ground, {0});

    Core::NodalysBackend backend;
    const Core::OperatingPointRun run = backend.RunOperatingPoint(circuit);
    REQUIRE(run.Result.has_value());
    // IB ≈ (10 V − 0.65 V)/1 MOhm, IC = BF·IB ≈ 3.9 mA, v(2) = 10 V − IC·1k
    const double base_current = (10.0 - 0.65) / 1e6;
    const double collector_current = 416.0 * base_current;
    REQUIRE_THAT(run.Result->NodeVoltages[2], WithinAbs(10.0 - collector_current * 1e3, 0.3));
}

TEST_CASE("A BSIM MOSFET library model is unsupported, with a message saying so") {
    Core::LibraryModel model;
    model.Name = "BSIM4_N";
    model.Kind = Core::ComponentType::NMOS;
    model.Text = ".model BSIM4_N NMOS(LEVEL=54 VTH0=0.7)\n";
    Core::VCC supply;
    supply.SetName("V1");
    supply.SetValue(5.0);
    Core::MOSFET transistor(Core::ComponentType::NMOS);
    transistor.SetName("M1");
    transistor.SetLibraryModel("BSIM4_N");
    Core::Ground ground;
    Core::Circuit circuit;
    circuit.AddLibraryModel(model);
    circuit.Add(supply, {1});
    circuit.Add(transistor, {1, 1, 0});
    circuit.Add(ground, {0});

    Core::NodalysBackend backend;
    const Core::OperatingPointRun run = backend.RunOperatingPoint(circuit);
    REQUIRE(!run.Result.has_value());
    REQUIRE(run.Result.error().find("level 1") != std::string::npos);
}

TEST_CASE("A part naming a library model the circuit lacks reports it") {
    Core::Resistor load;
    load.SetName("R1");
    load.SetValue(1e3);
    Core::Diode diode(Core::ComponentType::DIODE);
    diode.SetName("D1");
    diode.SetLibraryModel("missing");
    Core::VCC supply;
    supply.SetName("V1");
    supply.SetValue(5.0);
    Core::Ground ground;
    Core::Circuit circuit;
    circuit.Add(supply, {1});
    circuit.Add(load, {1, 2});
    circuit.Add(diode, {2, 0});
    circuit.Add(ground, {0});

    Core::NodalysBackend backend;
    const Core::OperatingPointRun run = backend.RunOperatingPoint(circuit);
    REQUIRE(!run.Result.has_value());
    REQUIRE(run.Result.error().find("does not have") != std::string::npos);
}
