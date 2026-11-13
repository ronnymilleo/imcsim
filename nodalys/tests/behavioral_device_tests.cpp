/**
 * @file    behavioral_device_tests.cpp
 * @brief   Tests of the behavioral sources of Nodalys in circuits with known answers: a multiplier, a squarer, a soft
 *          limiter, a source that follows a current, one that follows time, the small-signal gain at a bias, the
 *          netlist element and the refusals.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <nodalys/nodalys.h>
#include <numbers>
#include <string>
#include <vector>

using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

constexpr double DECIBELS_PER_DECADE = 20.0;
/// Every node leaks to ground through the 1e-12 S of GMIN, which moves a node behind 1 kOhm by a few parts in 1e9.
constexpr double GMIN_TOLERANCE = 1e-7;

Nodalys::Circuit Build(Nodalys::CircuitBuilder &builder) {
    auto circuit = builder.Build();
    REQUIRE(circuit.has_value());
    return std::move(*circuit);
}

Nodalys::OperatingPointResult OperatingPoint(const Nodalys::Circuit &circuit) {
    auto point = Nodalys::ComputeOperatingPoint(circuit);
    REQUIRE(point.has_value());
    return std::move(*point);
}

double CurrentOf(const Nodalys::OperatingPointResult &point, const std::string_view name) {
    const auto found = std::ranges::find(point.Currents, name, &Nodalys::ComponentCurrent::Name);
    REQUIRE(found != point.Currents.end());
    return found->Current;
}

} // namespace

TEST_CASE("A behavioral voltage source multiplies two node voltages", "[nodalys][behavioral]") {
    Nodalys::CircuitBuilder builder;
    builder.AddVdc("V1", 1, 0, 3.0);
    builder.AddVdc("V2", 2, 0, 4.0);
    builder.AddBehavioralVoltage("B1", 3, 0, "v(1) * v(2)");
    builder.AddResistor("R1", 3, 0, 1e3);
    const Nodalys::OperatingPointResult point = OperatingPoint(Build(builder));
    CHECK_THAT(point.NodeVoltages[3], WithinAbs(12.0, GMIN_TOLERANCE));
    // The source delivers 12 mA into the load, which is minus that into its + terminal, as for any voltage source
    CHECK_THAT(CurrentOf(point, "B1"), WithinAbs(-12e-3, GMIN_TOLERANCE));
}

TEST_CASE("A behavioral voltage source reads a difference of two nodes", "[nodalys][behavioral]") {
    Nodalys::CircuitBuilder builder;
    builder.AddVdc("V1", 1, 0, 3.0);
    builder.AddVdc("V2", 2, 0, 4.0);
    builder.AddBehavioralVoltage("B1", 3, 0, "10 * v(1, 2) + 100 * v(2, gnd)");
    builder.AddResistor("R1", 3, 0, 1e3);
    CHECK_THAT(OperatingPoint(Build(builder)).NodeVoltages[3], WithinAbs(-10.0 + 400.0, GMIN_TOLERANCE));
}

TEST_CASE("A behavioral current source pushes its current from + to - through itself", "[nodalys][behavioral]") {
    Nodalys::CircuitBuilder builder;
    builder.AddVdc("V1", 1, 0, 0.026);
    // 1 mA times tanh(1), into a 1 kOhm load whose node is the + of the source: the current leaves the node
    builder.AddBehavioralCurrent("B1", 2, 0, "1m * tanh(v(1) / 26m)");
    builder.AddResistor("R1", 2, 0, 1e3);
    const Nodalys::OperatingPointResult point = OperatingPoint(Build(builder));
    CHECK_THAT(point.NodeVoltages[2], WithinAbs(-std::tanh(1.0), GMIN_TOLERANCE));
    CHECK_THAT(CurrentOf(point, "B1"), WithinAbs(1e-3 * std::tanh(1.0), 1e-12));
}

TEST_CASE("A behavioral source that squares a swept voltage follows the square at every point, from either side",
          "[nodalys][behavioral]") {
    Nodalys::CircuitBuilder builder;
    builder.AddVdc("V1", 1, 0, 0.0);
    builder.AddBehavioralVoltage("B1", 2, 0, "v(1) ^ 2");
    builder.AddResistor("R1", 2, 0, 1e3);
    const auto sweep = Nodalys::ComputeDCSweep(
        Build(builder), {.SweptSource = "V1", .SweptStart = -2.0, .SweptStop = 2.0, .SweptStep = 0.25});
    REQUIRE(sweep.has_value());
    REQUIRE(sweep->SweptValues.size() == 17);
    for (std::size_t index = 0; index < sweep->SweptValues.size(); ++index) {
        const double input = sweep->SweptValues[index];
        INFO("input " << input);
        CHECK_THAT(sweep->Curves[0].NodeVoltages[2][index], WithinAbs(input * input, GMIN_TOLERANCE));
    }
}

TEST_CASE("A behavioral source follows the current of a voltage source, which may be added after it",
          "[nodalys][behavioral]") {
    Nodalys::CircuitBuilder builder;
    // The source of the current comes later in the circuit than the line that reads it
    builder.AddBehavioralVoltage("B1", 2, 0, "-1k * i(V1)");
    builder.AddResistor("R2", 2, 0, 10e3);
    builder.AddVdc("V1", 1, 0, 5.0);
    builder.AddResistor("R1", 1, 0, 1e3);
    const Nodalys::OperatingPointResult point = OperatingPoint(Build(builder));
    // V1 delivers 5 mA, so the current into its + terminal is -5 mA
    CHECK_THAT(CurrentOf(point, "V1"), WithinAbs(-5e-3, 1e-9));
    CHECK_THAT(point.NodeVoltages[2], WithinAbs(5.0, GMIN_TOLERANCE));
}

TEST_CASE("The name of a current may be written in any case", "[nodalys][behavioral]") {
    Nodalys::CircuitBuilder builder;
    builder.AddVdc("Vsense", 1, 0, 5.0);
    builder.AddResistor("R1", 1, 0, 1e3);
    builder.AddBehavioralVoltage("B1", 2, 0, "-1k * I(VSENSE)");
    builder.AddResistor("R2", 2, 0, 10e3);
    CHECK_THAT(OperatingPoint(Build(builder)).NodeVoltages[2], WithinAbs(5.0, GMIN_TOLERANCE));
}

TEST_CASE("A behavioral source follows time in a transient and reads 0 at the operating point",
          "[nodalys][behavioral]") {
    Nodalys::CircuitBuilder builder;
    builder.AddBehavioralVoltage("B1", 1, 0, "sin(2 * pi * 1k * time) + 2 * time");
    builder.AddResistor("R1", 1, 0, 1e3);
    const Nodalys::Circuit circuit = Build(builder);
    CHECK_THAT(OperatingPoint(circuit).NodeVoltages[1], WithinAbs(0.0, 1e-12));
    const auto result = Nodalys::ComputeTransient(circuit, {.StopTime = 2e-3, .TimeStep = 20e-6, .MaxStep = 20e-6});
    REQUIRE(result.has_value());
    REQUIRE(result->Times.size() > 50);
    for (std::size_t index = 0; index < result->Times.size(); ++index) {
        const double time = result->Times[index];
        INFO("time " << time);
        CHECK_THAT(result->NodeVoltages[1][index],
                   WithinAbs(std::sin(2.0 * std::numbers::pi * 1e3 * time) + 2.0 * time, GMIN_TOLERANCE));
    }
}

TEST_CASE("A behavioral source reading the voltage of a capacitor integrates with it in a transient",
          "[nodalys][behavioral]") {
    // A current source of 1 mA * (1 - v / 2) charging 1 uF: v approaches 2 V with the time constant 2 ms
    Nodalys::CircuitBuilder builder;
    builder.AddBehavioralCurrent("B1", 0, 1, "1m * (1 - v(1) / 2)");
    // A start at 1 nV, which is how a capacitor is told to start from its own voltage and not from the operating point
    builder.AddCapacitor("C1", 1, 0, 1e-6, 1e-9);
    const auto result = Nodalys::ComputeTransient(
        Build(builder), {.StopTime = 6e-3, .TimeStep = 20e-6, .MaxStep = 20e-6, .UseInitialConditions = true});
    REQUIRE(result.has_value());
    const double time_constant = 2e-3;
    for (std::size_t index = 0; index < result->Times.size(); ++index) {
        const double expected = 2.0 * (1.0 - std::exp(-result->Times[index] / time_constant));
        CHECK_THAT(result->NodeVoltages[1][index], WithinAbs(expected, 1e-3));
    }
}

TEST_CASE("The small-signal gain of a behavioral source is the derivative of its expression at the bias",
          "[nodalys][behavioral]") {
    // A squarer biased at 2 V, in the voltage and in the current form: d(v^2)/dv = 4 at the bias
    Nodalys::CircuitBuilder builder;
    builder.AddVdc("V1", 1, 0, 2.0, 1.0);
    builder.AddBehavioralVoltage("B1", 2, 0, "v(1) ^ 2");
    builder.AddResistor("R1", 2, 0, 1e3);
    builder.AddBehavioralCurrent("B2", 3, 0, "1m * v(1) ^ 2");
    builder.AddResistor("R2", 3, 0, 1e3);
    const Nodalys::Circuit circuit = Build(builder);

    const auto point = Nodalys::ComputeOperatingPoint(circuit);
    REQUIRE(point.has_value());
    CHECK_THAT(point->NodeVoltages[2], WithinAbs(4.0, GMIN_TOLERANCE));
    CHECK_THAT(point->NodeVoltages[3], WithinAbs(-4.0, GMIN_TOLERANCE));

    const auto sweep =
        Nodalys::ComputeACSweep(circuit, {.StartFrequency = 10.0, .StopFrequency = 1e3, .PointsPerDecade = 1});
    REQUIRE(sweep.has_value());
    for (const std::size_t node : {2, 3}) {
        for (const double magnitude : sweep->NodeMagnitudesDecibels[node]) {
            CHECK_THAT(magnitude, WithinAbs(DECIBELS_PER_DECADE * std::log10(4.0), 1e-6));
        }
    }
    // The current form is inverted: the current leaves the node, so the gain is -4
    CHECK_THAT(std::fabs(sweep->NodePhasesDegrees[2][0]), WithinAbs(0.0, 1e-6));
    CHECK_THAT(std::fabs(sweep->NodePhasesDegrees[3][0]), WithinAbs(180.0, 1e-6));

    const auto transfer = Nodalys::ComputeTransferFunction(circuit, {.InputSource = "V1", .OutputPositiveNode = 2});
    REQUIRE(transfer.has_value());
    CHECK_THAT(transfer->Gain, WithinAbs(4.0, GMIN_TOLERANCE));
    const auto current_transfer =
        Nodalys::ComputeTransferFunction(circuit, {.InputSource = "V1", .OutputPositiveNode = 3});
    REQUIRE(current_transfer.has_value());
    CHECK_THAT(current_transfer->Gain, WithinAbs(-4.0, GMIN_TOLERANCE));
}

TEST_CASE("There is no small-signal gain where the expression is flat, and none from a source held by time",
          "[nodalys][behavioral]") {
    // v^2 at 0 V has slope 0, and a time-only source has no input at all
    Nodalys::CircuitBuilder builder;
    builder.AddVdc("V1", 1, 0, 0.0, 1.0);
    builder.AddBehavioralVoltage("B1", 2, 0, "v(1) ^ 2");
    builder.AddResistor("R1", 2, 0, 1e3);
    builder.AddBehavioralVoltage("B2", 3, 0, "time");
    builder.AddResistor("R2", 3, 0, 1e3);
    const auto sweep = Nodalys::ComputeACSweep(Build(builder),
                                               {.StartFrequency = 100.0, .StopFrequency = 100.0, .PointsPerDecade = 1});
    REQUIRE(sweep.has_value());
    CHECK(sweep->NodeMagnitudesDecibels[2][0] < -200.0);
    CHECK(sweep->NodeMagnitudesDecibels[3][0] < -200.0);
}

TEST_CASE("A behavioral transconductor with a capacitor has the pole the derivative says", "[nodalys][behavioral]") {
    // gm = 1 mS through the derivative of 1m * v, into 1 kOhm || 1 uF: a pole at -1 / (R C) = -1000 rad/s
    Nodalys::CircuitBuilder builder;
    builder.AddVdc("V1", 1, 0, 0.0, 1.0);
    builder.AddBehavioralCurrent("B1", 0, 2, "1m * v(1)");
    builder.AddResistor("R1", 2, 0, 1e3);
    builder.AddCapacitor("C1", 2, 0, 1e-6);
    const auto roots = Nodalys::ComputePoleZero(Build(builder), {.OutputNode = 2, .InputSource = "V1"});
    REQUIRE(roots.has_value());
    REQUIRE(roots->Poles.size() == 1);
    CHECK_THAT(roots->Poles[0].real(), WithinRel(-1000.0, 1e-6));
    CHECK_THAT(roots->Poles[0].imag(), WithinAbs(0.0, 1e-6));
}

TEST_CASE("A behavioral source that squares the swing of a sine has the harmonics sin squared has",
          "[nodalys][behavioral][hb]") {
    // sin^2 = 1/2 - cos(2 w t) / 2: 0.5 V of DC and 0.5 V at the second harmonic
    Nodalys::CircuitBuilder builder;
    builder.AddVsine("V1", 1, 0, {.Amplitude = 1.0, .Frequency = 1e3});
    builder.AddBehavioralVoltage("B1", 2, 0, "v(1) ^ 2");
    builder.AddResistor("R1", 2, 0, 1e3);
    const auto result = Nodalys::ComputeHarmonicBalance(Build(builder), {.FundamentalFrequency = 1e3, .Harmonics = 6});
    REQUIRE(result.has_value());
    CHECK_THAT(result->NodePhasors[2][0].real(), WithinAbs(0.5, 1e-6));
    CHECK_THAT(std::abs(result->NodePhasors[2][1]), WithinAbs(0.0, 1e-6));
    CHECK_THAT(std::abs(result->NodePhasors[2][2]), WithinAbs(0.5, 1e-6));
    CHECK_THAT(std::abs(result->NodePhasors[2][3]), WithinAbs(0.0, 1e-6));
}

TEST_CASE("The harmonic balance refuses a behavioral source that reads time", "[nodalys][behavioral][hb]") {
    Nodalys::CircuitBuilder builder;
    builder.AddVsine("V1", 1, 0, {.Amplitude = 1.0, .Frequency = 1e3});
    builder.AddBehavioralVoltage("B1", 2, 0, "v(1) * time");
    builder.AddResistor("R1", 2, 0, 1e3);
    const auto result = Nodalys::ComputeHarmonicBalance(Build(builder), {.FundamentalFrequency = 1e3, .Harmonics = 4});
    REQUIRE_FALSE(result.has_value());
    CHECK_THAT(result.error(), ContainsSubstring("behavioral source that reads time"));
}

TEST_CASE("A circuit with no value for the expression fails and says which source and why", "[nodalys][behavioral]") {
    Nodalys::CircuitBuilder builder;
    builder.AddVdc("V1", 1, 0, -4.0);
    builder.AddBehavioralVoltage("B1", 2, 0, "sqrt(v(1))");
    builder.AddResistor("R1", 2, 0, 1e3);
    const auto point = Nodalys::ComputeOperatingPoint(Build(builder));
    REQUIRE_FALSE(point.has_value());
    CHECK_THAT(point.error(), ContainsSubstring("B1"));
    // The last iterate that failed is named, which source stepping may have scaled down from the -4 V
    CHECK_THAT(point.error(), ContainsSubstring("is out of range"));
    CHECK_THAT(point.error(), ContainsSubstring("sqrt("));
}

TEST_CASE("A source whose expression is wrong is refused when the circuit is built, with its name and the reason",
          "[nodalys][behavioral]") {
    SECTION("An unknown function") {
        Nodalys::CircuitBuilder builder;
        builder.AddBehavioralVoltage("B7", 1, 0, "foo(v(2))");
        const auto circuit = builder.Build();
        REQUIRE_FALSE(circuit.has_value());
        CHECK_THAT(circuit.error(), ContainsSubstring("B7"));
        CHECK_THAT(circuit.error(), ContainsSubstring("\"foo\" is not a function Nodalys knows"));
    }
    SECTION("A function ngspice reads as something else") {
        Nodalys::CircuitBuilder builder;
        builder.AddBehavioralCurrent("B1", 1, 0, "limit(v(2), -1, 1)");
        const auto circuit = builder.Build();
        REQUIRE_FALSE(circuit.has_value());
        CHECK_THAT(circuit.error(), ContainsSubstring("Monte Carlo"));
    }
    SECTION("A current of a source the circuit does not have") {
        Nodalys::CircuitBuilder builder;
        builder.AddBehavioralVoltage("B1", 1, 0, "i(Vmissing)");
        const auto circuit = builder.Build();
        REQUIRE_FALSE(circuit.has_value());
        CHECK_THAT(circuit.error(), ContainsSubstring("i(Vmissing) must name a voltage source"));
    }
    SECTION("A current of a source that is not a voltage source") {
        Nodalys::CircuitBuilder builder;
        builder.AddIdc("I1", 1, 0, 1e-3);
        builder.AddBehavioralVoltage("B1", 2, 0, "i(I1)");
        const auto circuit = builder.Build();
        REQUIRE_FALSE(circuit.has_value());
        CHECK_THAT(circuit.error(), ContainsSubstring("i(I1) must name a voltage source"));
    }
    SECTION("A node that is not a number") {
        Nodalys::CircuitBuilder builder;
        builder.AddBehavioralVoltage("B1", 1, 0, "v(out)");
        const auto circuit = builder.Build();
        REQUIRE_FALSE(circuit.has_value());
        CHECK_THAT(circuit.error(), ContainsSubstring("must name nodes by their number"));
    }
}

TEST_CASE("The B element of a netlist names nodes and sources as the netlist does", "[nodalys][behavioral][netlist]") {
    SECTION("Voltage form, with nodes by name and spaces around the equal sign") {
        const auto parsed = Nodalys::ParseNetlist("* multiplier\n"
                                                  "V1 a 0 3\n"
                                                  "V2 b 0 4\n"
                                                  "B1 out 0 V = v(a) * v(b) + v(a, b)\n"
                                                  "R1 out 0 1k\n"
                                                  ".op\n"
                                                  ".end\n");
        REQUIRE(parsed.has_value());
        const Nodalys::OperatingPointResult point = OperatingPoint(parsed->CircuitInstance);
        // 3 * 4 + (3 - 4)
        CHECK(std::ranges::any_of(point.NodeVoltages,
                                  [](const double volts) { return std::fabs(volts - 11.0) < GMIN_TOLERANCE; }));
    }
    SECTION("Current form, in lower case, with a node named by its number") {
        const auto parsed = Nodalys::ParseNetlist("* current form\n"
                                                  "V1 1 0 2\n"
                                                  "b1 2 0 i=1m*V(1)\n"
                                                  "R1 2 0 1k\n"
                                                  ".end\n");
        REQUIRE(parsed.has_value());
        CHECK_THAT(OperatingPoint(parsed->CircuitInstance).NodeVoltages[2], WithinAbs(-2.0, GMIN_TOLERANCE));
    }
    SECTION("A current read before its source is written") {
        const auto parsed = Nodalys::ParseNetlist("* forward current\n"
                                                  "B1 2 0 V=-1k*i(V1)\n"
                                                  "R2 2 0 10k\n"
                                                  "V1 1 0 5\n"
                                                  "R1 1 0 1k\n"
                                                  ".end\n");
        REQUIRE(parsed.has_value());
        CHECK_THAT(OperatingPoint(parsed->CircuitInstance).NodeVoltages[2], WithinAbs(5.0, GMIN_TOLERANCE));
    }
    SECTION("A comment after the expression is not part of it") {
        const auto parsed = Nodalys::ParseNetlist("* comment\n"
                                                  "V1 1 0 2\n"
                                                  "B1 2 0 V=v(1)*3 ; triple\n"
                                                  "R1 2 0 1k\n"
                                                  ".end\n");
        REQUIRE(parsed.has_value());
        CHECK_THAT(OperatingPoint(parsed->CircuitInstance).NodeVoltages[2], WithinAbs(6.0, GMIN_TOLERANCE));
    }
    SECTION("A malformed line, a wrong expression and a source inside a subcircuit are refused") {
        const auto no_kind = Nodalys::ParseNetlist("* title\nB1 1 0 v(2)\n.end\n");
        REQUIRE_FALSE(no_kind.has_value());
        CHECK_THAT(no_kind.error(), ContainsSubstring("Malformed behavioral source"));

        const auto wrong = Nodalys::ParseNetlist("* title\nB1 1 0 V=atan2(1, 2)\n.end\n");
        REQUIRE_FALSE(wrong.has_value());
        CHECK_THAT(wrong.error(), ContainsSubstring("B1"));
        CHECK_THAT(wrong.error(), ContainsSubstring("atan2"));

        const auto inside = Nodalys::ParseNetlist("* subcircuit\n"
                                                  ".subckt DOUBLER in out\n"
                                                  "B1 out 0 V=2*v(in)\n"
                                                  ".ends\n"
                                                  "V1 1 0 1\n"
                                                  "X1 1 2 DOUBLER\n"
                                                  ".end\n");
        REQUIRE_FALSE(inside.has_value());
        CHECK_THAT(inside.error(), ContainsSubstring("inside a subcircuit"));
    }
}
