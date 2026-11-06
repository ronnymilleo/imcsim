/**
 * @file    trace_math_tests.cpp
 * @brief   Tests for math channels: compiling expressions, evaluating them and naming their units.
 */

#include "plots/trace_math.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Catch::Matchers::WithinAbs;

const GUI::Dimension SECONDS{.Seconds = 1};
const GUI::Dimension VOLTS{.Volts = 1};

/**
 * @struct  TestResult
 * @brief   A small result to evaluate expressions on: three samples, two nodes besides ground, two currents.
 */
struct TestResult {
    std::vector<double> Times = {0.0, 1.0, 2.0};
    std::vector<std::vector<double>> NodeVoltages = {{0.0, 0.0, 0.0}, {5.0, 6.0, 7.0}, {2.0, 2.0, 4.0}};
    std::vector<Core::ComponentTrace> Currents = {{"R1", {1.0, 2.0, 3.0}}, {"Q1.C", {0.5, 0.5, 0.5}}};
};

GUI::MathResult EvaluateOrFail(const std::string &text, const GUI::Dimension x_unit = SECONDS) {
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
    CHECK(GUI::FormatUnit(EvaluateOrFail("ddt(I(R1))", VOLTS).Unit) == "S");
    CheckValues(EvaluateOrFail("abs(-I(Q1.C))").Values, {0.5, 0.5, 0.5});
    CheckValues(EvaluateOrFail("sqrt(4)").Values, {2.0, 2.0, 2.0});
}

TEST_CASE("Mistakes are reported instead of evaluated", "[trace_math]") {
    CHECK_FALSE(GUI::CompileExpression(""));
    CHECK_FALSE(GUI::CompileExpression("V(1)-"));
    CHECK_FALSE(GUI::CompileExpression("V(1"));
    CHECK_FALSE(GUI::CompileExpression("V(x y)"));
    CHECK_FALSE(GUI::CompileExpression("V(o%t)"));
    // A node number too large for any circuit is refused rather than wrapped around
    CHECK_FALSE(GUI::CompileExpression("V(99999999999)"));
    CHECK_FALSE(GUI::CompileExpression("log(V(1))"));
    CHECK_FALSE(GUI::CompileExpression("V(1) V(2)"));
    CHECK_FALSE(GUI::CompileExpression("I()"));

    const TestResult result;
    const auto missing_node = GUI::CompileExpression("V(9)");
    REQUIRE(missing_node);
    CHECK_FALSE(GUI::EvaluateExpression(*missing_node, result.Times, SECONDS, result.NodeVoltages, result.Currents));
    const auto missing_current = GUI::CompileExpression("I(R9)");
    REQUIRE(missing_current);
    CHECK_FALSE(GUI::EvaluateExpression(*missing_current, result.Times, SECONDS, result.NodeVoltages, result.Currents));
}

TEST_CASE("A net name stands for the node a label gives it, found when the expression is evaluated", "[trace_math]") {
    const TestResult result;
    const auto program = GUI::CompileExpression("V(OUT) - V(in, 2)");
    REQUIRE(program);
    // Before its nets are resolved, the program refuses to run rather than read ground
    CHECK_FALSE(GUI::EvaluateExpression(*program, result.Times, SECONDS, result.NodeVoltages, result.Currents));

    const GUI::NetResolver find_node = [](const std::string_view net) -> std::optional<int> {
        if (net == "OUT") {
            return 1;
        }
        if (net == "in") {
            return 2;
        }
        return std::nullopt;
    };
    const auto resolved = GUI::ResolveNetNames(*program, find_node);
    REQUIRE(resolved);
    const auto evaluated =
        GUI::EvaluateExpression(*resolved, result.Times, SECONDS, result.NodeVoltages, result.Currents);
    REQUIRE(evaluated);
    // V(1) minus V(2, 2), which is zero
    CheckValues(evaluated->Values, {5.0, 6.0, 7.0});

    const auto missing = GUI::ResolveNetNames(*GUI::CompileExpression("V(GONE)"), find_node);
    REQUIRE_FALSE(missing);
    CHECK(missing.error().contains("GONE"));
}

TEST_CASE("Operator channels write expressions that compile", "[trace_math]") {
    CHECK(GUI::BuildOperatorExpression("V(1)", GUI::MathOperator::SUBTRACT, "V(2)") == "V(1)-V(2)");
    const std::string power = GUI::BuildOperatorExpression("V(1)", GUI::MathOperator::MULTIPLY, "I(Q1.C)");
    CHECK(GUI::FormatUnit(EvaluateOrFail(power).Unit) == "W");
}

namespace {

/**
 * @brief   An AC sweep at two frequencies: V(1) is 1 V at 0 degrees, V(2) is 0.5 V lagging 90 degrees, and I(R1) is
 *          2 A leading 45 degrees.
 */
Core::ACSweep MakeACSweep() {
    const double half = 20.0 * std::log10(0.5);
    const double two = 20.0 * std::log10(2.0);
    return {.Frequencies = {1.0, 1000.0},
            .NodeMagnitudesDecibels = {{}, {0.0, 0.0}, {half, half}},
            .NodePhasesDegrees = {{}, {0.0, 0.0}, {-90.0, -90.0}},
            .CurrentMagnitudesDecibels = {{"R1", {two, two}}},
            .CurrentPhasesDegrees = {{"R1", {45.0, 45.0}}}};
}

GUI::ACMathResult EvaluateACOrFail(const std::string &text, const Core::ACSweep &sweep = MakeACSweep()) {
    const auto program = GUI::CompileExpression(text);
    if (!program) {
        FAIL(program.error());
    }
    auto evaluated = GUI::EvaluateACExpression(*program, sweep);
    if (!evaluated) {
        FAIL(evaluated.error());
    }
    return std::move(*evaluated);
}

void CheckValuesWithin(const std::vector<double> &actual, const std::vector<double> &expected, const double margin) {
    REQUIRE(actual.size() == expected.size());
    for (std::size_t index = 0; index < actual.size(); ++index) {
        CHECK_THAT(actual[index], WithinAbs(expected[index], margin));
    }
}

} // namespace

TEST_CASE("On an AC sweep, math works on the complex values of the traces", "[trace_math]") {
    // 1 - (-0.5j) is 1 + 0.5j: its magnitude is sqrt(1.25) and its phase atan(0.5)
    const GUI::ACMathResult difference = EvaluateACOrFail("V(1)-V(2)");
    const double magnitude = 20.0 * std::log10(std::sqrt(1.25));
    const double phase = std::atan(0.5) * 180.0 / std::numbers::pi;
    CheckValuesWithin(difference.MagnitudesDecibels, {magnitude, magnitude}, 1e-9);
    CheckValuesWithin(difference.PhasesDegrees, {phase, phase}, 1e-9);
    CHECK(GUI::FormatUnit(difference.Unit) == "V");

    // A ratio of two nodes is the gain and phase shift between them, without a unit
    const GUI::ACMathResult gain = EvaluateACOrFail("V(2)/V(1)");
    CheckValuesWithin(gain.MagnitudesDecibels, {-6.0206, -6.0206}, 1e-4);
    CheckValuesWithin(gain.PhasesDegrees, {-90.0, -90.0}, 1e-9);
    CHECK(GUI::FormatUnit(gain.Unit).empty());

    // An impedance divides a voltage by a current, phases included
    const GUI::ACMathResult impedance = EvaluateACOrFail("V(1)/I(R1)");
    CheckValuesWithin(impedance.MagnitudesDecibels, {-6.0206, -6.0206}, 1e-4);
    CheckValuesWithin(impedance.PhasesDegrees, {-45.0, -45.0}, 1e-9);
    CHECK(GUI::FormatUnit(impedance.Unit) == "Ohm");

    // abs() keeps the magnitude and drops the phase
    CheckValuesWithin(EvaluateACOrFail("abs(V(2))").PhasesDegrees, {0.0, 0.0}, 1e-9);
}

TEST_CASE("On an AC sweep, a derivative multiplies by j*omega and an integral divides by it", "[trace_math]") {
    // At 1 Hz omega is 2*pi, at 1 kHz a thousand times more, 60 dB higher
    const double one_hertz = 20.0 * std::log10(2.0 * std::numbers::pi);
    const GUI::ACMathResult derivative = EvaluateACOrFail("ddt(V(1))");
    CheckValuesWithin(derivative.MagnitudesDecibels, {one_hertz, one_hertz + 60.0}, 1e-9);
    CheckValuesWithin(derivative.PhasesDegrees, {90.0, 90.0}, 1e-9);
    CHECK(GUI::FormatUnit(derivative.Unit) == "V/s");

    const GUI::ACMathResult integral = EvaluateACOrFail("integ(V(1))");
    CheckValuesWithin(integral.MagnitudesDecibels, {-one_hertz, -one_hertz - 60.0}, 1e-9);
    CheckValuesWithin(integral.PhasesDegrees, {-90.0, -90.0}, 1e-9);
    CHECK(GUI::FormatUnit(integral.Unit) == "V*s");

    // At 0 Hz the integral has no finite value, which the plot skips
    Core::ACSweep at_zero = MakeACSweep();
    at_zero.Frequencies.front() = 0.0;
    const GUI::ACMathResult unbounded = EvaluateACOrFail("integ(V(1))", at_zero);
    CHECK(std::isnan(unbounded.MagnitudesDecibels.front()));
    CHECK(std::isnan(unbounded.PhasesDegrees.front()));
    CHECK(std::isfinite(unbounded.MagnitudesDecibels.back()));
}

TEST_CASE("On an AC sweep, ground reads zero and a missing trace is reported", "[trace_math]") {
    CheckValuesWithin(EvaluateACOrFail("V(1,0)").MagnitudesDecibels, {0.0, 0.0}, 1e-12);
    // Zero has no magnitude in decibels
    CHECK(std::isnan(EvaluateACOrFail("V(0)").MagnitudesDecibels.front()));

    const Core::ACSweep sweep = MakeACSweep();
    const auto missing_node = GUI::EvaluateACExpression(*GUI::CompileExpression("V(7)"), sweep);
    REQUIRE_FALSE(missing_node);
    CHECK(missing_node.error().contains("node 7"));
    const auto missing_current = GUI::EvaluateACExpression(*GUI::CompileExpression("I(C9)"), sweep);
    REQUIRE_FALSE(missing_current);
    CHECK(missing_current.error().contains("I(C9)"));
}
