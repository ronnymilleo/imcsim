/**
 * @file    netlist_subcircuits_tests.cpp
 * @brief   Tests for the subcircuits of a netlist read by ParseNetlist(): parameters, nested blocks, models, behavioral
 *          sources, private nodes for each instance and what it refuses.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <nodalys/nodalys.h>

#include <format>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;

namespace {

/// Tolerance of a node voltage that a closed form gives, in volts.
constexpr double VOLTAGE_TOLERANCE = 1e-6;

/// The first node of the diode that clamps a 12 V source behind 1 kOhm: one forward drop, in volts.
constexpr double DIODE_DROP_MIN = 0.5;
constexpr double DIODE_DROP_MAX = 0.9;

/**
 * @brief   Parses a netlist and solves its operating point.
 * @param[in]  netlist  The netlist text.
 * @param[out] voltages Voltage of each node, indexed by node number, when the parse succeeded.
 * @return  The error of the parser, or an empty text.
 */
std::string SolveNetlist(const std::string_view netlist, std::vector<double> &voltages) {
    const auto parsed = Nodalys::ParseNetlist(netlist);
    if (!parsed) {
        return parsed.error();
    }
    const auto point = Nodalys::ComputeOperatingPoint(parsed->CircuitInstance);
    REQUIRE(point.has_value());
    voltages = point->NodeVoltages;
    return {};
}

} // namespace

TEST_CASE("A subcircuit of a netlist takes parameters, and each instance has its own values and nodes",
          "[nodalys][parser][subcircuit]") {
    std::vector<double> voltages;
    SECTION("Two instances with different parameters") {
        const std::string error = SolveNetlist("* parameters\n"
                                               ".subckt DIVIDER IN OUT params: RT=1k RB=1k\n"
                                               "R1 IN OUT {RT}\n"
                                               "R2 OUT 0 {RB}\n"
                                               ".ends DIVIDER\n"
                                               "V1 1 0 12\n"
                                               "X1 1 2 DIVIDER RT=2k RB=2k\n"
                                               "X2 1 3 DIVIDER RT=1k RB=3k\n"
                                               "X3 1 4 DIVIDER\n"
                                               ".op\n"
                                               ".end\n",
                                               voltages);
        REQUIRE(error.empty());
        CHECK_THAT(voltages[2], WithinAbs(6.0, VOLTAGE_TOLERANCE));
        CHECK_THAT(voltages[3], WithinAbs(9.0, VOLTAGE_TOLERANCE));
        CHECK_THAT(voltages[4], WithinAbs(6.0, VOLTAGE_TOLERANCE));
    }

    SECTION("A node inside the block is private to each instance") {
        const std::string error = SolveNetlist("* private nodes\n"
                                               ".subckt CHAIN IN OUT params: RA=1k\n"
                                               "R1 IN MID {RA}\n"
                                               "R2 MID OUT 1k\n"
                                               "R3 OUT 0 2k\n"
                                               ".ends\n"
                                               "V1 1 0 12\n"
                                               "X1 1 2 CHAIN RA=1k\n"
                                               "X2 1 3 CHAIN RA=3k\n"
                                               ".op\n"
                                               ".end\n",
                                               voltages);
        REQUIRE(error.empty());
        // 12 V * 2k / (RA + 1k + 2k)
        CHECK_THAT(voltages[2], WithinAbs(6.0, VOLTAGE_TOLERANCE));
        CHECK_THAT(voltages[3], WithinAbs(4.0, VOLTAGE_TOLERANCE));
    }

    SECTION("A .param of the netlist is visible inside a block, and the keyword params: may be left out") {
        const std::string error = SolveNetlist("* top-level parameter\n"
                                               ".param RTOP=1k\n"
                                               ".subckt DIVIDER IN OUT\n"
                                               "R1 IN OUT {RTOP}\n"
                                               "R2 OUT 0 3k\n"
                                               ".ends\n"
                                               "V1 1 0 12\n"
                                               "X1 1 2 DIVIDER\n"
                                               ".end\n",
                                               voltages);
        REQUIRE(error.empty());
        CHECK_THAT(voltages[2], WithinAbs(9.0, VOLTAGE_TOLERANCE));
    }
}

TEST_CASE("A subcircuit of a netlist may place another, and the parameters flow down",
          "[nodalys][parser][subcircuit]") {
    std::vector<double> voltages;
    const std::string error = SolveNetlist("* nested\n"
                                           ".subckt SCALED IN OUT params: K=3\n"
                                           ".subckt DIVIDER A B params: RT=1k RB=1k\n"
                                           "R1 A B {RT}\n"
                                           "R2 B 0 {RB}\n"
                                           ".ends\n"
                                           "XA IN OUT DIVIDER RT=1k RB={K*1k}\n"
                                           ".ends\n"
                                           "V1 1 0 12\n"
                                           "X1 1 2 SCALED K=1\n"
                                           "X2 1 3 SCALED\n"
                                           ".end\n",
                                           voltages);
    REQUIRE(error.empty());
    CHECK_THAT(voltages[2], WithinAbs(6.0, VOLTAGE_TOLERANCE));
    CHECK_THAT(voltages[3], WithinAbs(9.0, VOLTAGE_TOLERANCE));
}

TEST_CASE("A subcircuit of a netlist sees the .model cards of the netlist and has its own",
          "[nodalys][parser][subcircuit]") {
    std::vector<double> voltages;
    const std::string error = SolveNetlist("* models\n"
                                           ".model DOUTER D(IS=1e-14)\n"
                                           ".subckt CLAMP IN OUT\n"
                                           ".model DINNER D(IS=1e-14)\n"
                                           "R1 IN OUT 1k\n"
                                           "D1 OUT 0 DINNER\n"
                                           ".ends\n"
                                           ".subckt OUTERCLAMP IN OUT\n"
                                           "R1 IN OUT 1k\n"
                                           "D1 OUT 0 DOUTER\n"
                                           ".ends\n"
                                           "V1 1 0 12\n"
                                           "X1 1 2 CLAMP\n"
                                           "X2 1 3 OUTERCLAMP\n"
                                           ".end\n",
                                           voltages);
    REQUIRE(error.empty());
    CHECK(voltages[2] > DIODE_DROP_MIN);
    CHECK(voltages[2] < DIODE_DROP_MAX);
    CHECK_THAT(voltages[3], WithinAbs(voltages[2], VOLTAGE_TOLERANCE));
}

TEST_CASE("A behavioral source inside a subcircuit of a netlist reads the nodes of its own instance",
          "[nodalys][parser][subcircuit][behavioral]") {
    std::vector<double> voltages;
    const std::string error = SolveNetlist("* behavioral\n"
                                           ".subckt DOUBLER IN OUT\n"
                                           "B1 OUT 0 V=2*v(IN)\n"
                                           "R9 OUT 0 1k\n"
                                           ".ends\n"
                                           "V1 1 0 3\n"
                                           "X1 1 2 DOUBLER\n"
                                           "X2 2 4 DOUBLER\n"
                                           ".end\n",
                                           voltages);
    REQUIRE(error.empty());
    CHECK_THAT(voltages[2], WithinAbs(6.0, VOLTAGE_TOLERANCE));
    CHECK_THAT(voltages[4], WithinAbs(12.0, VOLTAGE_TOLERANCE));
}

TEST_CASE("A source with a waveform inside a subcircuit of a netlist runs in a transient",
          "[nodalys][parser][subcircuit]") {
    const auto parsed = Nodalys::ParseNetlist("* waveform\n"
                                              ".subckt DRIVE OUT params: LEVEL=2\n"
                                              "V1 OUT 0 PULSE(0 {LEVEL} 0 1n 1n 1m 2m)\n"
                                              ".ends\n"
                                              "X1 1 DRIVE LEVEL=5\n"
                                              "R1 1 0 1k\n"
                                              ".tran 10u 0.5m\n"
                                              ".end\n");
    REQUIRE(parsed.has_value());
    const auto transient = Nodalys::ComputeTransient(parsed->CircuitInstance,
                                                     std::get<Nodalys::TransientConfig>(parsed->Analyses[0].Config));
    REQUIRE(transient.has_value());
    // Node 1, at the last point, is well inside the 1 ms the pulse stays high
    CHECK_THAT(transient->NodeVoltages[1].back(), WithinAbs(5.0, 1e-3));
}

TEST_CASE("The subcircuits of a netlist that cannot be placed are refused with the reason",
          "[nodalys][parser][subcircuit]") {
    const auto refusal = [](const std::string &body) {
        const auto parsed = Nodalys::ParseNetlist("* refused\n" + body + "V1 1 0 1\n.end\n");
        REQUIRE_FALSE(parsed.has_value());
        return parsed.error();
    };

    CHECK_THAT(refusal(".subckt A IN OUT\nR1 IN OUT 1k\n.ends\nX1 1 2 MISSING\n"), ContainsSubstring("MISSING"));
    CHECK_THAT(refusal(".subckt A IN OUT\nR1 IN OUT 1k\n.ends\nX1 A\n"),
               ContainsSubstring("Malformed subcircuit call"));
    CHECK_THAT(refusal(".subckt A IN OUT\nR1 IN OUT 1k\n.ends\nX1 1 A\n"), ContainsSubstring("1 nodes to A"));
    CHECK_THAT(refusal(".subckt A IN OUT\nR1 IN OUT 1k\n.ends\nX1 1 2 3 A\n"), ContainsSubstring("3 nodes"));
    CHECK_THAT(refusal(".subckt A IN OUT\nR1 IN OUT 1k\n.ends\nX1 1 2 A W=3\n"), ContainsSubstring("parameter W"));
    CHECK_THAT(refusal(".subckt A IN OUT\nJ1 IN OUT 0 JMOD\n.ends\nX1 1 2 A\n"), ContainsSubstring("JFET"));
    CHECK_THAT(refusal(".subckt A IN OUT\nR1 IN OUT 1k\nX1 1 2 A\n"), ContainsSubstring("no .ends"));
}

TEST_CASE("The internal nodes of an instance are named by the instance, and directives can use the names",
          "[nodalys][parser][subcircuit]") {
    // 12 V across 1k + 1k + 2k: the node MID sits at 9 V, the output at 6 V
    const std::string_view blocks = ".subckt CHAIN IN OUT\n"
                                    "R1 IN MID 1k\n"
                                    "R2 MID OUT 1k\n"
                                    "R3 OUT 0 2k\n"
                                    ".ends\n"
                                    "V1 1 0 12\n"
                                    "X1 1 2 CHAIN\n";

    SECTION("A transfer function reads the voltage of X1.MID, even when its directive comes first") {
        const auto parsed = Nodalys::ParseNetlist(std::format("* named\n.tf V(X1.MID) V1\n{}.end\n", blocks));
        REQUIRE(parsed.has_value());
        REQUIRE(parsed->Analyses.size() == 1);
        const auto &config = std::get<Nodalys::TransferFunctionConfig>(parsed->Analyses[0].Config);
        CHECK(config.OutputPositiveNode > 2);
        const auto transfer = Nodalys::ComputeTransferFunction(parsed->CircuitInstance, config);
        REQUIRE(transfer.has_value());
        CHECK_THAT(transfer->Gain, WithinAbs(9.0 / 12.0, VOLTAGE_TOLERANCE));
    }

    SECTION("A noise analysis and an initial voltage can name it too") {
        const auto parsed =
            Nodalys::ParseNetlist(std::format("* named\n{}.noise V(X1.MID) V1 dec 2 10 1k\n.end\n", blocks));
        REQUIRE(parsed.has_value());
        REQUIRE(parsed->Analyses.size() == 1);
        const auto &config = std::get<Nodalys::NoiseConfig>(parsed->Analyses[0].Config);
        CHECK(config.OutputPositiveNode > 2);
        CHECK(Nodalys::ComputeNoise(parsed->CircuitInstance, config).has_value());
        CHECK(Nodalys::ParseNetlist(std::format("* named\n{}.ic V(X1.MID)=1\n.end\n", blocks)).has_value());
    }

    SECTION("The operating point lists the node, behind the nodes of the netlist") {
        std::vector<double> voltages;
        REQUIRE(SolveNetlist(std::format("* named\n{}.end\n", blocks), voltages).empty());
        REQUIRE(voltages.size() == 4);
        CHECK_THAT(voltages[2], WithinAbs(6.0, VOLTAGE_TOLERANCE));
        CHECK_THAT(voltages[3], WithinAbs(9.0, VOLTAGE_TOLERANCE));
    }

    SECTION("A pole-zero analysis finds a source inside a block by the nodes it joins") {
        const auto parsed = Nodalys::ParseNetlist("* pz\n"
                                                  ".subckt DRIVE OUT\n"
                                                  "V1 OUT 0 1\n"
                                                  ".ends\n"
                                                  "X1 1 DRIVE\n"
                                                  "R1 1 2 1k\n"
                                                  "C1 2 0 1u\n"
                                                  ".pz 1 0 2 0 vol pz\n"
                                                  ".end\n");
        REQUIRE(parsed.has_value());
        const auto &config = std::get<Nodalys::PoleZeroConfig>(parsed->Analyses[0].Config);
        CHECK(config.InputSource == "X1.V1");
    }
}
