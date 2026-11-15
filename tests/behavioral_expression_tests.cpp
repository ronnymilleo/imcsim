/**
 * @file    behavioral_expression_tests.cpp
 * @brief   Tests for the expressions of behavioral sources: what they read, what is refused and how ngspice gets them.
 */

#include "circuit/behavioral_expression.h"
#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Kind = Core::BehavioralReference::Kind;

/// Nets IN and OUT are nodes 1 and 2, and ground is 0.
std::optional<int> FindTestNode(const std::string_view net) {
    if (net == "IN") {
        return 1;
    }
    if (net == "OUT") {
        return 2;
    }
    if (net == "0") {
        return 0;
    }
    return std::nullopt;
}

/// R1 is read through its probe, and V1 through itself.
std::optional<std::string> FindTestSource(const std::string_view current) {
    if (current == "R1") {
        return "Vprobe-R1";
    }
    if (current == "V1") {
        return "V1";
    }
    return std::nullopt;
}

std::string ErrorOf(const std::string_view text) {
    const auto parsed = Core::ParseBehavioralExpression(text);
    REQUIRE_FALSE(parsed);
    return parsed.error();
}

} // namespace

TEST_CASE("An expression reads its voltages by net and its currents by name, in the order written",
          "[behavioral_expression]") {
    const auto references = Core::ParseBehavioralExpression("2*V(IN) + v( IN , OUT ) - 1k*I(Q1.C)");
    REQUIRE(references);
    REQUIRE(references->size() == 3);
    CHECK((*references)[0] ==
          Core::BehavioralReference{.Type = Kind::VOLTAGE, .Names = {"IN"}, .Start = 2, .Length = 5});
    CHECK((*references)[1].Names == std::vector<std::string>{"IN", "OUT"});
    CHECK((*references)[2].Type == Kind::CURRENT);
    CHECK((*references)[2].Names == std::vector<std::string>{"Q1.C"});
    // Functions, time and the ternary of ngspice are left as they are
    const auto functions = Core::ParseBehavioralExpression("time > 1m ? tanh(V(IN)) : max(0, V(OUT))");
    REQUIRE(functions);
    CHECK(functions->size() == 2);
    CHECK(Core::ListBehavioralCurrents("I(R1)*I(V1)") == std::vector<std::string>{"R1", "V1"});
}

TEST_CASE("An expression that ngspice could not read is refused with the reason", "[behavioral_expression]") {
    CHECK(ErrorOf("  ").contains("Write an expression"));
    CHECK(ErrorOf("V(IN)*(2").contains("not closed"));
    CHECK(ErrorOf("V(IN))").contains("closes no parenthesis"));
    CHECK(ErrorOf("V(IN) $ 2").contains("\"$\""));
    CHECK(ErrorOf("V()").contains("net names"));
    CHECK(ErrorOf("V(A, B, C)").contains("net names"));
    CHECK(ErrorOf("V(2*IN)").contains("not a net name"));
    CHECK(ErrorOf("I(R1, R2)").contains("name of a current"));
    CHECK(ErrorOf("V(abs(IN))").contains("names, not an expression"));
}

TEST_CASE("An expression is written for ngspice with nodes and the sources its currents run through",
          "[behavioral_expression]") {
    const auto written =
        Core::TranslateBehavioralExpression("2*V(IN) + V(IN, OUT)*I(R1) - V(0)", FindTestNode, FindTestSource);
    REQUIRE(written);
    CHECK(*written == "2*v(1) + v(1,2)*i(Vprobe-R1) - v(0)");

    const auto missing_net = Core::TranslateBehavioralExpression("V(LO)", FindTestNode, FindTestSource);
    REQUIRE_FALSE(missing_net);
    CHECK(missing_net.error() == "The circuit has no net named LO");
    const auto missing_current = Core::TranslateBehavioralExpression("I(C9)", FindTestNode, FindTestSource);
    REQUIRE_FALSE(missing_current);
    CHECK(missing_current.error() == "The circuit has no current I(C9)");
}
