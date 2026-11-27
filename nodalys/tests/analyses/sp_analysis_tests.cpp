/**
 * @file    sp_analysis_tests.cpp
 * @brief   Tests of the S-parameter analysis (.sp): one port against the closed forms of known loads, the impedance and
 *          admittance matrices of a T and a pi network and where they are absent, the sweeps, the wording of each
 *          error and the exports of the matrices.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <nodalys/nodalys.h>

#include <complex>
#include <format>
#include <numbers>
#include <string>
#include <vector>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

using Complex = std::complex<double>;

constexpr double TWO_PI = 2.0 * std::numbers::pi;

/**
 * @brief   Parses a netlist that holds its own .sp card and runs the analysis on it.
 */
std::expected<Nodalys::SParameterResult, std::string> RunNetlist(const std::string &body, const std::string &card) {
    const auto parsed = Nodalys::ParseNetlist(std::format("* test\n{}{}\n.end\n", body, card));
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->Analyses.size() == 1);
    return Nodalys::ComputeSParameters(parsed->CircuitInstance,
                                       std::get<Nodalys::SParameterConfig>(parsed->Analyses[0].Config));
}

void RequireClose(const Complex actual, const Complex expected, const double tolerance) {
    CHECK_THAT(actual.real(), WithinAbs(expected.real(), tolerance));
    CHECK_THAT(actual.imag(), WithinAbs(expected.imag(), tolerance));
}

/// The ports of a two-port made of a series resistor of 100 ohm, and nothing else.
const std::string SERIES_BODY = "Vsport1 1 0 dc 0 portnum 1 z0 50\n"
                                "Vsport2 2 0 dc 0 portnum 2 z0 50\n"
                                "R1 1 2 100\n";

} // namespace

TEST_CASE("One port gives S11 of a 50 ohm load, a short and an open", "[nodalys][sp][one_port]") {
    // A matched load reflects nothing
    const auto matched = RunNetlist("Vsport1 1 0 dc 0 portnum 1 z0 50\nR1 1 0 50\n", ".sp lin 3 1Meg 3Meg");
    REQUIRE(matched.has_value());
    CHECK(matched->GetPortCount() == 1);
    REQUIRE(matched->Frequencies.size() == 3);
    for (std::size_t index = 0; index < 3; ++index) {
        RequireClose(matched->Get(index, 1, 1), 0.0, 1e-9);
        RequireClose(*matched->Impedance(index, 1, 1), 50.0, 1e-6);
        RequireClose(*matched->Admittance(index, 1, 1), 0.02, 1e-9);
    }

    // A short returns everything with the sign changed; its impedance is zero and its admittance is infinite
    const auto shorted = RunNetlist("Vsport1 1 0 dc 0 portnum 1 z0 50\nV1 1 0 0\n", ".sp lin 2 1Meg 2Meg");
    REQUIRE(shorted.has_value());
    RequireClose(shorted->Get(0, 1, 1), -1.0, 1e-9);
    RequireClose(*shorted->Impedance(0, 1, 1), 0.0, 1e-6);
    CHECK_FALSE(shorted->Admittance(0, 1, 1).has_value());

    // An open is a port with nothing at its node but the leak to ground every node has: S11 is 1, Z is infinite
    const auto open = RunNetlist("Vsport1 1 0 dc 0 portnum 1 z0 50\nR9 2 0 1k\n", ".sp lin 2 1Meg 2Meg");
    REQUIRE(open.has_value());
    RequireClose(open->Get(0, 1, 1), 1.0, 1e-6);
    CHECK_FALSE(open->Impedance(0, 1, 1).has_value());
    RequireClose(*open->Admittance(0, 1, 1), 0.0, 1e-9);
}

TEST_CASE("One port on a 100 ohm load against 75 ohm reads (R - Z0) / (R + Z0)", "[nodalys][sp][one_port]") {
    const auto result = RunNetlist("Vsport1 1 0 dc 0 portnum 1 z0 75\nR1 1 0 100\n", ".sp lin 2 1Meg 2Meg");
    REQUIRE(result.has_value());
    RequireClose(result->Get(1, 1, 1), 25.0 / 175.0, 1e-9);
    CHECK(result->ReferenceOhms == std::vector<double>{75.0});
}

TEST_CASE("One port on an RC reads the series impedance at every frequency", "[nodalys][sp][one_port]") {
    constexpr double OHMS = 1e3;
    constexpr double FARADS = 1e-9;
    const auto result = RunNetlist("Vsport1 1 0 dc 0 portnum 1 z0 50\nR1 1 2 1k\nC1 2 0 1n\n", ".sp dec 2 1k 1Meg");
    REQUIRE(result.has_value());
    REQUIRE(result->Frequencies.size() == 7);
    for (std::size_t index = 0; index < result->Frequencies.size(); ++index) {
        const double omega = TWO_PI * result->Frequencies[index];
        const Complex impedance = OHMS + 1.0 / Complex(0.0, omega * FARADS);
        RequireClose(result->Get(index, 1, 1), (impedance - 50.0) / (impedance + 50.0), 1e-9);
        // Every node of the engine leaks 1e-12 siemens to ground, which shows in Y at the picosiemens
        RequireClose(*result->Impedance(index, 1, 1), impedance, 0.1);
        RequireClose(*result->Admittance(index, 1, 1), 1.0 / impedance, 1e-10);
    }
}

TEST_CASE("The impedance and admittance of a T network are the closed forms, and inverses of each other",
          "[nodalys][sp][matrices]") {
    // T of 10 and 20 ohm in series with 30 ohm to ground in the middle, between ports of 50 and 75 ohm
    const auto result = RunNetlist("Vsport1 1 0 dc 0 portnum 1 z0 50\n"
                                   "Vsport2 2 0 dc 0 portnum 2 z0 75\n"
                                   "R1 1 3 10\nR2 3 2 20\nR3 3 0 30\n",
                                   ".sp lin 2 1Meg 2Meg");
    REQUIRE(result.has_value());
    REQUIRE(result->Frequencies.size() == 2);
    for (std::size_t index = 0; index < 2; ++index) {
        // Z11 = R1 + R3, Z12 = Z21 = R3, Z22 = R2 + R3
        RequireClose(*result->Impedance(index, 1, 1), 40.0, 1e-5);
        RequireClose(*result->Impedance(index, 1, 2), 30.0, 1e-5);
        RequireClose(*result->Impedance(index, 2, 1), 30.0, 1e-5);
        RequireClose(*result->Impedance(index, 2, 2), 50.0, 1e-5);
        // Y is the inverse: 1 / (40 * 50 - 30 * 30) = 1 / 1100 times [[50, -30], [-30, 40]]
        RequireClose(*result->Admittance(index, 1, 1), 50.0 / 1100.0, 1e-9);
        RequireClose(*result->Admittance(index, 1, 2), -30.0 / 1100.0, 1e-9);
        RequireClose(*result->Admittance(index, 2, 1), -30.0 / 1100.0, 1e-9);
        RequireClose(*result->Admittance(index, 2, 2), 40.0 / 1100.0, 1e-9);
    }
}

TEST_CASE("The impedance and admittance of a pi network are the closed forms", "[nodalys][sp][matrices]") {
    // Pi of 100 ohm at port 1, 200 ohm at port 2 and 50 ohm between them
    const auto result = RunNetlist("Vsport1 1 0 dc 0 portnum 1 z0 50\n"
                                   "Vsport2 2 0 dc 0 portnum 2 z0 50\n"
                                   "R1 1 0 100\nR2 2 0 200\nR3 1 2 50\n",
                                   ".sp lin 2 1Meg 2Meg");
    REQUIRE(result.has_value());
    // Y11 = 1/100 + 1/50, Y12 = Y21 = -1/50, Y22 = 1/200 + 1/50
    const double y11 = 0.01 + 0.02;
    const double y12 = -0.02;
    const double y22 = 0.005 + 0.02;
    RequireClose(*result->Admittance(0, 1, 1), y11, 1e-9);
    RequireClose(*result->Admittance(0, 1, 2), y12, 1e-9);
    RequireClose(*result->Admittance(0, 2, 1), y12, 1e-9);
    RequireClose(*result->Admittance(0, 2, 2), y22, 1e-9);
    const double determinant = y11 * y22 - y12 * y12;
    RequireClose(*result->Impedance(0, 1, 1), y22 / determinant, 1e-5);
    RequireClose(*result->Impedance(0, 1, 2), -y12 / determinant, 1e-5);
    RequireClose(*result->Impedance(0, 2, 2), y11 / determinant, 1e-5);
}

TEST_CASE("Z is absent for a series element between floating ports and Y for a shorted port, never huge numbers",
          "[nodalys][sp][matrices]") {
    const auto series = RunNetlist(SERIES_BODY, ".sp dec 1 1Meg 1G");
    REQUIRE(series.has_value());
    REQUIRE(series->ImpedanceMatrices.size() == series->Frequencies.size());
    for (std::size_t index = 0; index < series->Frequencies.size(); ++index) {
        CHECK_FALSE(series->ImpedanceMatrices[index].has_value());
        // Y = [[1, -1], [-1, 1]] / 100
        RequireClose(*series->Admittance(index, 1, 1), 0.01, 1e-9);
        RequireClose(*series->Admittance(index, 1, 2), -0.01, 1e-9);
        // The S matrix is untouched by what is absent: R / (R + 2 Z0) = 0.5
        RequireClose(series->Get(index, 1, 1), 0.5, 1e-9);
    }

    // Port 2 shorted by a 0 V source has no finite Y, and a Z that is still there
    const auto shorted = RunNetlist(SERIES_BODY + "V1 2 0 0\n", ".sp lin 2 1Meg 2Meg");
    REQUIRE(shorted.has_value());
    CHECK_FALSE(shorted->AdmittanceMatrices[0].has_value());
}

TEST_CASE("An impedance that is large but finite is still given", "[nodalys][sp][matrices]") {
    // 1 pF at 1 MHz is about 159 kohm, which is no open circuit for the analysis
    const auto result = RunNetlist("Vsport1 1 0 dc 0 portnum 1 z0 50\nC1 1 0 1p\n", ".sp lin 2 1Meg 2Meg");
    REQUIRE(result.has_value());
    const Complex z = *result->Impedance(0, 1, 1);
    CHECK_THAT(z.imag(), WithinRel(-1.0 / (TWO_PI * 1e6 * 1e-12), 1e-6));
}

TEST_CASE("A linear sweep of two points gives both frequencies, and of one point only the start",
          "[nodalys][sp][sweep]") {
    const auto two = RunNetlist(SERIES_BODY, ".sp lin 2 1Meg 3Meg");
    REQUIRE(two.has_value());
    CHECK(two->Frequencies == std::vector<double>{1e6, 3e6});
    CHECK(two->Values.size() == 8);
    const auto one = RunNetlist(SERIES_BODY, ".sp lin 1 1Meg 3Meg");
    REQUIRE(one.has_value());
    CHECK(one->Frequencies == std::vector<double>{1e6});
}

TEST_CASE("Ports in any order of the netlist give the matrix in the order of their numbers", "[nodalys][sp][ports]") {
    const auto result = RunNetlist("Vsport2 2 0 dc 0 portnum 2 z0 75\n"
                                   "Vsport1 1 0 dc 0 portnum 1 z0 50\n"
                                   "R1 1 2 100\n",
                                   ".sp lin 2 1Meg 2Meg");
    REQUIRE(result.has_value());
    CHECK(result->ReferenceOhms == std::vector<double>{50.0, 75.0});
    RequireClose(result->Get(0, 1, 1), 125.0 / 225.0, 1e-9);
    RequireClose(result->Get(0, 2, 2), 75.0 / 225.0, 1e-9);
}

TEST_CASE("The engine says in words what is wrong with the ports and the sweep", "[nodalys][sp][errors]") {
    const auto error_of = [](const std::string &body, const std::string &card) {
        const auto result = RunNetlist(body, card);
        REQUIRE_FALSE(result.has_value());
        return result.error();
    };
    const std::string sweep = ".sp lin 3 1Meg 3Meg";

    CHECK(error_of("Vsport1 1 0 dc 0 portnum 1 z0 0\nR1 1 0 50\n", sweep)
          == "The reference impedance of port 1 must be positive");
    CHECK(error_of("Vsport1 1 0 dc 0 portnum 1 z0 -50\nR1 1 0 50\n", sweep)
          == "The reference impedance of port 1 must be positive");
    CHECK(error_of("Vsport1 0 0 dc 0 portnum 1 z0 50\nR1 1 0 50\n", sweep)
          == "Port 1 is on ground; an S-parameter port needs a node of its own");
    CHECK(error_of("Vsport1 1 0 dc 0 portnum 1 z0 50\nVsport2 1 0 dc 0 portnum 2 z0 50\nR1 1 0 50\n", sweep)
          == "Ports 1 and 2 are on the same node; each port needs a node of its own");
    CHECK(error_of("Vsport1 1 0 dc 0 portnum 1 z0 50\nVsport2 2 0 dc 0 portnum 1 z0 50\nR1 1 2 50\n", sweep)
          == "Two S-parameter ports are numbered 1; give each port its own number");
    CHECK(error_of("Vsport1 1 0 dc 0 portnum 1 z0 50\nVsport2 2 0 dc 0 portnum 3 z0 50\nR1 1 2 50\n", sweep)
          == "The S-parameter ports must be numbered 1 to 2 with no gap; port 2 is missing");
    CHECK(error_of("R1 1 0 50\n", sweep) == "The circuit has no S-parameter port");

    const std::string one_port = "Vsport1 1 0 dc 0 portnum 1 z0 50\nR1 1 0 50\n";
    CHECK(error_of(one_port, ".sp lin 3 3Meg 1Meg") == "The stop frequency must be higher than the start frequency");
}

TEST_CASE("The matrices reach the CSV and the JSON, with an empty cell and a null where one is absent",
          "[nodalys][sp][export]") {
    const auto result = RunNetlist(SERIES_BODY, ".sp lin 2 1Meg 2Meg");
    REQUIRE(result.has_value());
    const std::string csv = Nodalys::ExportCsv(*result);
    CHECK(csv.contains(",Z11_re,Z11_im,Z12_re,Z12_im,Z21_re,Z21_im,Z22_re,Z22_im,Y11_re,Y11_im"));
    // Z is absent, so its eight columns are empty, and Y11 = 0.01
    CHECK(csv.contains(",,,,,,,,,0.01,0,-0.01,0"));
    const std::string json = Nodalys::ExportJson(*result);
    CHECK(json.contains("\"impedance\": [\n    null,\n    null\n  ]"));
    CHECK(json.contains("\"admittance\": ["));
    CHECK(json.contains("{\"re\": 0.01, \"im\": 0}"));

    // A result built without the matrices writes the columns it always had
    Nodalys::SParameterResult plain;
    plain.Frequencies = {1e9};
    plain.ReferenceOhms = {50.0};
    plain.Values = {{0.1, 0.0}};
    CHECK_FALSE(Nodalys::ExportCsv(plain).contains("Z11"));
    CHECK_FALSE(Nodalys::ExportJson(plain).contains("impedance"));
}

TEST_CASE("One port is written as a .s1p Touchstone file", "[nodalys][sp][export]") {
    const auto result = RunNetlist("Vsport1 1 0 dc 0 portnum 1 z0 50\nR1 1 0 100\n", ".sp lin 2 1Meg 2Meg");
    REQUIRE(result.has_value());
    const std::string text = Nodalys::ExportTouchstone(*result);
    CHECK(text.contains("# HZ S RI R 50\n"));
    // S11 = 1/3 with a zero imaginary part, one pair of numbers after the frequency
    CHECK(text.contains("1000000 0.3333333333 0\n"));
    CHECK(text.contains("2000000 0.3333333333 0\n"));
}
