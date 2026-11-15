/**
 * @file    expression_tests.cpp
 * @brief   Tests of the expression language of Nodalys: its numbers, operators, precedence and functions as ngspice's
 *          behavioral source reads them, its refusals, and its derivatives against finite differences.
 * @details The expected values were taken from ngspice 47, which runs `B1 1 0 V=<expression>` for each of them; where
 *          the C library would give another answer, such as 0 for a negative base to a fractional power, the test says
 *          so.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "expression/expression_math.h"
#include <algorithm>
#include <cmath>
#include <nodalys/nodalys.h>
#include <string>
#include <vector>

using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

Nodalys::Expression Parse(const std::string_view text) {
    auto expression = Nodalys::Expression::Parse(text);
    INFO("expression: " << text);
    REQUIRE(expression.has_value());
    return *expression;
}

Nodalys::ExpressionResult Evaluate(const std::string_view text, const std::vector<double> &inputs = {},
                                   const double time_seconds = 0.0) {
    const auto result = Parse(text).Evaluate(inputs, time_seconds);
    INFO("expression: " << text);
    REQUIRE(result.has_value());
    return *result;
}

double ValueOf(const std::string_view text, const std::vector<double> &inputs = {}, const double time_seconds = 0.0) {
    return Evaluate(text, inputs, time_seconds).Value;
}

std::string ParseError(const std::string_view text) {
    const auto expression = Nodalys::Expression::Parse(text);
    INFO("expression: " << text);
    REQUIRE_FALSE(expression.has_value());
    return expression.error();
}

std::string EvaluationError(const std::string_view text, const std::vector<double> &inputs = {}) {
    const auto result = Parse(text).Evaluate(inputs, 0.0);
    INFO("expression: " << text);
    REQUIRE_FALSE(result.has_value());
    return result.error();
}

} // namespace

TEST_CASE("Numbers take the SPICE scale suffixes, and the letters after them are ignored", "[nodalys][expression]") {
    CHECK_THAT(ValueOf("4.7k"), WithinRel(4700.0, 1e-12));
    CHECK_THAT(ValueOf("1meg"), WithinRel(1e6, 1e-12));
    CHECK_THAT(ValueOf("2MEG"), WithinRel(2e6, 1e-12));
    // M is milli in SPICE, whatever the case
    CHECK_THAT(ValueOf("2M"), WithinRel(2e-3, 1e-12));
    CHECK_THAT(ValueOf("10n"), WithinRel(10e-9, 1e-12));
    CHECK_THAT(ValueOf("1u"), WithinRel(1e-6, 1e-12));
    CHECK_THAT(ValueOf("2mil"), WithinRel(50.8e-6, 1e-12));
    CHECK_THAT(ValueOf("1e-3"), WithinRel(1e-3, 1e-12));
    CHECK_THAT(ValueOf("1.5E3"), WithinRel(1500.0, 1e-12));
    CHECK_THAT(ValueOf(".5"), WithinRel(0.5, 1e-12));
    CHECK_THAT(ValueOf("5."), WithinRel(5.0, 1e-12));
    CHECK_THAT(ValueOf("5V"), WithinRel(5.0, 1e-12));
    CHECK_THAT(ValueOf("1kohm"), WithinRel(1000.0, 1e-12));
    // The exponent letter is not a suffix when no digits follow it
    CHECK_THAT(ValueOf("2*e"), WithinRel(2.0 * 2.718281828459045, 1e-12));
}

TEST_CASE("pi and e are constants, time and temper are variables", "[nodalys][expression]") {
    CHECK_THAT(ValueOf("pi"), WithinRel(3.141592653589793, 1e-15));
    CHECK_THAT(ValueOf("Pi"), WithinRel(3.141592653589793, 1e-15));
    CHECK_THAT(ValueOf("e"), WithinRel(2.718281828459045, 1e-15));
    CHECK(ValueOf("time", {}, 2.5e-3) == 2.5e-3);
    CHECK(ValueOf("TIME*2", {}, 1.0) == 2.0);
    // Nodalys simulates at 27 C only, which is what ngspice reads without a .temp card
    CHECK(ValueOf("temper") == 27.0);
    CHECK(Parse("time + 1").IsTimeDependent());
    CHECK_FALSE(Parse("temper + 1").IsTimeDependent());
}

TEST_CASE("Operators bind and group as in ngspice", "[nodalys][expression]") {
    // The sign binds looser than the power
    CHECK(ValueOf("-2^2") == -4.0);
    CHECK(ValueOf("-2**2") == -4.0);
    CHECK(ValueOf("-(-2)^2") == -4.0);
    // Powers group from the left, unlike mathematics
    CHECK(ValueOf("2^3^2") == 64.0);
    CHECK(ValueOf("2**3**2") == 64.0);
    CHECK(ValueOf("2^-1") == 0.5);
    CHECK(ValueOf("2*3^2") == 18.0);
    CHECK(ValueOf("2*-3") == -6.0);
    CHECK(ValueOf("1+-3") == -2.0);
    CHECK(ValueOf("10/4/5") == 0.5);
    CHECK(ValueOf("1-2-3") == -4.0);
    // Not binds tighter than the arithmetic: (!0) + 1
    CHECK(ValueOf("!0+1") == 2.0);
    CHECK(ValueOf("!1+1") == 1.0);
    CHECK(ValueOf("+3") == 3.0);
}

TEST_CASE("Comparisons and logic give 1 or 0, and and binds tighter than or", "[nodalys][expression]") {
    CHECK(ValueOf("1<2") == 1.0);
    CHECK(ValueOf("2<1") == 0.0);
    CHECK(ValueOf("1<=1") == 1.0);
    CHECK(ValueOf("2>=3") == 0.0);
    CHECK(ValueOf("1==1") == 1.0);
    CHECK(ValueOf("1!=1") == 0.0);
    CHECK(ValueOf("1<>1") == 0.0);
    CHECK(ValueOf("1<>2") == 1.0);
    CHECK(ValueOf("1+2>3") == 0.0);
    CHECK(ValueOf("1<2&&2<3") == 1.0);
    // Any value but 0 is true
    CHECK(ValueOf("0.5&&2") == 1.0);
    CHECK(ValueOf("2||0") == 1.0);
    CHECK(ValueOf("!5") == 0.0);
    CHECK(ValueOf("!0") == 1.0);
    // With the same precedence this would read (1||0)&&0, which is 0
    CHECK(ValueOf("1||0&&0") == 1.0);
    CHECK(ValueOf("0&&0||1") == 1.0);
}

TEST_CASE("The ternary takes the branch the condition picks, groups to the right and evaluates nothing else",
          "[nodalys][expression]") {
    CHECK(ValueOf("1?2:3") == 2.0);
    CHECK(ValueOf("0?2:3") == 3.0);
    CHECK(ValueOf("0.5?1:2") == 1.0);
    CHECK(ValueOf("-1?1:2") == 1.0);
    CHECK(ValueOf("1?2:3?4:5") == 2.0);
    CHECK(ValueOf("0?2:0?4:5") == 5.0);
    CHECK(ValueOf("0?1:2+3") == 5.0);
    CHECK(ValueOf("1?2:3*4") == 2.0);
    CHECK(ValueOf("v(1)>0?10:20", {1.0}) == 10.0);
    CHECK(ValueOf("v(1)>0?10:20", {-1.0}) == 20.0);
    // The branch not taken is not evaluated, so a guard keeps its domain error away
    CHECK(ValueOf("v(1)>0 ? sqrt(v(1)) : 7", {-1.3}) == 7.0);
    CHECK(ValueOf("v(1)>0 ? ln(v(1)) : 7", {-1.3}) == 7.0);
    CHECK_THAT(ValueOf("v(1)>0 ? sqrt(v(1)) : 7", {4.0}), WithinAbs(2.0, 1e-15));
}

TEST_CASE("The functions give the values ngspice gives", "[nodalys][expression]") {
    CHECK_THAT(ValueOf("abs(-3)"), WithinAbs(3.0, 1e-15));
    CHECK_THAT(ValueOf("sqrt(4)"), WithinAbs(2.0, 1e-15));
    CHECK_THAT(ValueOf("exp(1)"), WithinRel(2.718281828459045, 1e-15));
    CHECK_THAT(ValueOf("tan(1)"), WithinRel(1.5574077246549023, 1e-14));
    CHECK_THAT(ValueOf("asin(0.5)"), WithinRel(0.5235987755982989, 1e-14));
    CHECK_THAT(ValueOf("acos(0.5)"), WithinRel(1.0471975511965979, 1e-14));
    CHECK_THAT(ValueOf("atan(1)"), WithinRel(0.7853981633974483, 1e-14));
    CHECK_THAT(ValueOf("sinh(1)"), WithinRel(1.1752011936438014, 1e-14));
    CHECK_THAT(ValueOf("cosh(1)"), WithinRel(1.5430806348152437, 1e-14));
    CHECK_THAT(ValueOf("tanh(1)"), WithinRel(0.7615941559557649, 1e-14));
    CHECK_THAT(ValueOf("asinh(1)"), WithinRel(0.881373587019543, 1e-14));
    CHECK_THAT(ValueOf("acosh(2)"), WithinRel(1.3169578969248166, 1e-14));
    CHECK_THAT(ValueOf("atanh(0.5)"), WithinRel(0.5493061443340549, 1e-14));
    CHECK_THAT(ValueOf("sin(pi/2)"), WithinAbs(1.0, 1e-15));
    CHECK_THAT(ValueOf("cos(pi)"), WithinAbs(-1.0, 1e-15));
    CHECK(ValueOf("floor(-1.5)") == -2.0);
    CHECK(ValueOf("ceil(-1.5)") == -1.0);
    CHECK(ValueOf("min(1, 2)") == 1.0);
    CHECK(ValueOf("max(1, 2)") == 2.0);
    CHECK(ValueOf("min(-3, 2)") == -3.0);
    CHECK(ValueOf("TANH(0)") == 0.0);
    CHECK(ValueOf("tanh (0)") == 0.0);
}

TEST_CASE("log is the natural logarithm, as in ngspice, and log10 the decimal one", "[nodalys][expression]") {
    CHECK_THAT(ValueOf("log(100)"), WithinRel(4.605170185988092, 1e-14));
    CHECK_THAT(ValueOf("ln(10)"), WithinRel(2.302585092994046, 1e-14));
    CHECK_THAT(ValueOf("log10(100)"), WithinAbs(2.0, 1e-14));
    // The logarithm of 0 is the most negative value ngspice returns, and of a negative number an error
    CHECK(ValueOf("ln(0)") == -1e99);
    CHECK(ValueOf("log10(0)") == -1e99);
    CHECK_THAT(EvaluationError("ln(-1)"), ContainsSubstring("ln(-1) is out of range"));
    CHECK_THAT(EvaluationError("log10(-2)"), ContainsSubstring("log10"));
}

TEST_CASE("The steps and the roundings take ngspice's values at their jumps", "[nodalys][expression]") {
    // u is half way through its jump at 0
    CHECK(ValueOf("u(-1)") == 0.0);
    CHECK(ValueOf("u(0)") == 0.5);
    CHECK(ValueOf("u(1)") == 1.0);
    CHECK(ValueOf("uramp(-1)") == 0.0);
    CHECK(ValueOf("uramp(0)") == 0.0);
    CHECK(ValueOf("uramp(2)") == 2.0);
    // u2 ramps from 0 to 1 between 0 and 1
    CHECK(ValueOf("u2(-1)") == 0.0);
    CHECK(ValueOf("u2(0.5)") == 0.5);
    CHECK(ValueOf("u2(2)") == 1.0);
    CHECK(ValueOf("sgn(-3)") == -1.0);
    CHECK(ValueOf("sgn(0)") == 0.0);
    CHECK(ValueOf("sgn(0.0001)") == 1.0);
    // nint rounds a half to the even integer
    CHECK(ValueOf("nint(2.4)") == 2.0);
    CHECK(ValueOf("nint(2.5)") == 2.0);
    CHECK(ValueOf("nint(3.5)") == 4.0);
    CHECK(ValueOf("nint(-2.5)") == -2.0);
    CHECK(ValueOf("nint(0.5)") == 0.0);
}

TEST_CASE("Powers follow ngspice for a negative base, a base of 0 and the signed pwr", "[nodalys][expression]") {
    CHECK(ValueOf("2^3") == 8.0);
    CHECK(ValueOf("2**3") == 8.0);
    CHECK_THAT(ValueOf("2^0.5"), WithinRel(1.4142135623730951, 1e-14));
    CHECK(ValueOf("(-2)^3") == -8.0);
    CHECK(ValueOf("(-2)^2") == 4.0);
    CHECK(ValueOf("(-2)^-1") == -0.5);
    // A real number has no fractional power of a negative one, and ngspice reads it as 0
    CHECK(ValueOf("(-2)^0.5") == 0.0);
    CHECK(ValueOf("(-1.3)^2.5") == 0.0);
    CHECK(ValueOf("0^0") == 1.0);
    CHECK(ValueOf("0^2") == 0.0);
    CHECK_THAT(EvaluationError("0^-1"), ContainsSubstring("out of range"));
    // pow() is the same, except that a base of 0 gives 0 for every exponent
    CHECK(ValueOf("pow(-2, 3)") == -8.0);
    CHECK(ValueOf("pow(-2, 0.5)") == 0.0);
    CHECK(ValueOf("pow(0, 0)") == 0.0);
    CHECK(ValueOf("pow(0, -1)") == 0.0);
    CHECK_THAT(ValueOf("pow(2, 0.5)"), WithinRel(1.4142135623730951, 1e-14));
    // pwr() keeps the sign of the base: its magnitude to the exponent
    CHECK(ValueOf("pwr(-2, 3)") == -8.0);
    CHECK(ValueOf("pwr(-2, 2)") == -4.0);
    CHECK_THAT(ValueOf("pwr(-2, 0.5)"), WithinRel(-1.4142135623730951, 1e-14));
    CHECK(ValueOf("pwr(0, 0)") == 1.0);
    CHECK(ValueOf("pwr(0, 0.5)") == 0.0);
    CHECK_THAT(EvaluationError("pwr(0, -1)"), ContainsSubstring("out of range"));
}

TEST_CASE("A division by zero, an overflow and a domain error are handled as ngspice handles them",
          "[nodalys][expression]") {
    // The first Newton iteration has every node at 0 V, where a quotient of voltages must still have a value
    CHECK(ValueOf("1/0") == 1e32);
    CHECK(ValueOf("-1/0") == -1e32);
    CHECK(ValueOf("0/0") == 0.0);
    CHECK_THAT(ValueOf("0.7/0"), WithinRel(7e31, 1e-12));
    // exp stops at the largest value, where ngspice gives 1e99
    CHECK(ValueOf("exp(1000)") == 1e99);
    CHECK_THAT(ValueOf("exp(200)"), WithinRel(std::exp(200.0), 1e-12));
    CHECK(ValueOf("exp(300)") == 1e99);
    CHECK(ValueOf("exp(-1000)") == 0.0);
    CHECK_THAT(EvaluationError("sqrt(-1)"), ContainsSubstring("sqrt(-1) is out of range"));
    CHECK_THAT(EvaluationError("asin(2)"), ContainsSubstring("asin"));
    CHECK_THAT(EvaluationError("acos(-2)"), ContainsSubstring("acos"));
    CHECK_THAT(EvaluationError("acosh(0.5)"), ContainsSubstring("acosh"));
    CHECK_THAT(EvaluationError("atanh(1)"), ContainsSubstring("atanh"));
    CHECK_THAT(EvaluationError("1e200*1e200"), ContainsSubstring("out of range"));
    // The message names the part, so the user can find what failed
    CHECK_THAT(EvaluationError("v(out) + sqrt(v(out))", {-4.0}), ContainsSubstring("sqrt(-4)"));
}

TEST_CASE("References name voltages and currents, and the same one written twice is one input",
          "[nodalys][expression]") {
    const Nodalys::Expression expression = Parse("v(a) * V(A) + v(a, b) - i(Vsense) + I(vsense) + v(b)");
    const std::vector<Nodalys::ExpressionInput> &inputs = expression.GetInputs();
    REQUIRE(inputs.size() == 4);
    CHECK(inputs[0].Type == Nodalys::ExpressionInput::Kind::VOLTAGE);
    CHECK(inputs[0].First == "a");
    CHECK(inputs[0].Second.empty());
    CHECK(inputs[1].First == "a");
    CHECK(inputs[1].Second == "b");
    CHECK(inputs[2].Type == Nodalys::ExpressionInput::Kind::CURRENT);
    CHECK(inputs[2].First == "Vsense");
    CHECK(inputs[3].First == "b");
    // Every reference is kept with its place in the text, which a caller uses to rewrite the names
    const std::vector<Nodalys::ExpressionReference> &references = expression.GetReferences();
    REQUIRE(references.size() == 6);
    CHECK(references[0].Start == 0);
    CHECK(references[0].Length == 4);
    CHECK(references[1].Start == 7);
    CHECK(references[2].Input.First == "a");
    CHECK(references[2].Input.Second == "b");
}

TEST_CASE("A voltage between two nodes and the current of a source are inputs of their own", "[nodalys][expression]") {
    // v(a, b) is read as a difference by whoever binds it; here it is one input
    const Nodalys::ExpressionResult result = Evaluate("v(a, b) * 2 + i(vs) * 3", {1.5, 2.0});
    CHECK_THAT(result.Value, WithinAbs(9.0, 1e-15));
    REQUIRE(result.Partials.size() == 2);
    CHECK(result.Partials[0] == 2.0);
    CHECK(result.Partials[1] == 3.0);
    // The names of a source may hold the characters of the names imcsim gives its probes
    const Nodalys::Expression probe = Parse("i(__probe_1_X1.R1-2) * 5");
    CHECK(probe.GetInputs()[0].First == "__probe_1_X1.R1-2");
    CHECK_THAT(EvaluationError("v(1) * 2", {}), ContainsSubstring("reads 1 inputs, not 0"));
}

TEST_CASE("An expression is refused with a message that names what Nodalys does not have", "[nodalys][expression]") {
    SECTION("A function it does not know") {
        CHECK_THAT(ParseError("foo(1)"), ContainsSubstring("\"foo\" is not a function Nodalys knows"));
        CHECK_THAT(ParseError("1 + table(1)"), ContainsSubstring("table"));
    }
    SECTION("limit is a Monte Carlo function in ngspice, not a clamp") {
        const std::string message = ParseError("limit(v(1), -1, 1)");
        CHECK_THAT(message, ContainsSubstring("limit()"));
        CHECK_THAT(message, ContainsSubstring("Monte Carlo"));
        CHECK_THAT(message, ContainsSubstring("min(max(x, low), high)"));
    }
    SECTION("atan2 is not in ngspice either") {
        const std::string message = ParseError("atan2(1, 2)");
        CHECK_THAT(message, ContainsSubstring("atan2()"));
        CHECK_THAT(message, ContainsSubstring("atan(y / x)"));
    }
    SECTION("The random functions") {
        for (const std::string_view name : {"gauss", "agauss", "unif", "aunif", "rnd"}) {
            const std::string message = ParseError(std::string(name) + "(1, 2)");
            INFO(name);
            CHECK_THAT(message, ContainsSubstring(std::string(name)));
            CHECK_THAT(message, ContainsSubstring("random numbers"));
        }
    }
    SECTION("hertz, the frequency") {
        CHECK_THAT(ParseError("v(1) * hertz"), ContainsSubstring("hertz"));
    }
    SECTION("A name that is none of the things an expression holds") {
        CHECK_THAT(ParseError("boltz * 2"), ContainsSubstring("\"boltz\" is not a number"));
        CHECK_THAT(ParseError("sin + 1"), ContainsSubstring("takes its operands in parentheses"));
        CHECK_THAT(ParseError("v + 1"), ContainsSubstring("followed by the name it reads"));
    }
    SECTION("The wrong number of operands") {
        CHECK_THAT(ParseError("min(1)"), ContainsSubstring("min() takes 2 operands, not 1"));
        CHECK_THAT(ParseError("max(1, 2, 3)"), ContainsSubstring("max() takes 2 operands, not 3"));
        CHECK_THAT(ParseError("sin(1, 2)"), ContainsSubstring("sin() takes 1 operand, not 2"));
        CHECK_THAT(ParseError("sin()"), ContainsSubstring("sin() takes 1 operand, not 0"));
    }
    SECTION("Operators and characters ngspice has no B-source meaning for") {
        CHECK_THAT(ParseError("10 % 3"), ContainsSubstring("\"%\" cannot be used here"));
        CHECK_THAT(ParseError("1 = 1"), ContainsSubstring("\"=\""));
        CHECK_THAT(ParseError("1 & 1"), ContainsSubstring("\"&\""));
    }
}

TEST_CASE("Mistakes in the text are told where they are", "[nodalys][expression]") {
    CHECK_THAT(ParseError(""), ContainsSubstring("empty"));
    CHECK_THAT(ParseError("   "), ContainsSubstring("empty"));
    CHECK_THAT(ParseError("(1 + 2"), ContainsSubstring("not closed"));
    CHECK_THAT(ParseError("1 + 2)"), ContainsSubstring("closes no parenthesis"));
    CHECK_THAT(ParseError("1 +"), ContainsSubstring("ends where an operand is expected"));
    CHECK_THAT(ParseError("1 2"), ContainsSubstring("\"2\" cannot be used here"));
    CHECK_THAT(ParseError("1 ? 2"), ContainsSubstring("needs the \":\""));
    CHECK_THAT(ParseError("sin(1"), ContainsSubstring("not closed"));
    CHECK_THAT(ParseError("v()"), ContainsSubstring("one or two node names"));
    CHECK_THAT(ParseError("v(a, b, c)"), ContainsSubstring("one or two node names"));
    CHECK_THAT(ParseError("v(a b)"), ContainsSubstring("one or two node names"));
    CHECK_THAT(ParseError("v(a + b)"), ContainsSubstring("one or two node names"));
    CHECK_THAT(ParseError("v(abs(1))"), ContainsSubstring("v() takes the name of a node, not an expression"));
    CHECK_THAT(ParseError("i(a, b)"), ContainsSubstring("name of one source"));
    CHECK_THAT(ParseError("i(abs(1))"), ContainsSubstring("not an expression"));
    CHECK_THAT(ParseError("1.2.3"), ContainsSubstring("cannot be used here"));
    // A text that nests without end is refused, not followed down the stack
    CHECK_THAT(ParseError(std::string(500, '(') + "1" + std::string(500, ')')), ContainsSubstring("nested too deeply"));
    CHECK_THAT(ParseError(std::string(500, '-') + "1"), ContainsSubstring("nested too deeply"));
}

TEST_CASE("The derivative of every operation matches a finite difference", "[nodalys][expression]") {
    struct Case {
        std::string Text;
        std::vector<std::vector<double>> Points;
    };
    const std::vector<Case> cases{
        {"v(1) * v(2)", {{1.5, -2.0}, {0.3, 0.7}}},
        {"v(1) / v(2)", {{1.5, -2.0}, {0.3, 0.7}}},
        {"v(1) + 3 * v(2) - 2", {{1.5, -2.0}}},
        {"-v(1) * v(1)", {{1.5, 0.0}}},
        {"1m * tanh(v(1) / 26m)", {{0.01, 0.0}, {-0.05, 0.0}}},
        {"exp(v(1)) * v(2)", {{0.5, 2.0}, {-1.0, 1.0}}},
        {"ln(v(1)) + log(v(2))", {{2.0, 3.0}}},
        {"log10(v(1))", {{2.0, 0.0}}},
        {"sqrt(v(1))", {{2.0, 0.0}, {0.25, 0.0}}},
        {"v(1) ^ 3", {{1.5, 0.0}, {-1.5, 0.0}}},
        {"v(1) ^ v(2)", {{1.5, 2.5}}},
        {"v(1) ** 0.5", {{2.0, 0.0}}},
        {"pow(v(1), v(2))", {{1.5, 2.5}}},
        {"pow(v(1), 3)", {{-2.0, 0.0}}},
        {"pwr(v(1), 2.5)", {{1.5, 0.0}, {-1.5, 0.0}}},
        {"pwr(v(1), v(2))", {{1.5, 2.5}, {-1.5, 3.0}}},
        {"sin(v(1)) * cos(v(2))", {{0.7, 1.9}}},
        {"tan(v(1))", {{0.7, 0.0}}},
        {"atan(v(1) / v(2))", {{0.7, 1.9}}},
        {"asin(v(1) / 4)", {{0.7, 0.0}}},
        {"acos(v(1) / 4)", {{0.7, 0.0}}},
        {"sinh(v(1)) + cosh(v(2))", {{0.7, 1.9}}},
        {"asinh(v(1))", {{0.7, 0.0}}},
        {"acosh(v(1) + 2)", {{0.7, 0.0}}},
        {"atanh(v(1) / 4)", {{0.7, 0.0}}},
        {"abs(v(1))", {{0.7, 0.0}, {-0.7, 0.0}}},
        {"min(v(1), v(2))", {{0.7, 1.9}, {2.7, 1.9}}},
        {"max(v(1), v(2))", {{0.7, 1.9}, {2.7, 1.9}}},
        {"uramp(v(1))", {{0.7, 0.0}, {-0.7, 0.0}}},
        {"u2(v(1) / 4)", {{0.7, 0.0}, {-0.7, 0.0}, {6.0, 0.0}}},
        {"v(1) > 0 ? v(1) * v(2) : -v(2)", {{0.7, 1.9}, {-0.7, 1.9}}},
        {"i(vs) * 1k + v(1)", {{0.7, 1.9}}},
        {"v(1) * time", {{0.7, 0.0}}},
        {"sgn(v(1)) * v(1) * v(1)", {{0.7, 0.0}, {-0.7, 0.0}}},
        {"floor(v(1)) + v(1)", {{0.7, 0.0}}},
        {"(v(1) < 1) * v(2) + (v(1) >= 1) * v(2) * 2", {{0.7, 1.9}, {1.5, 1.9}}},
    };
    for (const Case &entry : cases) {
        const Nodalys::Expression expression = Parse(entry.Text);
        const std::size_t input_count = expression.GetInputs().size();
        for (std::vector<double> point : entry.Points) {
            point.resize(input_count);
            const Nodalys::ExpressionResult analytic = Evaluate(entry.Text, point, 0.3);
            for (std::size_t input = 0; input < input_count; ++input) {
                const double step = 1e-6 * std::max(1.0, std::fabs(point[input]));
                std::vector<double> above = point;
                std::vector<double> below = point;
                above[input] += step;
                below[input] -= step;
                const double numeric =
                    (Evaluate(entry.Text, above, 0.3).Value - Evaluate(entry.Text, below, 0.3).Value) / (2.0 * step);
                INFO(entry.Text << " at input " << input << " of point " << point[0]);
                CHECK_THAT(analytic.Partials[input], WithinAbs(numeric, 1e-6 * std::max(1.0, std::fabs(numeric))));
            }
        }
    }
}

TEST_CASE("The jumps and corners have the slope of the side the value is on, and no smoothing",
          "[nodalys][expression]") {
    // abs and the ramp have a corner at 0, the steps and the roundings a jump
    CHECK(Evaluate("abs(v(1))", {2.0}).Partials[0] == 1.0);
    CHECK(Evaluate("abs(v(1))", {-2.0}).Partials[0] == -1.0);
    CHECK(Evaluate("abs(v(1))", {0.0}).Partials[0] == 0.0);
    CHECK(Evaluate("uramp(v(1))", {2.0}).Partials[0] == 1.0);
    CHECK(Evaluate("uramp(v(1))", {-2.0}).Partials[0] == 0.0);
    // On a corner ngspice takes the mean of the slopes of its two sides
    CHECK(Evaluate("uramp(v(1))", {0.0}).Partials[0] == 0.5);
    CHECK(Evaluate("u2(v(1))", {0.0}).Partials[0] == 0.5);
    CHECK(Evaluate("u2(v(1))", {1.0}).Partials[0] == 0.5);
    CHECK(Evaluate("u2(v(1))", {0.5}).Partials[0] == 1.0);
    CHECK(Evaluate("u2(v(1))", {2.0}).Partials[0] == 0.0);
    // A tie of min and max takes the second operand, and pwr has no slope at 0
    CHECK(Evaluate("min(v(1), 0)", {0.0}).Partials[0] == 0.0);
    CHECK(Evaluate("max(v(1), 0)", {0.0}).Partials[0] == 0.0);
    CHECK(Evaluate("pwr(v(1), 1)", {0.0}).Partials[0] == 0.0);
    CHECK(Evaluate("v(1) ^ 1", {0.0}).Partials[0] == 1.0);
    CHECK(Evaluate("pow(v(1), 1)", {0.0}).Partials[0] == 1.0);
    CHECK(Evaluate("u(v(1))", {2.0}).Partials[0] == 0.0);
    CHECK(Evaluate("sgn(v(1))", {2.0}).Partials[0] == 0.0);
    CHECK(Evaluate("nint(v(1))", {2.2}).Partials[0] == 0.0);
    // min and max take the slope of the operand that wins, and a clamp is flat past its limits
    CHECK(Evaluate("min(v(1), v(2))", {1.0, 2.0}).Partials == std::vector<double>{1.0, 0.0});
    CHECK(Evaluate("max(v(1), v(2))", {1.0, 2.0}).Partials == std::vector<double>{0.0, 1.0});
    CHECK(Evaluate("min(max(v(1), -1), 1)", {0.4}).Partials[0] == 1.0);
    CHECK(Evaluate("min(max(v(1), -1), 1)", {3.0}).Partials[0] == 0.0);
    CHECK(Evaluate("min(max(v(1), -1), 1)", {-3.0}).Partials[0] == 0.0);
    // The comparisons give a value with no slope, and the ternary the slope of its branch
    CHECK(Evaluate("v(1) > 0", {1.0}).Partials[0] == 0.0);
    CHECK(Evaluate("v(1) > 0 ? 5 * v(1) : 7 * v(1)", {1.0}).Partials[0] == 5.0);
    CHECK(Evaluate("v(1) > 0 ? 5 * v(1) : 7 * v(1)", {-1.0}).Partials[0] == 7.0);
    // exp is flat past its largest value, which is what the clamp makes of it
    CHECK(Evaluate("exp(v(1))", {1000.0}).Partials[0] == 0.0);
    // A slope that is not finite is a large number, so the first iteration, at 0 V, can still be linearized; it is
    // small enough to leave the matrix solvable
    CHECK(Evaluate("sqrt(v(1))", {0.0}).Partials[0] == 1e12);
    CHECK(Evaluate("ln(v(1))", {0.0}).Partials[0] == 1e12);
    CHECK(Evaluate("v(1) / v(2)", {0.0, 0.0}).Partials == std::vector<double>{0.0, 0.0});
}

TEST_CASE("The operations on their own give the value and both partials", "[nodalys][expression]") {
    const auto sum = Nodalys::ApplyOperation(Nodalys::Operation::SUBTRACT, 5.0, 3.0);
    REQUIRE(sum.has_value());
    CHECK(sum->Value == 2.0);
    CHECK(sum->PartialFirst == 1.0);
    CHECK(sum->PartialSecond == -1.0);
    const auto power = Nodalys::ApplyOperation(Nodalys::Operation::POWER, 2.0, 3.0);
    REQUIRE(power.has_value());
    CHECK(power->Value == 8.0);
    CHECK(power->PartialFirst == 12.0);
    CHECK_THAT(power->PartialSecond, WithinRel(8.0 * std::log(2.0), 1e-14));
    CHECK_FALSE(Nodalys::ApplyOperation(Nodalys::Operation::CONSTANT, 1.0, 0.0).has_value());
    CHECK_FALSE(Nodalys::ApplyOperation(Nodalys::Operation::TERNARY, 1.0, 0.0).has_value());
    CHECK(Nodalys::GetOperationName(Nodalys::Operation::SQRT) == "sqrt");
    CHECK(Nodalys::GetOperationName(Nodalys::Operation::POWER) == "^");
}
