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

    SECTION("S-parameter directive parsing (.sp) and port sources") {
        constexpr std::string_view netlist = "* Two-port attenuator\n"
                                             "Vsport1 1 0 dc 0 portnum 1 z0 50\n"
                                             "Vsport2 2 0 dc 0 portnum 2 z0 75\n"
                                             "R1 1 2 100\n"
                                             ".sp dec 5 1Meg 100Meg\n"
                                             ".end\n";

        const auto parsed = nodalys::ParseNetlist(netlist);
        REQUIRE(parsed.has_value());
        REQUIRE(parsed->Analyses.size() == 1);
        CHECK(parsed->Analyses[0].Kind == nodalys::AnalysisKind::SParameters);

        const auto &cfg = std::get<nodalys::SParameterConfig>(parsed->Analyses[0].Config);
        CHECK(cfg.SweepType == nodalys::FrequencySweepType::Decade);
        CHECK(cfg.Points == 5);
        CHECK_THAT(cfg.StartFrequency, WithinRel(1e6, 1e-9));
        CHECK_THAT(cfg.StopFrequency, WithinRel(100e6, 1e-9));

        REQUIRE(parsed->CircuitInstance.Ports().size() == 2);
        CHECK(parsed->CircuitInstance.Ports()[0].Number == 1);
        CHECK(parsed->CircuitInstance.Ports()[0].Node == 1);
        CHECK_THAT(parsed->CircuitInstance.Ports()[0].ReferenceOhms, WithinRel(50.0, 1e-9));
        CHECK(parsed->CircuitInstance.Ports()[1].Number == 2);
        CHECK(parsed->CircuitInstance.Ports()[1].Node == 2);
        CHECK_THAT(parsed->CircuitInstance.Ports()[1].ReferenceOhms, WithinRel(75.0, 1e-9));

        const auto sp = nodalys::ComputeSParameters(parsed->CircuitInstance, cfg);
        REQUIRE(sp.has_value());
        CHECK(sp->GetPortCount() == 2);
        CHECK(sp->Frequencies.size() == 11);
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

    SECTION("Touchstone, CSV, and JSON Exporters for S-Parameters") {
        nodalys::SParameterResult sp_res;
        sp_res.Frequencies = {1e9};
        sp_res.ReferenceOhms = {50.0, 50.0};
        // Row 1: S11=0.1, S12=0.8; Row 2: S21=0.8, S22=0.1
        sp_res.Values = {{0.1, 0.0}, {0.8, 0.0}, {0.8, 0.0}, {0.1, 0.0}};

        const std::string ts = nodalys::ExportTouchstone(sp_res);
        CHECK(ts.contains("# HZ S RI R 50"));
        CHECK(ts.contains("1000000000 0.1 0 0.8 0 0.8 0 0.1 0"));

        const std::string csv = nodalys::ExportCsv(sp_res);
        CHECK(csv.contains("Frequency_Hz,S11_dB,S11_deg,S11_re,S11_im"));
        CHECK(csv.contains(",-20,0,0.1,0"));

        const std::string json = nodalys::ExportJson(sp_res);
        CHECK(json.contains("\"analysis\": \"s_parameters\""));
        CHECK(json.contains("\"ports\": 2"));
        CHECK(json.contains("\"frequencies\""));
        CHECK(json.contains("\"values\""));

        const std::string raw = nodalys::ExportRaw(sp_res);
        CHECK(raw.contains("Plotname: S-Parameter Analysis"));
        CHECK(raw.contains("Flags: complex"));
    }

    SECTION("SaveStringToFile") {
        const std::string temp_path = "/tmp/nodalys_test_export.csv";
        const std::string csv_content = nodalys::ExportCsv(*op);
        CHECK(nodalys::SaveStringToFile(temp_path, csv_content));
        CHECK(std::filesystem::exists(temp_path));
        std::filesystem::remove(temp_path);
    }
}

TEST_CASE("SParameterResult computes derived RF figures of merit", "[nodalys][sp]") {
    nodalys::SParameterResult result;
    result.Frequencies = {1e9};
    result.ReferenceOhms = {50.0, 50.0};
    // S11 = 0.5 (magnitude 0.5), S12 = 0.5, S21 = 0.5, S22 = 0.5
    result.Values = {{0.5, 0.0}, {0.5, 0.0}, {0.5, 0.0}, {0.5, 0.0}};

    // Return loss for |S11| = 0.5 is -20*log10(0.5) ~ 6.0206 dB
    const auto rl = result.ReturnLossDb(0, 1);
    REQUIRE(rl.has_value());
    CHECK_THAT(*rl, WithinRel(6.0205999, 1e-5));

    // Insertion loss for |S21| = 0.5 is -20*log10(0.5) ~ 6.0206 dB
    const auto il = result.InsertionLossDb(0, 2, 1);
    REQUIRE(il.has_value());
    CHECK_THAT(*il, WithinRel(6.0205999, 1e-5));

    // VSWR for |S11| = 0.5 is (1 + 0.5) / (1 - 0.5) = 3.0
    const auto vswr = result.Vswr(0, 1);
    REQUIRE(vswr.has_value());
    CHECK_THAT(*vswr, WithinRel(3.0, 1e-6));

    // Input impedance: Zin = Z0 * (1 + S11) / (1 - S11) = 50 * (1.5 / 0.5) = 150 ohms
    const auto zin = result.InputImpedance(0, 1);
    REQUIRE(zin.has_value());
    CHECK_THAT(zin->real(), WithinRel(150.0, 1e-6));
    CHECK_THAT(zin->imag(), WithinAbs(0.0, 1e-9));

    // Zero reflection |S11| = 0 => VSWR = 1.0, Zin = 50 ohms, ReturnLoss = nullopt
    result.Values[0] = {0.0, 0.0};
    const auto vswr0 = result.Vswr(0, 1);
    REQUIRE(vswr0.has_value());
    CHECK_THAT(*vswr0, WithinRel(1.0, 1e-6));

    const auto zin0 = result.InputImpedance(0, 1);
    REQUIRE(zin0.has_value());
    CHECK_THAT(zin0->real(), WithinRel(50.0, 1e-6));

    CHECK(!result.ReturnLossDb(0, 1).has_value());
}

TEST_CASE("The Nodalys standalone binary runs netlists from arguments or standard input", "[nodalys][cli]") {
    const std::string cli_path =
        std::filesystem::exists("./out/release/bin/nodalys") ? "./out/release/bin/nodalys" : "./out/debug/bin/nodalys";
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

    SECTION("CLI execution with .sp analysis and Touchstone output") {
        const std::string test_netlist_path = "/tmp/nodalys_cli_sp_in.cir";
        const std::string test_output_path = "/tmp/nodalys_cli_sp_out.s2p";

        const std::string netlist = "* Test SP Attenuator\n"
                                    "Vsport1 1 0 dc 0 portnum 1 z0 50\n"
                                    "Vsport2 2 0 dc 0 portnum 2 z0 50\n"
                                    "R1 1 2 50\n"
                                    ".sp dec 2 1e6 1e8\n"
                                    ".end\n";
        REQUIRE(nodalys::SaveStringToFile(test_netlist_path, netlist));

        const std::string cmd =
            std::format("{} -b -a sp -f touchstone -o {} {}", cli_path, test_output_path, test_netlist_path);
        const int rc = std::system(cmd.c_str());
        CHECK(rc == 0);
        REQUIRE(std::filesystem::exists(test_output_path));

        std::ifstream out_file(test_output_path);
        std::string ts_content((std::istreambuf_iterator<char>(out_file)), std::istreambuf_iterator<char>());
        CHECK(ts_content.contains("# HZ S RI R 50"));
        CHECK(ts_content.contains("! Nodalys S-Parameter Simulation"));

        std::filesystem::remove(test_netlist_path);
        std::filesystem::remove(test_output_path);
    }
}
