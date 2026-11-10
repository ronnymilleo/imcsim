/**
 * @file    nodalys_parser_and_export_tests.cpp
 * @brief   Tests for SPICE netlist parsing, number scales, result exporters, and CLI.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <nodalys/nodalys.h>

#include <filesystem>
#include <fstream>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

TEST_CASE("Engineering scale suffixes and units parse into numeric values", "[nodalys][parser]") {
    SECTION("Standard metric scales") {
        CHECK(nodalys::ParseSpiceNumber("0").value_or(-1) == 0.0);
        CHECK(nodalys::ParseSpiceNumber("12").value_or(-1) == 12.0);
        CHECK(nodalys::ParseSpiceNumber("-5.5").value_or(0) == -5.5);
        CHECK_THAT(*nodalys::ParseSpiceNumber("1k"), WithinRel(1e3, 1e-9));
        CHECK_THAT(*nodalys::ParseSpiceNumber("2.2Meg"), WithinRel(2.2e6, 1e-9));
        CHECK_THAT(*nodalys::ParseSpiceNumber("100n"), WithinRel(100e-9, 1e-9));
        CHECK_THAT(*nodalys::ParseSpiceNumber("10u"), WithinRel(10e-6, 1e-9));
        CHECK_THAT(*nodalys::ParseSpiceNumber("47p"), WithinRel(47e-12, 1e-9));
        CHECK_THAT(*nodalys::ParseSpiceNumber("1.5f"), WithinRel(1.5e-15, 1e-9));
        CHECK_THAT(*nodalys::ParseSpiceNumber("1m"), WithinRel(1e-3, 1e-9));
        CHECK_THAT(*nodalys::ParseSpiceNumber("1mil"), WithinRel(25.4e-6, 1e-9));
        CHECK_THAT(*nodalys::ParseSpiceNumber("1G"), WithinRel(1e9, 1e-9));
        CHECK_THAT(*nodalys::ParseSpiceNumber("1T"), WithinRel(1e12, 1e-9));
    }

    SECTION("Case insensitivity") {
        CHECK_THAT(*nodalys::ParseSpiceNumber("10K"), WithinRel(1e4, 1e-9));
        CHECK_THAT(*nodalys::ParseSpiceNumber("10MEG"), WithinRel(1e7, 1e-9));
        CHECK_THAT(*nodalys::ParseSpiceNumber("10meg"), WithinRel(1e7, 1e-9));
        CHECK_THAT(*nodalys::ParseSpiceNumber("10M"), WithinRel(1e-2, 1e-9));
    }

    SECTION("Ignores trailing units") {
        CHECK_THAT(*nodalys::ParseSpiceNumber("10uF"), WithinRel(10e-6, 1e-9));
        CHECK_THAT(*nodalys::ParseSpiceNumber("1kOhm"), WithinRel(1e3, 1e-9));
        CHECK_THAT(*nodalys::ParseSpiceNumber("5V"), WithinRel(5.0, 1e-9));
        CHECK_THAT(*nodalys::ParseSpiceNumber("100mA"), WithinRel(0.1, 1e-9));
        CHECK_THAT(*nodalys::ParseSpiceNumber("100kHz"), WithinRel(1e5, 1e-9));
    }

    SECTION("Invalid numbers") {
        CHECK(!nodalys::ParseSpiceNumber("").has_value());
        CHECK(!nodalys::ParseSpiceNumber("abc").has_value());
    }
}

TEST_CASE("SPICE netlists parse into runnable circuits and simulation directives", "[nodalys][parser]") {
    SECTION("Resistive voltage divider with .op") {
        constexpr std::string_view netlist = "* Resistive Divider Test\n"
                                             "V1 1 0 10V\n"
                                             "R1 1 2 1k\n"
                                             "R2 2 0 1k\n"
                                             ".op\n"
                                             ".end\n";

        const auto parsed = nodalys::ParseNetlist(netlist);
        REQUIRE(parsed.has_value());
        CHECK(parsed->Title == "* Resistive Divider Test");
        REQUIRE(parsed->Analyses.size() == 1);
        CHECK(parsed->Analyses[0].Kind == nodalys::AnalysisKind::OperatingPoint);

        const auto op = nodalys::ComputeOperatingPoint(parsed->CircuitInstance);
        REQUIRE(op.has_value());
        REQUIRE(op->NodeVoltages.size() >= 3);
        CHECK_THAT(op->NodeVoltages[1], WithinRel(10.0, 1e-6));
        CHECK_THAT(op->NodeVoltages[2], WithinRel(5.0, 1e-6));
    }

    SECTION("RC circuit step response with .tran") {
        constexpr std::string_view netlist = "* RC Step Response\n"
                                             "V1 1 0 PULSE(0 5 0 1u 1u 50u 100u)\n"
                                             "R1 1 2 1k\n"
                                             "C1 2 0 10n\n"
                                             ".tran 0.5u 20u\n"
                                             ".end\n";

        const auto parsed = nodalys::ParseNetlist(netlist);
        REQUIRE(parsed.has_value());
        REQUIRE(parsed->Analyses.size() == 1);
        CHECK(parsed->Analyses[0].Kind == nodalys::AnalysisKind::Transient);

        const auto &cfg = std::get<nodalys::TransientConfig>(parsed->Analyses[0].Config);
        CHECK_THAT(cfg.TimeStep, WithinRel(0.5e-6, 1e-9));
        CHECK_THAT(cfg.StopTime, WithinRel(20e-6, 1e-9));

        const auto tran = nodalys::ComputeTransient(parsed->CircuitInstance, cfg);
        REQUIRE(tran.has_value());
        REQUIRE(tran->Times.size() > 10);
        // Capacitor charges towards 5V
        const double final_v = tran->NodeVoltages[2].back();
        CHECK(final_v > 4.0);
    }

    SECTION("BJT inverter stage with .model") {
        constexpr std::string_view netlist = "* BJT Inverter\n"
                                             "VCC 1 0 5V\n"
                                             "VIN 2 0 5V\n"
                                             "RB 2 3 10k\n"
                                             "RC 1 4 1k\n"
                                             "Q1 4 3 0 Q2N2222\n"
                                             ".model Q2N2222 NPN(BF=150 IS=1e-14)\n"
                                             ".op\n"
                                             ".end\n";

        const auto parsed = nodalys::ParseNetlist(netlist);
        REQUIRE(parsed.has_value());
        const auto op = nodalys::ComputeOperatingPoint(parsed->CircuitInstance);
        REQUIRE(op.has_value());
        // Node 4 is collector; with 5V input, transistor saturates and pulls node 4 low (< 0.3V)
        CHECK(op->NodeVoltages[4] < 0.3);
    }

    SECTION("MOSFET switch with .model") {
        constexpr std::string_view netlist = "* MOSFET Inverter\n"
                                             "VDD 1 0 5\n"
                                             "VG 2 0 5\n"
                                             "RD 1 3 1k\n"
                                             "M1 3 2 0 0 MOD1 W=100u L=1u\n"
                                             ".model MOD1 NMOS(VTO=1.0 KP=1e-4)\n"
                                             ".op\n"
                                             ".end\n";

        const auto parsed = nodalys::ParseNetlist(netlist);
        REQUIRE(parsed.has_value());
        const auto op = nodalys::ComputeOperatingPoint(parsed->CircuitInstance);
        REQUIRE(op.has_value());
        // Node 3 is drain; pulled low by active NMOS
        CHECK(op->NodeVoltages[3] < 0.5);
    }

    SECTION("Subcircuit expansion (.subckt and X)") {
        constexpr std::string_view netlist = "* Subcircuit Divider\n"
                                             ".subckt DIVIDER IN OUT GND\n"
                                             "R1 IN OUT 2k\n"
                                             "R2 OUT GND 2k\n"
                                             ".ends DIVIDER\n"
                                             "V1 1 0 12\n"
                                             "X1 1 2 0 DIVIDER\n"
                                             ".op\n"
                                             ".end\n";

        const auto parsed = nodalys::ParseNetlist(netlist);
        REQUIRE(parsed.has_value());
        const auto op = nodalys::ComputeOperatingPoint(parsed->CircuitInstance);
        REQUIRE(op.has_value());
        CHECK_THAT(op->NodeVoltages[1], WithinRel(12.0, 1e-6));
        CHECK_THAT(op->NodeVoltages[2], WithinRel(6.0, 1e-6));
    }

    SECTION("Analysis directives parsing (.dc, .ac, .tf, .noise)") {
        constexpr std::string_view netlist = "* Multi-analysis directives\n"
                                             "V1 1 0 10\n"
                                             "R1 1 2 1k\n"
                                             "R2 2 0 1k\n"
                                             ".dc V1 0 10 1\n"
                                             ".ac dec 10 10 100k\n"
                                             ".tf V(2) V1\n"
                                             ".noise V(2) V1 dec 5 10 100k\n"
                                             ".options reltol=1e-4 maxiter=200\n"
                                             ".end\n";

        const auto parsed = nodalys::ParseNetlist(netlist);
        REQUIRE(parsed.has_value());
        CHECK_THAT(parsed->Options.RelTol, WithinRel(1e-4, 1e-9));
        CHECK(parsed->Options.MaxIterations == 200);

        REQUIRE(parsed->Analyses.size() == 4);
        CHECK(parsed->Analyses[0].Kind == nodalys::AnalysisKind::DcSweep);
        CHECK(parsed->Analyses[1].Kind == nodalys::AnalysisKind::AcSweep);
        CHECK(parsed->Analyses[2].Kind == nodalys::AnalysisKind::TransferFunction);
        CHECK(parsed->Analyses[3].Kind == nodalys::AnalysisKind::Noise);
    }
}

TEST_CASE("Simulation results export to RAW, CSV, and JSON formats", "[nodalys][export]") {
    // Set up a simple simulated circuit to generate results
    constexpr std::string_view netlist = "* Exporter Verification Circuit\n"
                                         "V1 1 0 10V\n"
                                         "R1 1 2 1k\n"
                                         "R2 2 0 1k\n"
                                         ".op\n"
                                         ".tran 1u 10u\n"
                                         ".end\n";

    const auto parsed = nodalys::ParseNetlist(netlist);
    REQUIRE(parsed.has_value());

    const auto op = nodalys::ComputeOperatingPoint(parsed->CircuitInstance);
    REQUIRE(op.has_value());

    nodalys::TransientConfig tran_cfg{.StopTime = 10e-6, .TimeStep = 1e-6};
    const auto tran = nodalys::ComputeTransient(parsed->CircuitInstance, tran_cfg);
    REQUIRE(tran.has_value());

    SECTION("RAW Exporter (Nutmeg SPICE rawfile)") {
        const std::string raw_op = nodalys::ExportRaw(*op, "OP Test");
        CHECK(raw_op.contains("Title: OP Test"));
        CHECK(raw_op.contains("Plotname: Operating Point"));
        CHECK(raw_op.contains("Flags: real"));
        CHECK(raw_op.contains("Variables:"));
        CHECK(raw_op.contains("Values:"));

        const std::string raw_tran = nodalys::ExportRaw(*tran, "Tran Test");
        CHECK(raw_tran.contains("Title: Tran Test"));
        CHECK(raw_tran.contains("Plotname: Transient Analysis"));
        CHECK(raw_tran.contains("No. Points: 11"));
        CHECK(raw_tran.contains("\ttime\ttime"));
        CHECK(raw_tran.contains("\tv(1)\tvoltage"));
    }

    SECTION("CSV Exporter") {
        const std::string csv_op = nodalys::ExportCsv(*op);
        CHECK(csv_op.contains("Variable,Type,Value,Unit"));
        CHECK(csv_op.contains("V(1),Voltage,10,V"));
        CHECK(csv_op.contains("V(2),Voltage,5,V"));

        const std::string csv_tran = nodalys::ExportCsv(*tran);
        CHECK(csv_tran.contains("Time,V(1),V(2)"));
    }

    SECTION("JSON Exporter") {
        const std::string json_op = nodalys::ExportJson(*op);
        CHECK(json_op.contains("\"analysis\": \"operating_point\""));
        CHECK(json_op.contains("\"node_voltages\""));
        CHECK(json_op.contains("\"1\": 10"));
        CHECK(json_op.contains("\"2\": 5"));

        const std::string json_tran = nodalys::ExportJson(*tran);
        CHECK(json_tran.contains("\"analysis\": \"transient\""));
        CHECK(json_tran.contains("\"times\""));
        CHECK(json_tran.contains("\"node_voltages\""));
    }

    SECTION("SaveStringToFile") {
        const std::string temp_path = "/tmp/nodalys_test_export.csv";
        const std::string csv_content = nodalys::ExportCsv(*op);
        CHECK(nodalys::SaveStringToFile(temp_path, csv_content));
        CHECK(std::filesystem::exists(temp_path));
        std::filesystem::remove(temp_path);
    }
}

TEST_CASE("The Nodalys standalone binary runs netlists from arguments or standard input", "[nodalys][cli]") {
    const std::string cli_path = "./out/debug/bin/nodalys";
    if (!std::filesystem::exists(cli_path)) {
        SKIP("Nodalys CLI binary not present in default build directory");
    }

    SECTION("CLI --version flag") {
        const int rc = std::system(std::format("{} --version > /dev/null", cli_path).c_str());
        CHECK(rc == 0);
    }

    SECTION("CLI execution with netlist file and JSON output") {
        const std::string test_netlist_path = "/tmp/nodalys_cli_in.cir";
        const std::string test_output_path = "/tmp/nodalys_cli_out.json";

        const std::string netlist = "* Test Divider\n"
                                    "V1 1 0 10\n"
                                    "R1 1 2 1k\n"
                                    "R2 2 0 1k\n"
                                    ".op\n"
                                    ".end\n";
        REQUIRE(nodalys::SaveStringToFile(test_netlist_path, netlist));

        const std::string cmd = std::format("{} -b -f json -o {} {}", cli_path, test_output_path, test_netlist_path);
        const int rc = std::system(cmd.c_str());
        CHECK(rc == 0);
        REQUIRE(std::filesystem::exists(test_output_path));

        std::ifstream out_file(test_output_path);
        std::string json_content((std::istreambuf_iterator<char>(out_file)), std::istreambuf_iterator<char>());
        CHECK(json_content.contains("\"analysis\": \"operating_point\""));
        CHECK(json_content.contains("\"1\": 10"));
        CHECK(json_content.contains("\"2\": 5"));

        std::filesystem::remove(test_netlist_path);
        std::filesystem::remove(test_output_path);
    }

    SECTION("CLI execution with CSV output") {
        const std::string test_netlist_path = "/tmp/nodalys_cli_in.cir";
        const std::string test_output_path = "/tmp/nodalys_cli_out.csv";

        const std::string netlist = "* Test Divider CSV\n"
                                    "V1 1 0 12\n"
                                    "R1 1 2 2k\n"
                                    "R2 2 0 2k\n"
                                    ".op\n"
                                    ".end\n";
        REQUIRE(nodalys::SaveStringToFile(test_netlist_path, netlist));

        const std::string cmd = std::format("{} -b -f csv -o {} {}", cli_path, test_output_path, test_netlist_path);
        const int rc = std::system(cmd.c_str());
        CHECK(rc == 0);
        REQUIRE(std::filesystem::exists(test_output_path));

        std::ifstream out_file(test_output_path);
        std::string csv_content((std::istreambuf_iterator<char>(out_file)), std::istreambuf_iterator<char>());
        CHECK(csv_content.contains("Variable,Type,Value,Unit"));
        CHECK(csv_content.contains("V(1),Voltage,12,V"));
        CHECK(csv_content.contains("V(2),Voltage,6,V"));

        std::filesystem::remove(test_netlist_path);
        std::filesystem::remove(test_output_path);
    }
}
