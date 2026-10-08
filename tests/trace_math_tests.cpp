/**
 * @file    trace_math_tests.cpp
 * @brief   Tests for math channels: compiling expressions, evaluating them and naming their units.
 */

#include "trace_math.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <string>
#include <vector>

namespace {

using Catch::Matchers::WithinAbs;

const GUI::Dimension Seconds{.Seconds = 1};
const GUI::Dimension Volts{.Volts = 1};

/**
 * @struct  TestResult
 * @brief   A small result to evaluate expressions on: three samples, two nodes besides ground, two currents.
 */
struct TestResult {
    std::vector<double> Times = {0.0, 1.0, 2.0};
    std::vector<std::vector<double>> NodeVoltages = {{0.0, 0.0, 0.0}, {5.0, 6.0, 7.0}, {2.0, 2.0, 4.0}};
    std::vector<Core::ComponentTrace> Currents = {{"R1", {1.0, 2.0, 3.0}}, {"Q1.C", {0.5, 0.5, 0.5}}};
};

GUI::MathResult EvaluateOrFail(const std::string &text, const GUI::Dimension x_unit = Seconds) {
    const TestResult result;
    const auto program = GUI::CompileExpression(text);
    if (!program) {
        FAIL(program.error());
    }
    auto evaluated = GUI::EvaluateExpression(*program, result.Times, x_unit, result.NodeVoltages, result.Currents);
    if (!evaluated) {
        FAIL(evaluated.error());
    }
    return std::move(*evaluated);
}

void CheckValues(const std::vector<double> &actual, const std::vector<double> &expected) {
    REQUIRE(actual.size() == expected.size());
    for (std::size_t index = 0; index < actual.size(); ++index) {
        CHECK_THAT(actual[index], WithinAbs(expected[index], 1e-12));
    }
}

} // namespace

TEST_CASE("The voltage between two nodes is a difference in volts", "[trace_math]") {
    const GUI::MathResult difference = EvaluateOrFail("V(1)-V(2)");
    CheckValues(difference.Values, {3.0, 4.0, 3.0});
    CHECK(GUI::FormatUnit(difference.Unit) == "V");
    // The SPICE form, with spaces and lowercase, gives the same
    CheckValues(EvaluateOrFail(" v( 1 , 2 ) ").Values, {3.0, 4.0, 3.0});
}

TEST_CASE("Precedence, parentheses, negation and SPICE suffixes", "[trace_math]") {
    CheckValues(EvaluateOrFail("1+2*3").Values, {7.0, 7.0, 7.0});
    CheckValues(EvaluateOrFail("(1+2)*3").Values, {9.0, 9.0, 9.0});
    CheckValues(EvaluateOrFail("-V(2)+1k/1k").Values, {-1.0, -1.0, -3.0});
    CheckValues(EvaluateOrFail("2e-1*10").Values, {2.0, 2.0, 2.0});
    CheckValues(EvaluateOrFail("8-4-2").Values, {2.0, 2.0, 2.0});
}

TEST_CASE("Units follow the operations", "[trace_math]") {
    CHECK(GUI::FormatUnit(EvaluateOrFail("V(1)*I(R1)").Unit) == "W");
    CHECK(GUI::FormatUnit(EvaluateOrFail("V(1)/I(R1)").Unit) == "Ohm");
    CHECK(GUI::FormatUnit(EvaluateOrFail("I(R1)/V(1)").Unit) == "S");
    CHECK(GUI::FormatUnit(EvaluateOrFail("V(1)/V(2)").Unit).empty());
    CHECK(GUI::FormatUnit(EvaluateOrFail("2*V(1)+1").Unit) == "V");
    CHECK(GUI::FormatUnit(EvaluateOrFail("ddt(V(1))").Unit) == "V/s");
    CHECK(GUI::FormatUnit(EvaluateOrFail("integ(I(R1))").Unit) == "A*s");
    // Mixed units are still evaluated, with no known unit
    const GUI::MathResult mixed = EvaluateOrFail("V(1)+I(R1)");
    CheckValues(mixed.Values, {6.0, 8.0, 10.0});
    CHECK_FALSE(mixed.Unit.Known);
    CHECK(GUI::FormatUnit(mixed.Unit).empty());
}

TEST_CASE("Derivatives and integrals follow the X axis", "[trace_math]") {
    // V(1) rises 1 V per second; I(R1) integrates as a trapezoid
    CheckValues(EvaluateOrFail("ddt(V(1))").Values, {1.0, 1.0, 1.0});
    CheckValues(EvaluateOrFail("integ(I(R1))").Values, {0.0, 1.5, 4.0});
    // Against a swept voltage, the slope of a current is a conductance
    CHECK(GUI::FormatUnit(EvaluateOrFail("ddt(I(R1))", Volts).Unit) == "S");
    CheckValues(EvaluateOrFail("abs(-I(Q1.C))").Values, {0.5, 0.5, 0.5});
    CheckValues(EvaluateOrFail("sqrt(4)").Values, {2.0, 2.0, 2.0});
}

TEST_CASE("Mistakes are reported instead of evaluated", "[trace_math]") {
    CHECK_FALSE(GUI::CompileExpression(""));
    CHECK_FALSE(GUI::CompileExpression("V(1)-"));
    CHECK_FALSE(GUI::CompileExpression("V(1"));
    CHECK_FALSE(GUI::CompileExpression("V(x)"));
    CHECK_FALSE(GUI::CompileExpression("log(V(1))"));
    CHECK_FALSE(GUI::CompileExpression("V(1) V(2)"));
    CHECK_FALSE(GUI::CompileExpression("I()"));

    const TestResult result;
    const auto missing_node = GUI::CompileExpression("V(9)");
    REQUIRE(missing_node);
    CHECK_FALSE(GUI::EvaluateExpression(*missing_node, result.Times, Seconds, result.NodeVoltages, result.Currents));
    const auto missing_current = GUI::CompileExpression("I(R9)");
    REQUIRE(missing_current);
    CHECK_FALSE(GUI::EvaluateExpression(*missing_current, result.Times, Seconds, result.NodeVoltages, result.Currents));
}

TEST_CASE("Operator channels write expressions that compile", "[trace_math]") {
    CHECK(GUI::BuildOperatorExpression("V(1)", GUI::MathOperator::Subtract, "V(2)") == "V(1)-V(2)");
    const std::string power = GUI::BuildOperatorExpression("V(1)", GUI::MathOperator::Multiply, "I(Q1.C)");
    CHECK(GUI::FormatUnit(EvaluateOrFail(power).Unit) == "W");
}
