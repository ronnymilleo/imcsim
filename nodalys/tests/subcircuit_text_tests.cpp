/**
 * @file    subcircuit_text_tests.cpp
 * @brief   Tests for adding a subcircuit written as SPICE text to a circuit: its ports, parameters, expressions,
 *          models, nested subcircuits and instances, and what it refuses.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <nodalys/nodalys.h>

#include "parser/spice_text.h"
#include "parser/spice_values.h"

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>

using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

/// Tolerance of a node voltage that a closed form gives, in volts.
constexpr double VOLTAGE_TOLERANCE = 1e-6;

/**
 * @struct  Bench
 * @brief   A source on node 1 with a subcircuit text between node 1 and node 2, and what its operating point gives.
 */
struct Bench {
    Nodalys::CircuitBuilder Builder{3};
    std::vector<std::string> Warnings;
    std::optional<std::string> Failure;
    std::vector<double> Voltages;
};

/**
 * @brief   Puts a text between the nodes 1 and 2 of a 10 V source, with a resistor to ground on node 2 when asked,
 *          and solves the operating point.
 */
Bench Solve(const std::string &body, const std::vector<std::string> &ports = {"IN", "OUT"},
            const std::map<std::string, double> &parameters = {}, const double load_ohms = 0.0) {
    Bench bench;
    bench.Builder.AddVdc("V1", 1, 0, 10.0);
    if (load_ohms > 0.0) {
        bench.Builder.AddResistor("RLOAD", 2, 0, load_ohms);
    }
    const Nodalys::SubcircuitText text{.Name = "BLOCK", .Ports = ports, .Body = body};
    const auto added = Nodalys::AddSubcircuitText(bench.Builder, text,
                                                  {.Prefix = "X1", .PortNodes = {1, 2}, .Parameters = parameters});
    if (!added) {
        bench.Failure = added.error();
        return bench;
    }
    bench.Warnings = *added;
    auto circuit = bench.Builder.Build();
    REQUIRE(circuit.has_value());
    const auto point = Nodalys::ComputeOperatingPoint(*circuit);
    REQUIRE(point.has_value());
    bench.Voltages = point->NodeVoltages;
    return bench;
}

} // namespace

TEST_CASE("The text is split into logical lines and tokens that keep braces together", "[subcircuit_text]") {
    const std::vector<std::string> lines = Nodalys::SplitLogicalLines(
        "* a comment\nR1 a b 1k ; trailing\n  \n+ extra\nC1 a 0 {2 * CVAL} $ note\n$ whole line\n");
    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == "R1 a b 1k extra");
    CHECK(lines[1] == "C1 a 0 {2 * CVAL}");

    const std::vector<std::string> tokens = Nodalys::SplitTokens("R1 a, b R = {2 * RVAL} IC = 'x + 1' PULSE(0 1 2)");
    REQUIRE(tokens.size() == 6);
    CHECK(tokens[2] == "b");
    CHECK(tokens[3] == "R={2 * RVAL}");
    CHECK(tokens[4] == "IC='x + 1'");
    CHECK(tokens[5] == "PULSE(0 1 2)");
}

TEST_CASE("Values are literals or expressions of parameters, with or without braces", "[subcircuit_text]") {
    const Nodalys::ParameterLookup lookup = [](const std::string &name) -> std::optional<double> {
        if (name == "RVAL") {
            return 2e3;
        }
        return std::nullopt;
    };
    CHECK_THAT(*Nodalys::EvaluateValue("4.7k", lookup), WithinRel(4.7e3, 1e-12));
    CHECK_THAT(*Nodalys::EvaluateValue("{2 * rval + 1k}", lookup), WithinRel(5e3, 1e-12));
    CHECK_THAT(*Nodalys::EvaluateValue("'RVAL/4'", lookup), WithinRel(500.0, 1e-12));
    // A scale suffix is not a parameter name, and a function keeps its name
    CHECK_THAT(*Nodalys::EvaluateValue("{max(1meg, RVAL)}", lookup), WithinRel(1e6, 1e-12));
    CHECK_THAT(*Nodalys::EvaluateValue("{-RVAL}", lookup), WithinRel(-2e3, 1e-12));
    const auto unknown = Nodalys::EvaluateValue("{WRONG * 2}", lookup);
    REQUIRE_FALSE(unknown.has_value());
    CHECK_THAT(unknown.error(), ContainsSubstring("WRONG"));
    const auto time = Nodalys::EvaluateValue("{time}", lookup);
    REQUIRE_FALSE(time.has_value());
    CHECK_THAT(time.error(), ContainsSubstring("constant"));
}

TEST_CASE("A text maps its ports to the nodes of the instance and reads its parameters", "[subcircuit_text]") {
    const std::string body = "R1 IN OUT {RTOP}\nR2 OUT 0 {RBOT}\n.param RBOT={RTOP * 3}\n";
    // The divider rule: 10 V * 3k / (1k + 3k)
    const Bench by_default = Solve(body, {"IN", "OUT"}, {{"RTOP", 1e3}});
    REQUIRE_FALSE(by_default.Failure);
    CHECK_THAT(by_default.Voltages[2], WithinAbs(7.5, VOLTAGE_TOLERANCE));
    // An instance value replaces the default, and the expression follows it
    const Bench overridden = Solve(body, {"IN", "OUT"}, {{"RTOP", 5e3}});
    CHECK_THAT(overridden.Voltages[2], WithinAbs(7.5, VOLTAGE_TOLERANCE));
    // A value the instance passes wins over the .param line of the text with the same name
    const Bench passed = Solve("R1 IN OUT 1k\nR2 OUT 0 {RBOT}\n.param RBOT=1k\n", {"IN", "OUT"}, {{"RBOT", 3e3}});
    CHECK_THAT(passed.Voltages[2], WithinAbs(7.5, VOLTAGE_TOLERANCE));
}

TEST_CASE("A text is read without regard to case and with continuation lines", "[subcircuit_text]") {
    const Bench bench = Solve("* upper and lower case\nr1 in mid 1K\n+ \nR2 MID out 1k\nrl Out 0\n+ 2K\n");
    REQUIRE_FALSE(bench.Failure);
    // 10 V across 1k + 1k + 2k, the output after two of them
    CHECK_THAT(bench.Voltages[2], WithinAbs(5.0, VOLTAGE_TOLERANCE));
}

TEST_CASE("The internal nodes of an instance are private to it", "[subcircuit_text]") {
    // Two instances of one text in series, each with its own MID node: 1k + 1k, then 1k + 1k, then a 4k load
    Nodalys::CircuitBuilder builder(4);
    builder.AddVdc("V1", 1, 0, 10.0);
    builder.AddResistor("RLOAD", 3, 0, 4e3);
    const Nodalys::SubcircuitText text{.Name = "TWO", .Ports = {"A", "B"}, .Body = "R1 A MID 1k\nR2 MID B 1k\n"};
    REQUIRE(Nodalys::AddSubcircuitText(builder, text, {.Prefix = "X1", .PortNodes = {1, 2}, .Parameters = {}}));
    REQUIRE(Nodalys::AddSubcircuitText(builder, text, {.Prefix = "X2", .PortNodes = {2, 3}, .Parameters = {}}));
    auto circuit = builder.Build();
    REQUIRE(circuit.has_value());
    const auto point = Nodalys::ComputeOperatingPoint(*circuit);
    REQUIRE(point.has_value());
    // 10 V over 8k: node 2 sits after 2k, node 3 after 4k; the two MID nodes are nodes 4 and 5
    CHECK_THAT(point->NodeVoltages[2], WithinAbs(7.5, VOLTAGE_TOLERANCE));
    CHECK_THAT(point->NodeVoltages[3], WithinAbs(5.0, VOLTAGE_TOLERANCE));
    CHECK_THAT(point->NodeVoltages[4], WithinAbs(8.75, VOLTAGE_TOLERANCE));
    CHECK_THAT(point->NodeVoltages[5], WithinAbs(6.25, VOLTAGE_TOLERANCE));
    // The elements carry the name of their instance
    bool has_first = false;
    bool has_second = false;
    for (const Nodalys::ComponentCurrent &current : point->Currents) {
        has_first = has_first || current.Name == "X1.R1";
        has_second = has_second || current.Name == "X2.R1";
    }
    CHECK(has_first);
    CHECK(has_second);
}

TEST_CASE("A diode of the text uses the .model card of the text, whose values may be expressions",
          "[subcircuit_text]") {
    const std::string body = ".param ISAT=1e-15\nD1 IN OUT DMOD\n.model DMOD D(IS={ISAT * 10} N=1)\n";
    const Bench bench = Solve(body, {"IN", "OUT"}, {}, 1e3);
    REQUIRE_FALSE(bench.Failure);
    // Shockley: the current I through 1k satisfies 10 - 1k*I = Vd = Vt ln(I/Is + 1)
    const double drop = 10.0 - bench.Voltages[2];
    CHECK(drop > 0.55);
    CHECK(drop < 0.75);
    const double current = bench.Voltages[2] / 1e3;
    CHECK_THAT(drop, WithinAbs(0.025852 * std::log(current / 1e-14 + 1.0), 2e-3));
    CHECK(bench.Warnings.empty());
}

TEST_CASE("A transistor, a MOSFET and a switch of the text use their models", "[subcircuit_text]") {
    // A BJT with a base resistor, beta 100: Ic = 100 * (10 - 0.7) / 93k = 10 mA at most, so it saturates with a 1k
    const std::string bjt = "Q1 OUT B 0 QMOD\nRB IN B 93k\n.model QMOD NPN(BF=100 IS=1e-14)\n";
    const Bench saturated = Solve(bjt, {"IN", "OUT"}, {}, 1e3);
    REQUIRE_FALSE(saturated.Failure);
    CHECK(saturated.Voltages[2] > 0.0);
    CHECK(saturated.Voltages[2] < 10.0);

    // A level 1 NMOS as a source follower
    const std::string mosfet = "M1 IN IN OUT 0 NM L=10u W=100u\n.model NM NMOS(VTO=1 KP=2e-5 LEVEL=1)\n";
    const Bench follower = Solve(mosfet, {"IN", "OUT"}, {}, 1e3);
    REQUIRE_FALSE(follower.Failure);
    CHECK(follower.Voltages[2] > 0.0);

    // A switch closed by the 10 V of the input passes it to a 1k load through 1 ohm: 10 V * 1k / (1k + 1)
    const std::string closed = "S1 IN OUT IN 0 SMOD\n.model SMOD SW(VT=5 VH=0.1 RON=1 ROFF=1e9)\n";
    const Bench switched = Solve(closed, {"IN", "OUT"}, {}, 1e3);
    REQUIRE_FALSE(switched.Failure);
    CHECK_THAT(switched.Voltages[2], WithinRel(10.0 * 1e3 / 1001.0, 1e-3));
}

TEST_CASE("A model parameter the engine does not read is reported once, never dropped without a word",
          "[subcircuit_text]") {
    const std::string body = "D1 IN MID DMOD\nD2 MID OUT DMOD\n.model DMOD D(IS=1e-14 CJS=3p VAR=2)\n";
    const Bench bench = Solve(body, {"IN", "OUT"}, {}, 1e3);
    REQUIRE_FALSE(bench.Failure);
    REQUIRE(bench.Warnings.size() == 1);
    CHECK_THAT(bench.Warnings[0], ContainsSubstring("CJS, VAR"));
    CHECK_THAT(bench.Warnings[0], ContainsSubstring("DMOD"));
    CHECK_THAT(bench.Warnings[0], ContainsSubstring("BLOCK"));
}

TEST_CASE("A subcircuit written inside the text is expanded for each X line, with its own parameters",
          "[subcircuit_text]") {
    const std::string body = R"(.subckt HALF A B params: K=2
R1 A B {1k * K}
.ends HALF
X1 IN MID HALF K=1
X2 MID OUT HALF
RL OUT 0 3k
)";
    // 1k, then the default 2k, then 3k: 10 V over 6k, the output across 3k
    const Bench bench = Solve(body);
    REQUIRE_FALSE(bench.Failure);
    CHECK_THAT(bench.Voltages[2], WithinAbs(5.0, VOLTAGE_TOLERANCE));
}

TEST_CASE("A nested subcircuit sees the models and parameters of the text around it", "[subcircuit_text]") {
    const std::string body = R"(.param RBASE=1k
.model DMOD D(IS=1e-14)
.subckt LEG A B
R1 A MID {RBASE}
D1 MID B DMOD
.ends
X1 IN OUT LEG
)";
    const Bench bench = Solve(body, {"IN", "OUT"}, {}, 1e3);
    REQUIRE_FALSE(bench.Failure);
    CHECK(bench.Voltages[2] > 3.0);
    CHECK(bench.Voltages[2] < 5.0);
}

TEST_CASE("Controlled and behavioral sources of the text read the nodes and currents of the instance",
          "[subcircuit_text]") {
    // A gain stage, a sensed current mirrored by a CCCS, and a behavioral source reading both
    const std::string body = "E1 OUT 0 IN 0 {GAIN}\nVSENSE IN SENSED 0\nRS SENSED 0 1k\nF1 0 FOUT VSENSE 2\n"
                             "RF FOUT 0 1k\nB1 BOUT 0 V=v(IN)/2 + 1k*i(VSENSE)\nRB BOUT 0 1meg\n";
    Nodalys::CircuitBuilder builder(3);
    builder.AddVdc("V1", 1, 0, 10.0);
    const Nodalys::SubcircuitText text{.Name = "SRC", .Ports = {"IN", "OUT"}, .Body = body};
    REQUIRE(Nodalys::AddSubcircuitText(builder, text,
                                       {.Prefix = "X1", .PortNodes = {1, 2}, .Parameters = {{"GAIN", 0.5}}}));
    auto circuit = builder.Build();
    REQUIRE(circuit.has_value());
    const auto point = Nodalys::ComputeOperatingPoint(*circuit);
    REQUIRE(point.has_value());
    CHECK_THAT(point->NodeVoltages[2], WithinAbs(5.0, VOLTAGE_TOLERANCE));
    double mirrored = 0.0;
    for (const Nodalys::ComponentCurrent &current : point->Currents) {
        if (current.Name == "X1.F1") {
            mirrored = current.Current;
        }
    }
    // 10 V over 1k is 10 mA; the mirror gives twice that
    CHECK_THAT(std::abs(mirrored), WithinAbs(20e-3, 1e-6));
}

TEST_CASE("A VALUE expression of a controlled source is read like a behavioral source", "[subcircuit_text]") {
    const Bench bench = Solve("E1 OUT 0 VALUE = {v(IN) * 0.25 + limit_free}\n.param limit_free=1\n");
    REQUIRE_FALSE(bench.Failure);
    CHECK_THAT(bench.Voltages[2], WithinAbs(3.5, VOLTAGE_TOLERANCE));
}

TEST_CASE("A text with an element, a directive or a model the engine cannot simulate is refused by name",
          "[subcircuit_text]") {
    const auto refusal = [](const std::string &body) {
        const auto bench = Solve(body, {"IN", "OUT"}, {}, 1e3);
        REQUIRE(bench.Failure.has_value());
        return *bench.Failure;
    };
    const std::string jfet = refusal("J1 OUT IN 0 JMOD\n");
    CHECK_THAT(jfet, ContainsSubstring("BLOCK"));
    CHECK_THAT(jfet, ContainsSubstring("J1 OUT IN 0 JMOD"));
    CHECK_THAT(jfet, ContainsSubstring("JFET"));
    CHECK_THAT(refusal("K1 L1 L2 0.9\n"), ContainsSubstring("coupled inductors"));
    CHECK_THAT(refusal("V2 OUT 0 PULSE(0 1 0 1n 1n 1u 2u)\n"), ContainsSubstring("PULSE"));
    CHECK_THAT(refusal(".func twice(x) {2*x}\nR1 IN OUT 1\n"), ContainsSubstring(".func"));
    CHECK_THAT(refusal("R1 IN OUT 1k TC1=0.001\n"), ContainsSubstring("TC1"));
    CHECK_THAT(refusal("R1 IN OUT {NOPE}\n"), ContainsSubstring("NOPE"));
    CHECK_THAT(refusal("D1 IN OUT MISSING\n"), ContainsSubstring("MISSING"));
    CHECK_THAT(refusal("Q1 OUT IN 0 BIPOLAR\n.model BIPOLAR NJF(VTO=-2)\n"), ContainsSubstring("NJF"));
    CHECK_THAT(refusal("M1 IN IN OUT 0 NM\n.model NM NMOS(LEVEL=3 VTO=1)\n"), ContainsSubstring("level 1 model only"));
    CHECK_THAT(refusal("M1 IN IN OUT 0 NM AD=1p\n.model NM NMOS(VTO=1)\n"), ContainsSubstring("AD="));
    CHECK_THAT(refusal("E1 OUT 0 POLY(1) IN 0 0 1\n"), ContainsSubstring("POLY"));
    CHECK_THAT(refusal("F1 0 OUT VNONE 2\n"), ContainsSubstring("VNONE"));
    CHECK_THAT(refusal("B1 OUT 0 V=hertz*2\n"), ContainsSubstring("hertz"));
    CHECK_THAT(refusal("X1 IN OUT NOWHERE\n"), ContainsSubstring("NOWHERE"));
    CHECK_THAT(refusal(".subckt OPEN A B\nR1 A B 1\n"), ContainsSubstring("no .ends"));
    CHECK_THAT(refusal(".include other.lib\n"), ContainsSubstring(".include"));
    CHECK_THAT(refusal(".subckt TWICE A B\nX1 A B TWICE\n.ends\nX1 IN OUT TWICE\n"), ContainsSubstring("too deeply"));
    CHECK_THAT(refusal(".subckt TWO A B\nR1 A B 1\n.ends\nX1 TWO\n"), ContainsSubstring("X<name>"));
    CHECK_THAT(refusal(".subckt ONE A\nR1 A 0 1\n.ends\nX1 IN OUT ONE\n"), ContainsSubstring("2 nodes to ONE"));
    CHECK_THAT(refusal(".subckt ONE A params: K=1\nR1 A 0 1\n.ends\nX1 IN ONE Q=2\n"),
               ContainsSubstring("parameter Q"));
}

TEST_CASE("A text that does not fit its ports is refused", "[subcircuit_text]") {
    Nodalys::CircuitBuilder builder(2);
    const Nodalys::SubcircuitText text{.Name = "ONE", .Ports = {"A", "B"}, .Body = "R1 A B 1k\n"};
    const auto added = Nodalys::AddSubcircuitText(builder, text, {.Prefix = "X1", .PortNodes = {1}, .Parameters = {}});
    REQUIRE_FALSE(added.has_value());
    CHECK_THAT(added.error(), ContainsSubstring("2 ports"));
}

TEST_CASE("A text is checked before a run without a circuit to put it in", "[subcircuit_text]") {
    const Nodalys::SubcircuitText good{.Name = "GOOD", .Ports = {"A", "B"}, .Body = "R1 A B {R}\n"};
    CHECK_FALSE(Nodalys::CheckSubcircuitText(good, {{"R", 1e3}}).has_value());
    const auto unset = Nodalys::CheckSubcircuitText(good, {});
    REQUIRE(unset.has_value());
    CHECK_THAT(*unset, ContainsSubstring("GOOD"));
    const Nodalys::SubcircuitText bad{.Name = "BAD", .Ports = {"A", "B"}, .Body = "Z1 A B 0 ZMOD\n"};
    const auto reason = Nodalys::CheckSubcircuitText(bad, {});
    REQUIRE(reason.has_value());
    CHECK_THAT(*reason, ContainsSubstring("MESFET"));
}
