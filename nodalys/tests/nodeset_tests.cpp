/**
 * @file    nodeset_tests.cpp
 * @brief   Tests for the nodeset (SPICE `.nodeset`): a guess that picks the stable state a bistable circuit lands on
 *          in the solve of an operating point, which then converges again with it released.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <nodalys/nodalys.h>

#include <cmath>
#include <complex>
#include <format>
#include <string>
#include <string_view>
#include <variant>

using Catch::Matchers::WithinAbs;

namespace {

/// A collector voltage at or below this is a saturated transistor, at or above 4 V a cut-off one.
constexpr double LOW_LEVEL_VOLTS = 0.5;
constexpr double HIGH_LEVEL_VOLTS = 4.0;

/// Two NPN transistors with cross-coupled collectors and bases, which has two stable operating points: node 2 low and
/// node 3 high, or the other way round. The extra lines say what to compute and where to start.
std::string BistableNetlist(const std::string_view extra, const std::string_view supply = "5") {
    return std::format("* bistable\n"
                       "V1 1 0 {}\n"
                       "R1 1 2 1k\n"
                       "R2 1 3 1k\n"
                       "R3 2 5 10k\n"
                       "R4 3 4 10k\n"
                       "Q1 2 4 0 QN\n"
                       "Q2 3 5 0 QN\n"
                       ".model QN NPN(IS=1e-15 BF=100)\n"
                       "{}.end\n",
                       supply, extra);
}

/// The bistable with a small tone on its supply, so the harmonic balance has a bias to start from and a swing around
/// it.
constexpr std::string_view TONE_SUPPLY = "SIN(5 0.05 1k)";
constexpr int TONE_HARMONICS = 3;

Nodalys::HarmonicBalanceResult SolveBalance(const std::string &extra) {
    const auto parsed = Nodalys::ParseNetlist(BistableNetlist(extra, TONE_SUPPLY));
    REQUIRE(parsed.has_value());
    const auto balance = Nodalys::ComputeHarmonicBalance(parsed->CircuitInstance,
                                                         {.FundamentalFrequency = 1e3, .Harmonics = TONE_HARMONICS});
    REQUIRE(balance.has_value());
    return *balance;
}

double DcVoltage(const Nodalys::HarmonicBalanceResult &balance, const std::size_t node) {
    return balance.NodePhasors[node][0].real();
}

Nodalys::ParsedNetlist Parse(const std::string &netlist) {
    auto parsed = Nodalys::ParseNetlist(netlist);
    REQUIRE(parsed.has_value());
    return std::move(*parsed);
}

} // namespace

TEST_CASE("A nodeset picks the stable state of a bistable circuit in the operating point", "[nodalys][nodeset]") {
    SECTION("The first transistor on") {
        const auto parsed = Parse(BistableNetlist(".nodeset v(2)=0.2 v(3)=5\n.op\n"));
        const auto op = Nodalys::ComputeOperatingPoint(parsed.CircuitInstance);
        REQUIRE(op.has_value());
        CHECK(op->NodeVoltages[2] < LOW_LEVEL_VOLTS);
        CHECK(op->NodeVoltages[3] > HIGH_LEVEL_VOLTS);
    }
    SECTION("The second transistor on") {
        const auto parsed = Parse(BistableNetlist(".nodeset v(2)=5 v(3)=0.2\n.op\n"));
        const auto op = Nodalys::ComputeOperatingPoint(parsed.CircuitInstance);
        REQUIRE(op.has_value());
        CHECK(op->NodeVoltages[3] < LOW_LEVEL_VOLTS);
        CHECK(op->NodeVoltages[2] > HIGH_LEVEL_VOLTS);
    }
}

TEST_CASE("The builder sets a nodeset the same way the netlist does", "[nodalys][nodeset]") {
    Nodalys::CircuitBuilder builder(3);
    builder.AddVdc("V1", 1, 0, 5.0);
    builder.AddResistor("R1", 1, 2, 1e3);
    builder.AddResistor("R2", 2, 0, 1e3);
    builder.SetNodeset(2, 4.0);
    // Ground has no voltage to guess
    builder.SetNodeset(0, 3.0);
    const auto circuit = builder.Build();
    REQUIRE(circuit.has_value());
    const auto op = Nodalys::ComputeOperatingPoint(*circuit);
    REQUIRE(op.has_value());
    // The only operating point is the divider's, whatever the guess
    CHECK_THAT(op->NodeVoltages[2], WithinAbs(2.5, 1e-6));
}

TEST_CASE("The transient starts from the state the nodeset picked", "[nodalys][nodeset]") {
    for (const bool is_first_on : {true, false}) {
        const std::string nodeset = is_first_on ? ".nodeset v(2)=0.2 v(3)=5\n" : ".nodeset v(2)=5 v(3)=0.2\n";
        const auto parsed = Parse(BistableNetlist(nodeset + ".tran 10u 1m\n"));
        const auto tran = Nodalys::ComputeTransient(parsed.CircuitInstance,
                                                    std::get<Nodalys::TransientConfig>(parsed.Analyses[0].Config));
        REQUIRE(tran.has_value());
        const double low = is_first_on ? tran->NodeVoltages[2].front() : tran->NodeVoltages[3].front();
        const double high = is_first_on ? tran->NodeVoltages[3].front() : tran->NodeVoltages[2].front();
        CHECK(low < LOW_LEVEL_VOLTS);
        CHECK(high > HIGH_LEVEL_VOLTS);
        // It stays there
        CHECK_THAT(is_first_on ? tran->NodeVoltages[2].back() : tran->NodeVoltages[3].back(), WithinAbs(low, 1e-3));
    }
}

TEST_CASE("A nodeset on a circuit with one operating point is released and changes nothing", "[nodalys][nodeset]") {
    const auto plain = Parse("* divider\nV1 1 0 5\nR1 1 2 1k\nR2 2 3 1k\nR3 3 0 1k\n.op\n.end\n");
    const auto guessed =
        Parse("* divider\nV1 1 0 5\nR1 1 2 1k\nR2 2 3 1k\nR3 3 0 1k\n.nodeset v(2)=4 v(3)=-2\n.op\n.end\n");
    const auto without = Nodalys::ComputeOperatingPoint(plain.CircuitInstance);
    const auto with = Nodalys::ComputeOperatingPoint(guessed.CircuitInstance);
    REQUIRE(without.has_value());
    REQUIRE(with.has_value());
    for (std::size_t node = 1; node < 4; ++node) {
        CHECK_THAT(with->NodeVoltages[node], WithinAbs(without->NodeVoltages[node], 1e-9));
    }
    CHECK_THAT(with->NodeVoltages[2], WithinAbs(10.0 / 3.0, 1e-6));
    // Nothing stays held: the second node is where the circuit puts it
    CHECK_THAT(with->NodeVoltages[3], WithinAbs(5.0 / 3.0, 1e-6));
}

TEST_CASE("A DC sweep starts from the nodeset at its first point and continues from the point before after",
          "[nodalys][nodeset]") {
    for (const bool is_first_on : {true, false}) {
        const std::string nodeset = is_first_on ? ".nodeset v(2)=0.2 v(3)=5\n" : ".nodeset v(2)=5 v(3)=0.2\n";
        const auto parsed = Parse(BistableNetlist(nodeset + ".dc V1 5 4 -0.5\n"));
        const auto sweep = Nodalys::ComputeDCSweep(parsed.CircuitInstance,
                                                   std::get<Nodalys::DcSweepConfig>(parsed.Analyses[0].Config));
        REQUIRE(sweep.has_value());
        const auto &voltages = sweep->Curves[0].NodeVoltages;
        // The state of the first point holds as the rail comes down, since each point starts from the one before
        for (std::size_t point = 0; point < voltages[2].size(); ++point) {
            CHECK((voltages[2][point] < LOW_LEVEL_VOLTS) == is_first_on);
        }
    }
}

TEST_CASE("A transient that starts from initial conditions has no operating point to guess", "[nodalys][nodeset]") {
    const auto parsed = Parse("* uic\nV1 1 0 5\nR1 1 2 1k\nC1 2 0 1u IC=0\n.nodeset v(2)=4\n.tran 10u 5m uic\n.end\n");
    const auto tran = Nodalys::ComputeTransient(parsed.CircuitInstance,
                                                std::get<Nodalys::TransientConfig>(parsed.Analyses[0].Config), true);
    REQUIRE(tran.has_value());
    // The capacitor starts from its own 0 V, not from the 4 V of the guess
    CHECK_THAT(tran->NodeVoltages[2].front(), WithinAbs(0.0, 0.05));
}

TEST_CASE("Malformed .nodeset directives are rejected", "[nodalys][nodeset]") {
    CHECK_FALSE(Nodalys::ParseNetlist("* bad\nR1 1 0 1k\n.nodeset 1=5\n.op\n.end\n").has_value());
    CHECK_FALSE(Nodalys::ParseNetlist("* bad\nR1 1 0 1k\n.nodeset v(1)=abc\n.op\n.end\n").has_value());
}

TEST_CASE("The harmonic balance starts from the bias state a nodeset picks", "[nodalys][nodeset][hb]") {
    SECTION("The first transistor on") {
        const auto balance = SolveBalance(".nodeset v(2)=0.2 v(3)=5\n");
        CHECK(DcVoltage(balance, 2) < LOW_LEVEL_VOLTS);
        CHECK(DcVoltage(balance, 3) > HIGH_LEVEL_VOLTS);
    }
    SECTION("The second transistor on") {
        const auto balance = SolveBalance(".nodeset v(2)=5 v(3)=0.2\n");
        CHECK(DcVoltage(balance, 3) < LOW_LEVEL_VOLTS);
        CHECK(DcVoltage(balance, 2) > HIGH_LEVEL_VOLTS);
    }
    SECTION("The tone rides on the state of the nodeset, not on the other one") {
        const auto first = SolveBalance(".nodeset v(2)=0.2 v(3)=5\n");
        const auto second = SolveBalance(".nodeset v(2)=5 v(3)=0.2\n");
        // The collector that conducts follows the supply tone through its divider, in each state on a different node
        CHECK(std::abs(first.NodePhasors[3][1]) > 0.0);
        CHECK(std::abs(second.NodePhasors[2][1]) > 0.0);
        CHECK(DcVoltage(first, 2) != DcVoltage(second, 2));
    }
    SECTION("Without nodesets the balance takes the state it always did") {
        const auto balance = SolveBalance("");
        const auto parsed = Parse(BistableNetlist(""));
        const auto op = Nodalys::ComputeOperatingPoint(parsed.CircuitInstance);
        REQUIRE(op.has_value());
        constexpr double SAME_STATE_TOLERANCE_VOLTS = 0.1;
        CHECK_THAT(DcVoltage(balance, 2), WithinAbs(op->NodeVoltages[2], SAME_STATE_TOLERANCE_VOLTS));
        CHECK_THAT(DcVoltage(balance, 3), WithinAbs(op->NodeVoltages[3], SAME_STATE_TOLERANCE_VOLTS));
    }
}

TEST_CASE("A nodeset leaves the harmonic balance of a circuit with one state unchanged", "[nodalys][nodeset][hb]") {
    const auto parsed = Parse("* divider\nV1 1 0 SIN(2 1 1k)\nR1 1 2 1k\nR2 2 0 1k\nD1 2 0 DX\n"
                              ".model DX D(IS=1e-14)\n.end\n");
    const auto guided = Parse("* divider\nV1 1 0 SIN(2 1 1k)\nR1 1 2 1k\nR2 2 0 1k\nD1 2 0 DX\n"
                              ".model DX D(IS=1e-14)\n.nodeset v(2)=-3\n.end\n");
    const Nodalys::HarmonicBalanceConfig config{.FundamentalFrequency = 1e3, .Harmonics = TONE_HARMONICS};
    const auto plain_result = Nodalys::ComputeHarmonicBalance(parsed.CircuitInstance, config);
    const auto guided_result = Nodalys::ComputeHarmonicBalance(guided.CircuitInstance, config);
    REQUIRE(plain_result.has_value());
    REQUIRE(guided_result.has_value());
    CHECK_THAT(DcVoltage(*guided_result, 2), WithinAbs(DcVoltage(*plain_result, 2), 1e-6));
}
