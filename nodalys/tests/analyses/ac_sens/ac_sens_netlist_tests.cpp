/**
 * @file    ac_sens_netlist_tests.cpp
 * @brief   Tests of the AC sensitivity in the netlist parser, the command line and the exporters: the `.sens ... ac`
 *          directive, its refusals, and numbers that equal ComputeAcSensitivity on the same circuit.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <nodalys/nodalys.h>

#include "cli/cli.h"

#include <cmath>
#include <complex>
#include <cstdlib>
#include <format>
#include <numbers>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

namespace {

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

/// A low-pass whose corner is at 1 kHz: R * C = 1 / (2 pi 1 kHz).
const std::string RC_PARTS = "V1 in 0 DC 0 AC 1\n"
                             "R1 in out 1k\n"
                             "C1 out 0 159.15494309189535n\n";

std::string RcNetlist(const std::string &directive) {
    return "RC low-pass\n" + RC_PARTS + directive + "\n.end\n";
}

/**
 * @brief   Runs the command line on a netlist given as its standard input; the exit code goes to `code`.
 */
std::string RunCommandLine(const std::vector<std::string> &arguments, const std::string &netlist, int &code) {
    std::vector<const char *> argv{"nodalys"};
    for (const std::string &argument : arguments) {
        argv.push_back(argument.c_str());
    }
    argv.push_back("-");
    std::istringstream standard_input(netlist);
    std::ostringstream standard_output;
    std::ostringstream standard_error;
    code = Nodalys::RunCli(static_cast<int>(argv.size()), argv.data(), standard_input, standard_output, standard_error);
    return code == EXIT_SUCCESS ? standard_output.str() : standard_error.str();
}

const Nodalys::AcSensitivityConfig &GetAcConfig(const Nodalys::ParsedNetlist &parsed, const std::size_t index = 0) {
    REQUIRE(parsed.Analyses.size() > index);
    REQUIRE(parsed.Analyses[index].Kind == Nodalys::AnalysisKind::AC_SENSITIVITY);
    return std::get<Nodalys::AcSensitivityConfig>(parsed.Analyses[index].Config);
}

std::string FindRefusal(const std::string &netlist) {
    const auto parsed = Nodalys::ParseNetlist(netlist);
    REQUIRE_FALSE(parsed.has_value());
    return parsed.error();
}

/**
 * @brief   Runs ComputeAcSensitivity directly on the RC circuit of the tests, bypassing the CLI and exporters.
 */
Nodalys::AcSensitivityResult ComputeDirectly(const std::string &directive) {
    auto parsed = Nodalys::ParseNetlist(RcNetlist(directive));
    REQUIRE(parsed.has_value());
    const auto result = Nodalys::ComputeAcSensitivity(parsed->CircuitInstance, GetAcConfig(*parsed), parsed->Options);
    REQUIRE(result.has_value());
    return *result;
}

/**
 * @brief   The cells of the CSV row that starts with a frequency, split at the commas.
 */
std::vector<std::string> FindCsvRow(const std::string &csv, const std::string &frequency) {
    std::istringstream lines(csv);
    std::string line;
    while (std::getline(lines, line)) {
        if (line.starts_with(frequency + ",")) {
            std::vector<std::string> cells;
            std::istringstream stream(line);
            std::string cell;
            while (std::getline(stream, cell, ',')) {
                cells.push_back(cell);
            }
            return cells;
        }
    }
    FAIL("No row at " << frequency);
    return {};
}

} // namespace

TEST_CASE("The .sens directive with ac reads its sweep, output and input source", "[nodalys][parser][acsens]") {
    SECTION("A decade sweep with a single-ended output") {
        const auto parsed = Nodalys::ParseNetlist(RcNetlist(".sens v(out) ac dec 10 1 1meg"));
        REQUIRE(parsed.has_value());
        const Nodalys::AcSensitivityConfig &config = GetAcConfig(*parsed);
        CHECK(config.SweepType == Nodalys::FrequencySweepType::DECADE);
        CHECK(config.PointsPerDecade == 10);
        CHECK(config.StartFrequency == 1.0);
        CHECK(config.StopFrequency == 1e6);
        CHECK(config.OutputPositiveNode != 0);
        CHECK(config.OutputNegativeNode == 0);
        CHECK(config.InputSource == "V1");
    }

    SECTION("The octave and linear sweeps and a lower case directive") {
        const auto octave = Nodalys::ParseNetlist(RcNetlist(".SENS V(OUT) AC OCT 4 100 1k"));
        REQUIRE(octave.has_value());
        CHECK(GetAcConfig(*octave).SweepType == Nodalys::FrequencySweepType::OCTAVE);
        CHECK(GetAcConfig(*octave).PointsPerDecade == 4);

        const auto linear = Nodalys::ParseNetlist(RcNetlist(".sens v(out) ac lin 5 100 500"));
        REQUIRE(linear.has_value());
        CHECK(GetAcConfig(*linear).SweepType == Nodalys::FrequencySweepType::LINEAR);
        CHECK(GetAcConfig(*linear).StopFrequency == 500.0);
    }

    SECTION("A differential output takes both nodes") {
        const auto parsed = Nodalys::ParseNetlist(RcNetlist(".sens v(in,out) ac dec 5 10 10k"));
        REQUIRE(parsed.has_value());
        CHECK(GetAcConfig(*parsed).OutputNegativeNode != 0);
        CHECK(GetAcConfig(*parsed).OutputNegativeNode != GetAcConfig(*parsed).OutputPositiveNode);
    }

    SECTION("The input is the one source with an AC magnitude, a current source included, wherever it stands") {
        const auto parsed = Nodalys::ParseNetlist("Current drive\n"
                                                  ".sens v(out) ac dec 5 10 10k\n"
                                                  "V1 in 0 5\n"
                                                  "I1 0 out DC 0 AC 2\n"
                                                  "R1 out 0 1k\n"
                                                  "R2 in out 1k\n"
                                                  ".end\n");
        REQUIRE(parsed.has_value());
        CHECK(GetAcConfig(*parsed).InputSource == "I1");
    }

    SECTION("The DC form is still the DC form, and both can be in one netlist") {
        const auto parsed = Nodalys::ParseNetlist(RcNetlist(".sens v(out)\n.sens v(out) ac dec 5 10 10k"));
        REQUIRE(parsed.has_value());
        REQUIRE(parsed->Analyses.size() == 2);
        CHECK(parsed->Analyses[0].Kind == Nodalys::AnalysisKind::SENSITIVITY);
        CHECK(parsed->Analyses[1].Kind == Nodalys::AnalysisKind::AC_SENSITIVITY);
    }
}

TEST_CASE("The .sens directive with ac refuses what it cannot run, saying why", "[nodalys][parser][acsens]") {
    CHECK(FindRefusal(RcNetlist(".sens v(out) ac")).contains("Malformed .sens ac"));
    CHECK(FindRefusal(RcNetlist(".sens v(out) ac 10 1 1meg")).contains("Malformed .sens ac"));
    CHECK(FindRefusal(RcNetlist(".sens v(out) ac dec 10 1")).contains("Malformed .sens ac"));
    CHECK(FindRefusal(RcNetlist(".sens v(out) ac log 10 1 1meg")).contains("Unknown sweep type 'log'"));
    CHECK(FindRefusal(RcNetlist(".sens v(out) ac dec 0 1 1meg")).contains("number of points"));
    CHECK(FindRefusal(RcNetlist(".sens v(out) ac dec 2.5 1 1meg")).contains("number of points"));
    CHECK(FindRefusal(RcNetlist(".sens v(out) ac dec 10 0 1meg")).contains("frequency range"));
    CHECK(FindRefusal(RcNetlist(".sens v(out) ac dec 10 1k 1")).contains("frequency range"));
    CHECK(FindRefusal(RcNetlist(".sens v(out) ac dec 10 abc 1k")).contains("frequency range"));
    CHECK(FindRefusal(RcNetlist(".sens v(0,out) ac dec 10 1 1k")).contains("other than ground"));
    CHECK(FindRefusal(RcNetlist(".sens v(0) ac dec 10 1 1k")).contains("other than ground"));
    CHECK(FindRefusal(RcNetlist(".sens v(out,out) ac dec 10 1 1k")).contains("the same node"));

    SECTION("The refusal names the line") {
        CHECK(FindRefusal(RcNetlist(".sens v(out) ac dec 0 1 1meg")).contains("'.sens v(out) ac dec 0 1 1meg'"));
    }

    SECTION("No source has an AC magnitude") {
        const std::string refusal = FindRefusal("Silent\nV1 in 0 5\nR1 in out 1k\nC1 out 0 1n\n"
                                                ".sens v(out) ac dec 10 1 1k\n.end\n");
        CHECK(refusal.contains("No source has an AC magnitude"));
    }

    SECTION("An AC magnitude of zero is no AC magnitude") {
        const std::string refusal = FindRefusal("Zero\nV1 in 0 DC 5 AC 0\nR1 in out 1k\nC1 out 0 1n\n"
                                                ".sens v(out) ac dec 10 1 1k\n.end\n");
        CHECK(refusal.contains("No source has an AC magnitude"));
    }

    SECTION("Two sources with an AC magnitude leave the input unclear, and both are named") {
        const std::string refusal = FindRefusal("Two\nV1 in 0 AC 1\nV2 aux 0 AC 1\nR1 in out 1k\nR2 aux out 1k\n"
                                                "C1 out 0 1n\n.sens v(out) ac dec 10 1 1k\n.end\n");
        CHECK(refusal.contains("More than one source"));
        CHECK(refusal.contains("V1, V2"));
    }
}

TEST_CASE("The AC sensitivity of the other sweep types lands on the frequencies of the sweep", "[nodalys][acsens]") {
    SECTION("Octaves double the frequency every N points and end on the stop") {
        const Nodalys::AcSensitivityResult result = ComputeDirectly(".sens v(out) ac oct 1 100 800");
        REQUIRE(result.Frequencies.size() == 4);
        CHECK_THAT(result.Frequencies[0], WithinRel(100.0, 1e-12));
        CHECK_THAT(result.Frequencies[1], WithinRel(200.0, 1e-12));
        CHECK_THAT(result.Frequencies[3], WithinRel(800.0, 1e-12));
    }

    SECTION("A linear sweep has N points from the start to the stop") {
        const Nodalys::AcSensitivityResult result = ComputeDirectly(".sens v(out) ac lin 5 100 500");
        REQUIRE(result.Frequencies.size() == 5);
        CHECK_THAT(result.Frequencies[0], WithinRel(100.0, 1e-12));
        CHECK_THAT(result.Frequencies[2], WithinRel(300.0, 1e-12));
        CHECK_THAT(result.Frequencies[4], WithinRel(500.0, 1e-12));
    }

    SECTION("A linear sweep of one point is the start") {
        const Nodalys::AcSensitivityResult result = ComputeDirectly(".sens v(out) ac lin 1 250 500");
        REQUIRE(result.Frequencies.size() == 1);
        CHECK_THAT(result.Frequencies[0], WithinRel(250.0, 1e-12));
    }
}

TEST_CASE("The command line runs the AC sensitivity and writes it in each format", "[nodalys][cli][acsens]") {
    const std::string netlist = RcNetlist(".sens v(out) ac dec 1 10 10k");
    const Nodalys::AcSensitivityResult direct = ComputeDirectly(".sens v(out) ac dec 1 10 10k");
    REQUIRE(direct.Frequencies.size() == 4);
    const Nodalys::AcElementSensitivity *resistor = nullptr;
    for (const Nodalys::AcElementSensitivity &element : direct.Elements) {
        if (element.Name == "r1") {
            resistor = &element;
        }
    }
    REQUIRE(resistor != nullptr);
    // At the corner a 1 % higher R moves the gain by -0.043429 dB and the phase by -0.28648 degrees
    constexpr std::size_t CORNER = 2;
    REQUIRE_THAT(direct.Frequencies[CORNER], WithinRel(1000.0, 1e-9));
    CHECK_THAT(*resistor->MagnitudeDecibelsPerPercent[CORNER], WithinAbs(-0.043429, 1e-6));
    CHECK_THAT(*resistor->PhaseDegreesPerPercent[CORNER], WithinAbs(-0.28648, 1e-5));

    SECTION("CSV has a row per frequency with the output phasor and the figures of each element") {
        int code = EXIT_FAILURE;
        const std::string csv = RunCommandLine({"-b", "-f", "csv"}, netlist, code);
        REQUIRE(code == EXIT_SUCCESS);
        CHECK(csv.starts_with("Frequency,Output_re,Output_im,c1_dB_per_percent,c1_deg_per_percent,r1_dB_per_percent,"
                              "r1_deg_per_percent,v1_dB_per_percent,v1_deg_per_percent\n"));
        const std::vector<std::string> row = FindCsvRow(csv, "1000");
        REQUIRE(row.size() == 3 + 2 * direct.Elements.size());
        CHECK_THAT(std::stod(row[1]), WithinAbs(direct.Output[CORNER].real(), 1e-7));
        CHECK_THAT(std::stod(row[2]), WithinAbs(direct.Output[CORNER].imag(), 1e-7));
        CHECK_THAT(std::stod(row[5]), WithinAbs(-0.043429, 1e-6));
        CHECK_THAT(std::stod(row[6]), WithinAbs(-0.28648, 1e-5));
    }

    SECTION("JSON holds the same numbers, with the input and the elements") {
        int code = EXIT_FAILURE;
        const std::string json = RunCommandLine({"-b", "-f", "json"}, netlist, code);
        REQUIRE(code == EXIT_SUCCESS);
        CHECK(json.contains("\"analysis\": \"ac_sensitivity\""));
        CHECK(json.contains("\"input_source\": \"V1\""));
        CHECK(json.contains("\"frequencies\": [10, 100, 1000, 10000]"));
        CHECK(json.contains("\"name\": \"r1\", \"parameter\": \"resistance\", \"value\": 1000"));
        CHECK(json.contains("\"name\": \"c1\", \"parameter\": \"capacitance\""));
        CHECK(json.contains("\"magnitude_db_per_percent\": ["));
        CHECK(json.contains("-0.043429"));
        CHECK(json.contains("-0.2864"));
        CHECK(json.contains("\"warnings\": []"));
    }

    SECTION("The raw file has complex vectors: the frequency, the output and the derivative of each element") {
        int code = EXIT_FAILURE;
        const std::string raw = RunCommandLine({"-b", "-f", "raw"}, netlist, code);
        REQUIRE(code == EXIT_SUCCESS);
        CHECK(raw.contains("Plotname: AC Sensitivity Analysis"));
        CHECK(raw.contains("Flags: complex"));
        CHECK(raw.contains("No. Variables: 5"));
        CHECK(raw.contains("No. Points: 4"));
        CHECK(raw.contains("\tfrequency\tfrequency\n"));
        CHECK(raw.contains("\tr1\tvoltage\n"));
        // The derivative of the output with respect to R at the corner: dV/dR = -V / (R (1 + j)) = -x V / (R (1 + jx))
        const std::complex<double> derivative = resistor->Derivatives[CORNER];
        CHECK(raw.contains(std::format("{:.15e},{:.15e}", derivative.real(), derivative.imag())));
    }

    SECTION("--analysis sens runs the AC form of the netlist") {
        int code = EXIT_FAILURE;
        const std::string csv = RunCommandLine({"-b", "-f", "csv", "-a", "sens"}, netlist, code);
        REQUIRE(code == EXIT_SUCCESS);
        CHECK(csv.starts_with("Frequency,Output_re"));
    }

    SECTION("--analysis sens runs both forms when the netlist has both") {
        int code = EXIT_FAILURE;
        const std::string csv = RunCommandLine({"-b", "-f", "csv", "-a", "sens"},
                                               RcNetlist(".sens v(out)\n.sens v(out) ac dec 1 10 10k"),
                                               code);
        REQUIRE(code == EXIT_SUCCESS);
        CHECK(csv.contains("Frequency,Output_re"));
        CHECK(csv.contains("Vector,Value"));
    }

    SECTION("A refused directive fails the run with its message") {
        int code = EXIT_SUCCESS;
        const std::string message = RunCommandLine({"-b"}, RcNetlist(".sens v(out) ac dec 0 10 10k"), code);
        CHECK(code == EXIT_FAILURE);
        CHECK(message.contains("number of points"));
    }
}

TEST_CASE("A frequency where the output is zero has no figure: null in the JSON, empty in the CSV, one warning",
          "[nodalys][export][acsens]") {
    // A Wien arm against a 2:1 divider is balanced at 1 / (2 pi R C), where the difference of the two nodes is zero
    constexpr double RESISTANCE = 1000.0;
    constexpr double CAPACITANCE = 1e-7;
    const double null_frequency = 1.0 / (2.0 * std::numbers::pi * RESISTANCE * CAPACITANCE);
    const std::string netlist = std::format("Wien bridge\n"
                                            "V1 1 0 AC 1\n"
                                            "R1 1 2 1k\n"
                                            "C1 2 3 100n\n"
                                            "R2 3 0 1k\n"
                                            "C2 3 0 100n\n"
                                            "R3 1 4 2k\n"
                                            "R4 4 0 1k\n"
                                            ".sens v(3,4) ac dec 1 {:.15g} {:.15g}\n"
                                            ".end\n",
                                            null_frequency / 10.0,
                                            null_frequency);

    int code = EXIT_FAILURE;
    const std::string csv = RunCommandLine({"-b", "-f", "csv"}, netlist, code);
    REQUIRE(code == EXIT_SUCCESS);
    // The last row is the null frequency: the output phasor is there, the figures are empty
    const std::size_t last_row = csv.rfind('\n', csv.size() - 2) + 1;
    const std::string row = csv.substr(last_row);
    CHECK(row.contains(",,"));

    const std::string json = RunCommandLine({"-b", "-f", "json"}, netlist, code);
    REQUIRE(code == EXIT_SUCCESS);
    CHECK(json.contains("null"));
    CHECK(json.contains("\"warnings\": [\""));
    CHECK(json.contains("\"output_positive_node\": 3"));
    CHECK(json.contains("\"output_negative_node\": 4"));
}
