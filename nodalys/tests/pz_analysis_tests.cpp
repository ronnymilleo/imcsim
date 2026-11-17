/**
 * @file    pz_analysis_tests.cpp
 * @brief   Tests of the Nodalys pole-zero analysis on circuits that stress a root finder: poles spread over many
 *          decades, many close poles, a pole at the origin, on the imaginary axis, repeated, a right-half-plane zero
 *          and pole, controlled sources and a biased transistor.
 * @details Each circuit is checked two ways: against roots worked out by hand, where the circuit has them in closed
 *          form, and by rebuilding the response from the roots and comparing it with the AC sweep (pole_zero_oracle.h),
 *          which needs no expected roots, so it holds for every circuit.
 */

#include "pole_zero_oracle.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <complex>
#include <format>
#include <nodalys/netlist_parser.h>
#include <nodalys/nodalys.h>
#include <numbers>
#include <string>
#include <vector>

namespace {

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

constexpr double TWO_PI = 2.0 * std::numbers::pi;
/// Relative accuracy asked of a root with a closed form; the Gmin conductance of 1e-12 S moves a pole by far less.
constexpr double CLOSED_FORM_TOLERANCE = 1e-6;

/// A netlist parsed, with the .pz request it holds.
struct Prepared {
    Nodalys::ParsedNetlist Netlist;
    Nodalys::PoleZeroConfig Config;
};

Prepared Prepare(const std::string &text) {
    auto parsed = Nodalys::ParseNetlist(text);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->Analyses.size() == 1);
    const Nodalys::PoleZeroConfig config = std::get<Nodalys::PoleZeroConfig>(parsed->Analyses[0].Config);
    return {.Netlist = std::move(*parsed), .Config = config};
}

/// Runs the analysis on a netlist and compares the roots with the AC sweep over a band.
Testing::PoleZeroReport Check(const std::string &text, const Testing::OracleBand &band) {
    const Prepared prepared = Prepare(text);
    return Testing::CheckPoleZero(prepared.Netlist.CircuitInstance, prepared.Config, band);
}

/// Sorts roots by real part, then imaginary part, so two lists can be compared one to one.
std::vector<std::complex<double>> Sorted(std::vector<std::complex<double>> roots) {
    std::ranges::sort(roots, [](const auto &lhs, const auto &rhs) {
        return lhs.real() != rhs.real() ? lhs.real() < rhs.real() : lhs.imag() < rhs.imag();
    });
    return roots;
}

/// The roots of a x^2 + b x + c = 0, for the circuits whose denominator is a quadratic.
std::vector<std::complex<double>> QuadraticRoots(const double a, const double b, const double c) {
    const std::complex<double> root = std::sqrt(std::complex<double>(b * b - 4.0 * a * c, 0.0));
    return {(-b + root) / (2.0 * a), (-b - root) / (2.0 * a)};
}

/// An RC ladder of the given number of sections, 1 kohm and 1 nF each, driven by a source and open at the end.
std::string RcLadder(const int sections) {
    std::string text = "RC ladder\nVin 1 0 0 AC 1\n";
    for (int section = 1; section <= sections; ++section) {
        text += std::format("R{0} {0} {1} 1k\nC{0} {1} 0 1n\n", section, section + 1);
    }
    text += std::format(".pz {} Vin pz\n.end\n", sections + 1);
    return text;
}

} // namespace

TEST_CASE("A 20-section RC ladder keeps all its close real poles, at the places the closed form gives",
          "[pz][stress]") {
    // The tridiagonal pencil has eigenvalues s_k = -(2 / RC) (1 - cos((2k - 1) pi / (2N + 1))) for a source at one
    // end and an open circuit at the other
    constexpr int SECTIONS = 20;
    constexpr double TIME_CONSTANT = 1e3 * 1e-9;
    const Testing::PoleZeroReport report =
        Check(RcLadder(SECTIONS), {.StartFrequencyHertz = 1.0, .StopFrequencyHertz = 1e9});
    INFO(Testing::Describe(report));
    REQUIRE(report.Poles.size() == SECTIONS);
    CHECK(report.Zeros.empty());
    std::vector<std::complex<double>> expected;
    for (int k = 1; k <= SECTIONS; ++k) {
        const double angle = (2.0 * k - 1.0) * std::numbers::pi / (2.0 * SECTIONS + 1.0);
        expected.emplace_back(-2.0 / TIME_CONSTANT * (1.0 - std::cos(angle)), 0.0);
    }
    const std::vector<std::complex<double>> found = Sorted(report.Poles);
    const std::vector<std::complex<double>> wanted = Sorted(expected);
    for (std::size_t index = 0; index < wanted.size(); ++index) {
        CHECK_THAT(found[index].real(), WithinRel(wanted[index].real(), CLOSED_FORM_TOLERANCE));
        CHECK_THAT(found[index].imag(), WithinAbs(0.0, 1e-6 * std::abs(wanted[index].real())));
    }
    CHECK(Testing::IsMatch(report));
}

TEST_CASE("Poles from 1 Hz to 100 MHz are all found", "[pz][stress]") {
    // Five buffered RC sections, so each pole is exactly -1 / RC
    const std::string netlist = "decades\n"
                                "Vin 1 0 0 AC 1\n"
                                "R1 1 2 159.155\nC1 2 0 1m\nE1 3 0 2 0 1\n"
                                "R2 3 4 15.9155k\nC2 4 0 1u\nE2 5 0 4 0 1\n"
                                "R3 5 6 1.59155k\nC3 6 0 10n\nE3 7 0 6 0 1\n"
                                "R4 7 8 159.155\nC4 8 0 10n\nE4 9 0 8 0 1\n"
                                "R5 9 10 1.59155\nC5 10 0 1n\n"
                                ".pz 10 Vin pz\n.end\n";
    const Testing::PoleZeroReport report = Check(netlist, {.StartFrequencyHertz = 0.01, .StopFrequencyHertz = 1e12});
    INFO(Testing::Describe(report));
    REQUIRE(report.Poles.size() == 5);
    CHECK(report.Zeros.empty());
    for (const double hertz : {1.0, 10.0, 1e4, 1e5, 1e8}) {
        CHECK(Testing::HasRootNear(report.Poles, {-TWO_PI * hertz, 0.0}, 1e-5));
    }
    CHECK(Testing::IsMatch(report));
}

TEST_CASE("A pole pair on the imaginary axis stays on it, with the zero at the origin", "[pz][stress]") {
    // A current into a parallel LC: V / I = sL / (1 + s^2 LC), poles at +/- j / sqrt(LC), a zero at 0
    const Testing::PoleZeroReport report = Check("tank\nIin 0 1 0 AC 1\nL1 1 0 1m\nC1 1 0 1u\n.pz 1 Iin pz\n.end\n",
                                                 {.StartFrequencyHertz = 10.0, .StopFrequencyHertz = 1e6});
    INFO(Testing::Describe(report));
    REQUIRE(report.Poles.size() == 2);
    REQUIRE(report.Zeros.size() == 1);
    const double resonance = 1.0 / std::sqrt(1e-3 * 1e-6);
    // The Gmin conductance gives the pair a real part of -Gmin / 2C = -5e-7 rad/s, which is as far from the axis as
    // the model puts them
    for (const std::complex<double> &pole : report.Poles) {
        CHECK_THAT(std::abs(pole.imag()), WithinRel(resonance, CLOSED_FORM_TOLERANCE));
        CHECK_THAT(pole.real(), WithinAbs(0.0, 1e-5));
    }
    CHECK(report.Zeros[0] == std::complex<double>(0.0, 0.0));
    CHECK(Testing::IsMatch(report));
}

TEST_CASE("The pole of an ideal integrator sits at the origin, where Gmin leaves it a hair to the left",
          "[pz][stress]") {
    // A current into a capacitor: V / I = 1 / sC. Gmin of 1e-12 S across the node puts the pole at -Gmin / C
    const Testing::PoleZeroReport report = Check("integrator\nIin 0 1 0 AC 1\nC1 1 0 1u\n.pz 1 Iin pz\n.end\n",
                                                 {.StartFrequencyHertz = 0.1, .StopFrequencyHertz = 1e6});
    INFO(Testing::Describe(report));
    REQUIRE(report.Poles.size() == 1);
    CHECK(report.Zeros.empty());
    CHECK_THAT(report.Poles[0].real(), WithinAbs(-1e-6, 1e-7));
    CHECK_THAT(report.Poles[0].imag(), WithinAbs(0.0, 1e-12));
    CHECK(Testing::IsMatch(report));
}

TEST_CASE("A repeated pole is found twice", "[pz][stress]") {
    // Two RC sections, 1k and 1 uF each, with a buffer between them: H = 1 / (1 + s/1000)^2
    const Testing::PoleZeroReport report =
        Check("double\nVin 1 0 0 AC 1\nR1 1 2 1k\nC1 2 0 1u\nE1 3 0 2 0 1\nR2 3 4 1k\nC2 4 0 1u\n.pz 4 Vin pz\n.end\n",
              {.StartFrequencyHertz = 0.1, .StopFrequencyHertz = 1e5});
    INFO(Testing::Describe(report));
    REQUIRE(report.Poles.size() == 2);
    for (const std::complex<double> &pole : report.Poles) {
        // A double root is only as exact as the square root of the rounding, so the pair may split a little
        CHECK_THAT(pole.real(), WithinRel(-1000.0, 1e-5));
        CHECK_THAT(pole.imag(), WithinAbs(0.0, 1e-2));
    }
    CHECK(Testing::IsMatch(report));
}

TEST_CASE("A common-source stage has the right-half-plane zero at gm over Cgd and the two poles of its quadratic",
          "[pz][stress]") {
    constexpr double SOURCE_RESISTANCE = 1e3;
    constexpr double LOAD_RESISTANCE = 10e3;
    constexpr double GATE_SOURCE_CAPACITANCE = 10e-12;
    constexpr double GATE_DRAIN_CAPACITANCE = 1e-12;
    constexpr double TRANSCONDUCTANCE = 2e-3;
    const std::string netlist = "common source\nVin 1 0 0 AC 1\nRs 1 2 1k\nCgs 2 0 10p\nCgd 2 3 1p\n"
                                "G1 3 0 2 0 2m\nRL 3 0 10k\n.pz 3 Vin pz\n.end\n";
    const Testing::PoleZeroReport report = Check(netlist, {.StartFrequencyHertz = 1e3, .StopFrequencyHertz = 1e11});
    INFO(Testing::Describe(report));
    REQUIRE(report.Zeros.size() == 1);
    REQUIRE(report.Poles.size() == 2);
    // Node 3: gm vg + vd / RL + s Cgd (vd - vg) = 0 gives the zero s = gm / Cgd; the determinant of the two nodes is
    // Cgs Cgd s^2 + ((Cgs + Cgd) / RL + Cgd / Rs + gm Cgd) s + 1 / (Rs RL)
    CHECK_THAT(report.Zeros[0].real(), WithinRel(TRANSCONDUCTANCE / GATE_DRAIN_CAPACITANCE, CLOSED_FORM_TOLERANCE));
    CHECK(Testing::HasRightHalfPlaneRoot(report.Zeros, 0.0));
    const std::vector<std::complex<double>> expected =
        QuadraticRoots(GATE_SOURCE_CAPACITANCE * GATE_DRAIN_CAPACITANCE,
                       (GATE_SOURCE_CAPACITANCE + GATE_DRAIN_CAPACITANCE) / LOAD_RESISTANCE +
                           GATE_DRAIN_CAPACITANCE / SOURCE_RESISTANCE + TRANSCONDUCTANCE * GATE_DRAIN_CAPACITANCE,
                       1.0 / (SOURCE_RESISTANCE * LOAD_RESISTANCE));
    for (const std::complex<double> &pole : expected) {
        CHECK(Testing::HasRootNear(report.Poles, pole, CLOSED_FORM_TOLERANCE));
    }
    CHECK_FALSE(Testing::HasRightHalfPlaneRoot(report.Poles, 0.0));
    CHECK(Testing::IsMatch(report));
}

TEST_CASE("A node with negative net conductance has its pole in the right half-plane", "[pz][stress]") {
    // 1 mS of resistor against -3 mS of controlled current leaves -2 mS across 1 uF: s = +2000 rad/s
    const Testing::PoleZeroReport report =
        Check("unstable\nIin 0 1 0 AC 1\nR1 1 0 1k\nC1 1 0 1u\nG1 1 0 1 0 -3m\n.pz 1 Iin pz\n.end\n",
              {.StartFrequencyHertz = 1.0, .StopFrequencyHertz = 1e5});
    INFO(Testing::Describe(report));
    REQUIRE(report.Poles.size() == 1);
    CHECK_THAT(report.Poles[0].real(), WithinRel(2000.0, CLOSED_FORM_TOLERANCE));
    CHECK(Testing::HasRightHalfPlaneRoot(report.Poles, 0.0));
    CHECK(Testing::IsMatch(report));
}

TEST_CASE("Each kind of controlled source gives the pole its load sets", "[pz][stress]") {
    // Every case ends in an RC of 1 kohm and 1 uF, so the one pole is at -1000 rad/s
    SECTION("Voltage-controlled voltage source") {
        const Testing::PoleZeroReport report =
            Check("vcvs\nVin 1 0 0 AC 1\nR1 1 2 1k\nE1 3 0 2 0 2\nR2 3 4 1k\nC2 4 0 1u\n.pz 4 Vin pz\n.end\n",
                  {.StartFrequencyHertz = 0.1, .StopFrequencyHertz = 1e5});
        INFO(Testing::Describe(report));
        REQUIRE(report.Poles.size() == 1);
        CHECK_THAT(report.Poles[0].real(), WithinRel(-1000.0, CLOSED_FORM_TOLERANCE));
        CHECK(Testing::IsMatch(report));
    }
    SECTION("Voltage-controlled current source") {
        const Testing::PoleZeroReport report =
            Check("vccs\nVin 1 0 0 AC 1\nR1 1 2 1k\nG1 0 3 2 0 2m\nR2 3 0 1k\nC2 3 0 1u\n.pz 3 Vin pz\n.end\n",
                  {.StartFrequencyHertz = 0.1, .StopFrequencyHertz = 1e5});
        INFO(Testing::Describe(report));
        REQUIRE(report.Poles.size() == 1);
        CHECK_THAT(report.Poles[0].real(), WithinRel(-1000.0, CLOSED_FORM_TOLERANCE));
        CHECK(Testing::IsMatch(report));
    }
    SECTION("Current-controlled current source") {
        const Testing::PoleZeroReport report =
            Check("cccs\nVin 1 0 0 AC 1\nVs 1 2 0\nR1 2 0 1k\nF1 0 3 Vs 2\nR2 3 0 1k\nC2 3 0 1u\n.pz 3 Vin pz\n.end\n",
                  {.StartFrequencyHertz = 0.1, .StopFrequencyHertz = 1e5});
        INFO(Testing::Describe(report));
        REQUIRE(report.Poles.size() == 1);
        CHECK_THAT(report.Poles[0].real(), WithinRel(-1000.0, CLOSED_FORM_TOLERANCE));
        CHECK(Testing::IsMatch(report));
    }
    SECTION("Current-controlled voltage source") {
        const Testing::PoleZeroReport report =
            Check("ccvs\nVin 1 0 0 AC 1\nVs 1 2 0\nR1 2 0 1k\nH1 3 0 Vs 1k\nR2 3 4 1k\nC2 4 0 1u\n.pz 4 Vin pz\n.end\n",
                  {.StartFrequencyHertz = 0.1, .StopFrequencyHertz = 1e5});
        INFO(Testing::Describe(report));
        REQUIRE(report.Poles.size() == 1);
        CHECK_THAT(report.Poles[0].real(), WithinRel(-1000.0, CLOSED_FORM_TOLERANCE));
        CHECK(Testing::IsMatch(report));
    }
}

TEST_CASE("A transistor amplifier linearized at its bias point has every root, whatever the scale of its parts",
          "[pz][stress]") {
    // Coupling capacitors of 10 uF, an emitter bypass of 100 uF and junctions of picofarads, with base and collector
    // resistance: roots from 6 rad/s to 1e10 rad/s and an internal node of 1 ohm
    const std::string netlist =
        "bjt amplifier\nVcc 5 0 12\nVin 1 0 0 AC 1\nC1 1 2 10u\nR1 5 2 51k\nR2 2 0 10k\nRC 5 3 4.7k\nQ1 3 2 4 qm\n"
        "RE 4 0 1k\nCE 4 0 100u\nC2 3 6 10u\nRL 6 0 10k\n"
        ".model qm NPN(IS=6.734f BF=416.4 BR=0.7371 VAF=74 RB=10 RC=1 CJE=4.5p CJC=3.6p TF=301p)\n"
        ".pz 6 Vin pz\n.end\n";
    const Testing::PoleZeroReport report = Check(netlist, {.StartFrequencyHertz = 0.01, .StopFrequencyHertz = 1e10});
    INFO(Testing::Describe(report));
    CHECK(report.Poles.size() == 5);
    CHECK_FALSE(Testing::HasRightHalfPlaneRoot(report.Poles, 0.0));
    // The emitter bypass of 100 uF across 1 kohm puts a zero at -1 / (RE CE) = -10 rad/s, and the collector-base
    // capacitance a right-half-plane zero far above the signal
    CHECK(Testing::HasRootNear(report.Zeros, {-10.0, 0.0}, 1e-3));
    CHECK(Testing::HasRightHalfPlaneRoot(report.Zeros, 1e9));
    CHECK(Testing::IsMatch(report));
}

TEST_CASE("The roots do not depend on how the circuit is numbered or ordered", "[pz][stress]") {
    // The same amplifier with other node numbers and its parts in another order, which reorders the pencil
    const std::string first =
        "bjt amplifier\nVcc 5 0 12\nVin 1 0 0 AC 1\nC1 1 2 10u\nR1 5 2 51k\nR2 2 0 10k\nRC 5 3 4.7k\nQ1 3 2 4 qm\n"
        "RE 4 0 1k\nCE 4 0 100u\nC2 3 6 10u\nRL 6 0 10k\n"
        ".model qm NPN(IS=6.734f BF=416.4 BR=0.7371 VAF=74 RB=10 RC=1 CJE=4.5p CJC=3.6p TF=301p)\n"
        ".pz 6 Vin pz\n.end\n";
    const std::string second =
        "bjt amplifier reordered\nRL 3 0 10k\nC2 6 3 10u\nCE 2 0 100u\nRE 2 0 1k\nQ1 6 4 2 qm\nRC 1 6 4.7k\n"
        "R2 4 0 10k\nR1 1 4 51k\nC1 5 4 10u\nVin 5 0 0 AC 1\nVcc 1 0 12\n"
        ".model qm NPN(IS=6.734f BF=416.4 BR=0.7371 VAF=74 RB=10 RC=1 CJE=4.5p CJC=3.6p TF=301p)\n"
        ".pz 3 Vin pz\n.end\n";
    const Testing::PoleZeroReport one = Check(first, {.StartFrequencyHertz = 0.01, .StopFrequencyHertz = 1e10});
    const Testing::PoleZeroReport other = Check(second, {.StartFrequencyHertz = 0.01, .StopFrequencyHertz = 1e10});
    INFO(Testing::Describe(one));
    INFO(Testing::Describe(other));
    REQUIRE(one.Poles.size() == other.Poles.size());
    REQUIRE(one.Zeros.size() == other.Zeros.size());
    for (const std::complex<double> &pole : one.Poles) {
        CHECK(Testing::HasRootNear(other.Poles, pole, 1e-5));
    }
    for (const std::complex<double> &zero : one.Zeros) {
        // A zero at the origin has no size to be relative to, so it is compared with the scale of the lowest root
        if (std::abs(zero) < 1e-3) {
            CHECK(std::ranges::any_of(other.Zeros, [](const auto &root) { return std::abs(root) < 1e-3; }));
        } else {
            CHECK(Testing::HasRootNear(other.Zeros, zero, 1e-5));
        }
    }
}

TEST_CASE("A circuit with no capacitor or inductor has no poles and no zeros, and says nothing is wrong",
          "[pz][messages]") {
    const Prepared prepared = Prepare("divider\nVin 1 0 0 AC 1\nR1 1 2 1k\nR2 2 0 3k\n.pz 2 Vin pz\n.end\n");
    const auto result = Nodalys::ComputePoleZero(prepared.Netlist.CircuitInstance, prepared.Config);
    REQUIRE(result.has_value());
    CHECK(result->Poles.empty());
    CHECK(result->Zeros.empty());
    CHECK(result->Warnings.empty());
}

TEST_CASE("An output the source does not reach is refused with the reason", "[pz][messages]") {
    SECTION("A separate RC network") {
        const Prepared prepared =
            Prepare("separate\nVin 1 0 0 AC 1\nR1 1 2 1k\nC1 2 0 1u\nR3 3 0 1k\nC3 3 0 1u\n.pz 3 Vin pz\n.end\n");
        const auto result = Nodalys::ComputePoleZero(prepared.Netlist.CircuitInstance, prepared.Config);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().contains("does not respond"));
    }
    SECTION("A diode that is off at the operating point") {
        const Prepared prepared = Prepare("off diode\nVin 1 0 0 AC 1\nD1 1 2 dm\nR1 2 0 1k\nC1 2 0 1u\n"
                                          ".model dm D(IS=1e-14)\n.pz 2 Vin pz\n.end\n");
        const auto result = Nodalys::ComputePoleZero(prepared.Netlist.CircuitInstance, prepared.Config);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().contains("does not respond"));
    }
    SECTION("The same diode biased on has the pole its conductance sets") {
        const Testing::PoleZeroReport report =
            Check("on diode\nVin 1 0 5 AC 1\nD1 1 2 dm\nR1 2 0 1k\nC1 2 0 1u\n.model dm D(IS=1e-14)\n"
                  ".pz 2 Vin pz\n.end\n",
                  {.StartFrequencyHertz = 1.0, .StopFrequencyHertz = 1e8});
        INFO(Testing::Describe(report));
        REQUIRE(report.Poles.size() == 1);
        CHECK_FALSE(Testing::HasRightHalfPlaneRoot(report.Poles, 0.0));
        CHECK(Testing::IsMatch(report));
    }
}

TEST_CASE("An output node the source holds has a constant transfer, so every pole is cancelled by a zero",
          "[pz][messages]") {
    const Testing::PoleZeroReport report = Check("held\nVin 1 0 0 AC 1\nR1 1 2 1k\nC1 2 0 1u\n.pz 1 Vin pz\n.end\n",
                                                 {.StartFrequencyHertz = 1.0, .StopFrequencyHertz = 1e6});
    INFO(Testing::Describe(report));
    REQUIRE(report.Poles.size() == report.Zeros.size());
    for (const std::complex<double> &pole : report.Poles) {
        CHECK(Testing::HasRootNear(report.Zeros, pole, 1e-6));
    }
}
