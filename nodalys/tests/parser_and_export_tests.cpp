/**
 * @file    parser_and_export_tests.cpp
 * @brief   Tests for SPICE netlist parsing, number scales, result exporters, and CLI.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <nodalys/nodalys.h>

#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <string_view>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

/// How the CLI gets the netlist of a run.
enum class InputKind {
    FILE_ARGUMENT,
    STANDARD_INPUT
};

/**
 * @brief   A path in double quotes, so the shell keeps a path with spaces in one piece.
 */
std::string Quote(const std::filesystem::path &path) {
    return std::format("\"{}\"", path.string());
}

/**
 * @class   CliFolder
 * @brief   A folder of its own for the files of the CLI runs of one test, removed at the end.
 * @details Each test process gets a new folder, so the tests can run side by side, as ctest runs them.
 */
class CliFolder {
public:
    CliFolder() {
        std::random_device seed;
        m_Path = std::filesystem::temp_directory_path() / std::format("nodalys-cli-{:08x}", seed());
        std::filesystem::create_directories(m_Path);
    }
    ~CliFolder() {
        std::error_code ignored;
        std::filesystem::remove_all(m_Path, ignored);
    }
    CliFolder(const CliFolder &) = delete;
    CliFolder &operator=(const CliFolder &) = delete;

    /**
     * @brief   Runs the CLI on a netlist and reads the file it writes.
     * @return  The contents of the output file, or none when the CLI failed or wrote nothing.
     */
    std::optional<std::string> Run(const std::string_view arguments, const std::string_view netlist,
                                   const InputKind input = InputKind::FILE_ARGUMENT) const {
        const std::filesystem::path input_path = m_Path / "input.cir";
        const std::filesystem::path output_path = m_Path / "output";
        std::filesystem::remove(output_path);
        if (!Nodalys::SaveStringToFile(input_path.string(), std::string(netlist))) {
            return std::nullopt;
        }
        const std::string source = input == InputKind::STANDARD_INPUT ? "- < " + Quote(input_path) : Quote(input_path);
        const std::string command =
            std::format("{} {} -o {} {}", Quote(NODALYS_CLI_PATH), arguments, Quote(output_path), source);
        if (std::system(command.c_str()) != 0 || !std::filesystem::exists(output_path)) {
            return std::nullopt;
        }
        const std::ifstream file(output_path);
        std::ostringstream contents;
        contents << file.rdbuf();
        return contents.str();
    }

private:
    std::filesystem::path m_Path;
};

} // namespace

TEST_CASE("Engineering scale suffixes and units parse into numeric values", "[nodalys][parser]") {
    SECTION("Standard metric scales") {
        CHECK(Nodalys::ParseSpiceNumber("0").value_or(-1) == 0.0);
        CHECK(Nodalys::ParseSpiceNumber("12").value_or(-1) == 12.0);
        CHECK(Nodalys::ParseSpiceNumber("-5.5").value_or(0) == -5.5);
        CHECK_THAT(*Nodalys::ParseSpiceNumber("1k"), WithinRel(1e3, 1e-9));
        CHECK_THAT(*Nodalys::ParseSpiceNumber("2.2Meg"), WithinRel(2.2e6, 1e-9));
        CHECK_THAT(*Nodalys::ParseSpiceNumber("100n"), WithinRel(100e-9, 1e-9));
        CHECK_THAT(*Nodalys::ParseSpiceNumber("10u"), WithinRel(10e-6, 1e-9));
        CHECK_THAT(*Nodalys::ParseSpiceNumber("47p"), WithinRel(47e-12, 1e-9));
        CHECK_THAT(*Nodalys::ParseSpiceNumber("1.5f"), WithinRel(1.5e-15, 1e-9));
        CHECK_THAT(*Nodalys::ParseSpiceNumber("1m"), WithinRel(1e-3, 1e-9));
        CHECK_THAT(*Nodalys::ParseSpiceNumber("1mil"), WithinRel(25.4e-6, 1e-9));
        CHECK_THAT(*Nodalys::ParseSpiceNumber("1G"), WithinRel(1e9, 1e-9));
        CHECK_THAT(*Nodalys::ParseSpiceNumber("1T"), WithinRel(1e12, 1e-9));
    }

    SECTION("Case insensitivity") {
        CHECK_THAT(*Nodalys::ParseSpiceNumber("10K"), WithinRel(1e4, 1e-9));
        CHECK_THAT(*Nodalys::ParseSpiceNumber("10MEG"), WithinRel(1e7, 1e-9));
        CHECK_THAT(*Nodalys::ParseSpiceNumber("10meg"), WithinRel(1e7, 1e-9));
        CHECK_THAT(*Nodalys::ParseSpiceNumber("10M"), WithinRel(1e-2, 1e-9));
    }

    SECTION("Ignores trailing units") {
        CHECK_THAT(*Nodalys::ParseSpiceNumber("10uF"), WithinRel(10e-6, 1e-9));
        CHECK_THAT(*Nodalys::ParseSpiceNumber("1kOhm"), WithinRel(1e3, 1e-9));
        CHECK_THAT(*Nodalys::ParseSpiceNumber("5V"), WithinRel(5.0, 1e-9));
        CHECK_THAT(*Nodalys::ParseSpiceNumber("100mA"), WithinRel(0.1, 1e-9));
        CHECK_THAT(*Nodalys::ParseSpiceNumber("100kHz"), WithinRel(1e5, 1e-9));
    }

    SECTION("Invalid numbers") {
        CHECK(!Nodalys::ParseSpiceNumber("").has_value());
        CHECK(!Nodalys::ParseSpiceNumber("abc").has_value());
    }
}

TEST_CASE("SPICE netlists parse into runnable circuits and simulation directives", "[nodalys][parser]") {
    SECTION("Resistive voltage divider with .op") {
        const std::string_view netlist = "* Resistive Divider Test\n"
                                         "V1 1 0 10V\n"
                                         "R1 1 2 1k\n"
                                         "R2 2 0 1k\n"
                                         ".op\n"
                                         ".end\n";

        const auto parsed = Nodalys::ParseNetlist(netlist);
        REQUIRE(parsed.has_value());
        CHECK(parsed->Title == "* Resistive Divider Test");
        REQUIRE(parsed->Analyses.size() == 1);
        CHECK(parsed->Analyses[0].Kind == Nodalys::AnalysisKind::OPERATING_POINT);

        const auto op = Nodalys::ComputeOperatingPoint(parsed->CircuitInstance);
        REQUIRE(op.has_value());
        REQUIRE(op->NodeVoltages.size() >= 3);
        CHECK_THAT(op->NodeVoltages[1], WithinRel(10.0, 1e-6));
        CHECK_THAT(op->NodeVoltages[2], WithinRel(5.0, 1e-6));
    }

    SECTION("RC circuit step response with .tran") {
        const std::string_view netlist = "* RC Step Response\n"
                                         "V1 1 0 PULSE(0 5 0 1u 1u 50u 100u)\n"
                                         "R1 1 2 1k\n"
                                         "C1 2 0 10n\n"
                                         ".tran 0.5u 20u\n"
                                         ".end\n";

        const auto parsed = Nodalys::ParseNetlist(netlist);
        REQUIRE(parsed.has_value());
        REQUIRE(parsed->Analyses.size() == 1);
        CHECK(parsed->Analyses[0].Kind == Nodalys::AnalysisKind::TRANSIENT);

        const auto &cfg = std::get<Nodalys::TransientConfig>(parsed->Analyses[0].Config);
        CHECK_THAT(cfg.TimeStep, WithinRel(0.5e-6, 1e-9));
        CHECK_THAT(cfg.StopTime, WithinRel(20e-6, 1e-9));

        const auto tran = Nodalys::ComputeTransient(parsed->CircuitInstance, cfg);
        REQUIRE(tran.has_value());
        REQUIRE(tran->Times.size() > 10);
        // Capacitor charges towards 5V
        const double final_v = tran->NodeVoltages[2].back();
        CHECK(final_v > 4.0);
    }

    SECTION("BJT inverter stage with .model") {
        const std::string_view netlist = "* BJT Inverter\n"
                                         "VCC 1 0 5V\n"
                                         "VIN 2 0 5V\n"
                                         "RB 2 3 10k\n"
                                         "RC 1 4 1k\n"
                                         "Q1 4 3 0 Q2N2222\n"
                                         ".model Q2N2222 NPN(BF=150 IS=1e-14)\n"
                                         ".op\n"
                                         ".end\n";

        const auto parsed = Nodalys::ParseNetlist(netlist);
        REQUIRE(parsed.has_value());
        const auto op = Nodalys::ComputeOperatingPoint(parsed->CircuitInstance);
        REQUIRE(op.has_value());
        // Node 4 is collector; with 5V input, transistor saturates and pulls node 4 low (< 0.3V)
        CHECK(op->NodeVoltages[4] < 0.3);
    }

    SECTION("MOSFET switch with .model") {
        const std::string_view netlist = "* MOSFET Inverter\n"
                                         "VDD 1 0 5\n"
                                         "VG 2 0 5\n"
                                         "RD 1 3 1k\n"
                                         "M1 3 2 0 0 MOD1 W=100u L=1u\n"
                                         ".model MOD1 NMOS(VTO=1.0 KP=1e-4)\n"
                                         ".op\n"
                                         ".end\n";

        const auto parsed = Nodalys::ParseNetlist(netlist);
        REQUIRE(parsed.has_value());
        const auto op = Nodalys::ComputeOperatingPoint(parsed->CircuitInstance);
        REQUIRE(op.has_value());
        // Node 3 is drain; pulled low by active NMOS
        CHECK(op->NodeVoltages[3] < 0.5);
    }

    SECTION("Subcircuit expansion (.subckt and X)") {
        const std::string_view netlist = "* Subcircuit Divider\n"
                                         ".subckt DIVIDER IN OUT GND\n"
                                         "R1 IN OUT 2k\n"
                                         "R2 OUT GND 2k\n"
                                         ".ends DIVIDER\n"
                                         "V1 1 0 12\n"
                                         "X1 1 2 0 DIVIDER\n"
                                         ".op\n"
                                         ".end\n";

        const auto parsed = Nodalys::ParseNetlist(netlist);
        REQUIRE(parsed.has_value());
        const auto op = Nodalys::ComputeOperatingPoint(parsed->CircuitInstance);
        REQUIRE(op.has_value());
        CHECK_THAT(op->NodeVoltages[1], WithinRel(12.0, 1e-6));
        CHECK_THAT(op->NodeVoltages[2], WithinRel(6.0, 1e-6));
    }

    SECTION("Analysis directives parsing (.dc, .ac, .tf, .noise)") {
        const std::string_view netlist = "* Multi-analysis directives\n"
                                         "V1 1 0 10\n"
                                         "R1 1 2 1k\n"
                                         "R2 2 0 1k\n"
                                         ".dc V1 0 10 1\n"
                                         ".ac dec 10 10 100k\n"
                                         ".tf V(2) V1\n"
                                         ".noise V(2) V1 dec 5 10 100k\n"
                                         ".options reltol=1e-4 maxiter=200\n"
                                         ".end\n";

        const auto parsed = Nodalys::ParseNetlist(netlist);
        REQUIRE(parsed.has_value());
        CHECK_THAT(parsed->Options.RelTol, WithinRel(1e-4, 1e-9));
        CHECK(parsed->Options.MaxIterations == 200);

        REQUIRE(parsed->Analyses.size() == 4);
        CHECK(parsed->Analyses[0].Kind == Nodalys::AnalysisKind::DC_SWEEP);
        CHECK(parsed->Analyses[1].Kind == Nodalys::AnalysisKind::AC_SWEEP);
        CHECK(parsed->Analyses[2].Kind == Nodalys::AnalysisKind::TRANSFER_FUNCTION);
        CHECK(parsed->Analyses[3].Kind == Nodalys::AnalysisKind::NOISE);
    }

    SECTION("S-parameter directive parsing (.sp) and port sources") {
        const std::string_view netlist = "* Two-port attenuator\n"
                                         "Vsport1 1 0 dc 0 portnum 1 z0 50\n"
                                         "Vsport2 2 0 dc 0 portnum 2 z0 75\n"
                                         "R1 1 2 100\n"
                                         ".sp dec 5 1Meg 100Meg\n"
                                         ".end\n";

        const auto parsed = Nodalys::ParseNetlist(netlist);
        REQUIRE(parsed.has_value());
        REQUIRE(parsed->Analyses.size() == 1);
        CHECK(parsed->Analyses[0].Kind == Nodalys::AnalysisKind::S_PARAMETERS);

        const auto &cfg = std::get<Nodalys::SParameterConfig>(parsed->Analyses[0].Config);
        CHECK(cfg.SweepType == Nodalys::FrequencySweepType::DECADE);
        CHECK(cfg.Points == 5);
        CHECK_THAT(cfg.StartFrequency, WithinRel(1e6, 1e-9));
        CHECK_THAT(cfg.StopFrequency, WithinRel(100e6, 1e-9));

        REQUIRE(parsed->CircuitInstance.GetPorts().size() == 2);
        CHECK(parsed->CircuitInstance.GetPorts()[0].Number == 1);
        CHECK(parsed->CircuitInstance.GetPorts()[0].Node == 1);
        CHECK_THAT(parsed->CircuitInstance.GetPorts()[0].ReferenceOhms, WithinRel(50.0, 1e-9));
        CHECK(parsed->CircuitInstance.GetPorts()[1].Number == 2);
        CHECK(parsed->CircuitInstance.GetPorts()[1].Node == 2);
        CHECK_THAT(parsed->CircuitInstance.GetPorts()[1].ReferenceOhms, WithinRel(75.0, 1e-9));

        const auto sp = Nodalys::ComputeSParameters(parsed->CircuitInstance, cfg);
        REQUIRE(sp.has_value());
        CHECK(sp->GetPortCount() == 2);
        CHECK(sp->Frequencies.size() == 11);
    }

    SECTION("Pole-Zero analysis directives (.pz)") {
        const std::string_view netlist = "* RC Pole-Zero Netlist\n"
                                         "Vin 1 0 0V\n"
                                         "R1 1 2 1k\n"
                                         "C1 2 0 1u\n"
                                         ".pz 2 Vin pz\n"
                                         ".end\n";

        const auto parsed = Nodalys::ParseNetlist(netlist);
        REQUIRE(parsed.has_value());
        REQUIRE(parsed->Analyses.size() == 1);
        CHECK(parsed->Analyses[0].Kind == Nodalys::AnalysisKind::POLE_ZERO);

        const auto &cfg = std::get<Nodalys::PoleZeroConfig>(parsed->Analyses[0].Config);
        CHECK(cfg.OutputNode == 2);
        CHECK(cfg.InputSource == "Vin");
        CHECK(cfg.FindPoles);
        CHECK(cfg.FindZeros);

        const auto pz = Nodalys::ComputePoleZero(parsed->CircuitInstance, cfg);
        REQUIRE(pz.has_value());
        REQUIRE(pz->Poles.size() == 1);
        CHECK_THAT(pz->Poles[0].real(), WithinRel(-1000.0, 1e-4));
        CHECK_THAT(pz->Poles[0].imag(), WithinAbs(0.0, 1e-6));
        CHECK(pz->Zeros.empty());
    }

    SECTION("Pole-Zero 4-node standard SPICE syntax") {
        const std::string_view netlist = "* RC Pole-Zero Standard Syntax\n"
                                         "Vin 1 0 0V\n"
                                         "R1 1 2 1k\n"
                                         "C1 2 0 1u\n"
                                         ".pz 1 0 2 0 vol pol\n"
                                         ".end\n";

        const auto parsed = Nodalys::ParseNetlist(netlist);
        REQUIRE(parsed.has_value());
        REQUIRE(parsed->Analyses.size() == 1);
        CHECK(parsed->Analyses[0].Kind == Nodalys::AnalysisKind::POLE_ZERO);

        const auto &cfg = std::get<Nodalys::PoleZeroConfig>(parsed->Analyses[0].Config);
        CHECK(cfg.OutputNode == 2);
        CHECK(cfg.InputSource == "Vin");
        CHECK(cfg.FindPoles);
        CHECK_FALSE(cfg.FindZeros);
    }

    SECTION("Harmonic Balance directive (.hb)") {
        const std::string_view netlist = "* HB RC Divider\n"
                                         "Vin 1 0 SIN(0 5 1k)\n"
                                         "R1 1 2 1k\n"
                                         "R2 2 0 1k\n"
                                         ".hb 1k 8 samples=64\n"
                                         ".end\n";

        const auto parsed = Nodalys::ParseNetlist(netlist);
        REQUIRE(parsed.has_value());
        REQUIRE(parsed->Analyses.size() == 1);
        CHECK(parsed->Analyses[0].Kind == Nodalys::AnalysisKind::HARMONIC_BALANCE);

        const auto &cfg = std::get<Nodalys::HarmonicBalanceConfig>(parsed->Analyses[0].Config);
        CHECK_THAT(cfg.FundamentalFrequency, WithinRel(1000.0, 1e-9));
        CHECK(cfg.Harmonics == 8);
        CHECK(cfg.Samples == 64);

        const auto hb = Nodalys::ComputeHarmonicBalance(parsed->CircuitInstance, cfg);
        REQUIRE(hb.has_value());
        REQUIRE(hb->Frequencies.size() == 9);
        CHECK_THAT(std::abs(hb->NodePhasors[2][1]), WithinRel(2.5, 1e-6));

        const std::string_view default_samples_netlist = "V1 1 0 SIN(0 1 2k)\n"
                                                         "R1 1 0 100\n"
                                                         ".hb 2k 5\n"
                                                         ".end\n";
        const auto parsed_default = Nodalys::ParseNetlist(default_samples_netlist);
        REQUIRE(parsed_default.has_value());
        const auto &cfg_default = std::get<Nodalys::HarmonicBalanceConfig>(parsed_default->Analyses[0].Config);
        CHECK_THAT(cfg_default.FundamentalFrequency, WithinRel(2000.0, 1e-9));
        CHECK(cfg_default.Harmonics == 5);
        CHECK(cfg_default.Samples == 0);

        CHECK_FALSE(Nodalys::ParseNetlist("V1 1 0 1\nR1 1 0 1\n.hb 1k\n.end\n").has_value());
        CHECK_FALSE(Nodalys::ParseNetlist("V1 1 0 1\nR1 1 0 1\n.hb -1k 4\n.end\n").has_value());
        CHECK_FALSE(Nodalys::ParseNetlist("V1 1 0 1\nR1 1 0 1\n.hb 1k 4 samples=bad\n.end\n").has_value());
    }
}

TEST_CASE("Simulation results export to RAW, CSV, and JSON formats", "[nodalys][export]") {
    // Set up a simple simulated circuit to generate results
    const std::string_view netlist = "* Exporter Verification Circuit\n"
                                     "V1 1 0 10V\n"
                                     "R1 1 2 1k\n"
                                     "R2 2 0 1k\n"
                                     ".op\n"
                                     ".tran 1u 10u\n"
                                     ".end\n";

    const auto parsed = Nodalys::ParseNetlist(netlist);
    REQUIRE(parsed.has_value());

    const auto op = Nodalys::ComputeOperatingPoint(parsed->CircuitInstance);
    REQUIRE(op.has_value());

    Nodalys::TransientConfig tran_cfg{.StopTime = 10e-6, .TimeStep = 1e-6};
    const auto tran = Nodalys::ComputeTransient(parsed->CircuitInstance, tran_cfg);
    REQUIRE(tran.has_value());

    SECTION("RAW Exporter (Nutmeg SPICE rawfile)") {
        const std::string raw_op = Nodalys::ExportRaw(*op, "OP Test");
        CHECK(raw_op.contains("Title: OP Test"));
        CHECK(raw_op.contains("Plotname: Operating Point"));
        CHECK(raw_op.contains("Flags: real"));
        CHECK(raw_op.contains("Variables:"));
        CHECK(raw_op.contains("Values:"));

        const std::string raw_tran = Nodalys::ExportRaw(*tran, "Tran Test");
        CHECK(raw_tran.contains("Title: Tran Test"));
        CHECK(raw_tran.contains("Plotname: Transient Analysis"));
        CHECK(raw_tran.contains("No. Points: 11"));
        CHECK(raw_tran.contains("\ttime\ttime"));
        CHECK(raw_tran.contains("\tv(1)\tvoltage"));
    }

    SECTION("CSV Exporter") {
        const std::string csv_op = Nodalys::ExportCsv(*op);
        CHECK(csv_op.contains("Variable,Type,Value,Unit"));
        CHECK(csv_op.contains("V(1),Voltage,10,V"));
        CHECK(csv_op.contains("V(2),Voltage,5,V"));

        const std::string csv_tran = Nodalys::ExportCsv(*tran);
        CHECK(csv_tran.contains("Time,V(1),V(2)"));
    }

    SECTION("JSON Exporter") {
        const std::string json_op = Nodalys::ExportJson(*op);
        CHECK(json_op.contains("\"analysis\": \"operating_point\""));
        CHECK(json_op.contains("\"node_voltages\""));
        CHECK(json_op.contains("\"1\": 10"));
        CHECK(json_op.contains("\"2\": 5"));

        const std::string json_tran = Nodalys::ExportJson(*tran);
        CHECK(json_tran.contains("\"analysis\": \"transient\""));
        CHECK(json_tran.contains("\"times\""));
        CHECK(json_tran.contains("\"node_voltages\""));
    }

    SECTION("Touchstone, CSV, and JSON Exporters for S-Parameters") {
        Nodalys::SParameterResult sp_res;
        sp_res.Frequencies = {1e9};
        sp_res.ReferenceOhms = {50.0, 50.0};
        // Row 1: S11=0.1, S12=0.8; Row 2: S21=0.8, S22=0.1
        sp_res.Values = {{0.1, 0.0}, {0.8, 0.0}, {0.8, 0.0}, {0.1, 0.0}};

        const std::string ts = Nodalys::ExportTouchstone(sp_res);
        CHECK(ts.contains("# HZ S RI R 50"));
        CHECK(ts.contains("1000000000 0.1 0 0.8 0 0.8 0 0.1 0"));

        const std::string csv = Nodalys::ExportCsv(sp_res);
        CHECK(csv.contains("Frequency_Hz,S11_dB,S11_deg,S11_re,S11_im"));
        CHECK(csv.contains(",-20,0,0.1,0"));

        const std::string json = Nodalys::ExportJson(sp_res);
        CHECK(json.contains("\"analysis\": \"s_parameters\""));
        CHECK(json.contains("\"ports\": 2"));
        CHECK(json.contains("\"frequencies\""));
        CHECK(json.contains("\"values\""));

        const std::string raw = Nodalys::ExportRaw(sp_res);
        CHECK(raw.contains("Plotname: S-Parameter Analysis"));
        CHECK(raw.contains("Flags: complex"));
    }

    SECTION("Pole-Zero result export (RAW, CSV, JSON)") {
        Nodalys::PoleZeroResult pz_res;
        pz_res.Poles = {{-1000.0, 0.0}};
        pz_res.Zeros = {{0.0, 10000.0}, {0.0, -10000.0}};

        const std::string raw = Nodalys::ExportRaw(pz_res, "PZ Test");
        CHECK(raw.contains("Plotname: Pole-Zero Analysis"));
        CHECK(raw.contains("Flags: complex"));
        CHECK(raw.contains("No. Variables: 2"));
        CHECK(raw.contains("No. Points: 2"));

        const std::string csv = Nodalys::ExportCsv(pz_res);
        CHECK(csv.contains("Type,Index,Real (rad/s),Imag (rad/s),Frequency (Hz)"));
        CHECK(csv.contains("Pole,1,-1000,0,159.15494"));
        CHECK(csv.contains("Zero,1,0,10000,1591.5494"));

        const std::string json = Nodalys::ExportJson(pz_res);
        CHECK(json.contains("\"analysis\": \"pole_zero\""));
        CHECK(json.contains("\"poles\": [{\"real\": -1000, \"imag\": 0, \"frequency_hz\": 159.15494}]"));
        CHECK(json.contains("\"zeros\": ["));
    }

    SECTION("Harmonic Balance result export (RAW, CSV, JSON)") {
        Nodalys::HarmonicBalanceResult hb_res;
        hb_res.Frequencies = {0.0, 1000.0, 2000.0};
        hb_res.NodePhasors.resize(2);
        hb_res.NodePhasors[1] = {{0.5, 0.0}, {2.0, 0.0}, {0.2, 0.0}};
        hb_res.CurrentPhasors.push_back({.Name = "V1", .Phasors = {{-0.5e-3, 0.0}, {-2e-3, 0.0}, {-0.2e-3, 0.0}}});
        hb_res.Times = {0.0, 250e-6, 500e-6, 750e-6};
        hb_res.NodeWaveforms.resize(2);
        hb_res.NodeWaveforms[1] = {2.7, 0.3, -1.3, 0.3};
        hb_res.CurrentWaveforms.push_back({.Name = "V1", .Values = {-2.7e-3, -0.3e-3, 1.3e-3, -0.3e-3}});
        hb_res.Iterations = 3;
        hb_res.Strategy = "direct";

        const std::string raw = Nodalys::ExportRaw(hb_res, "HB Test");
        CHECK(raw.contains("Plotname: Harmonic Balance Spectrum"));
        CHECK(raw.contains("Flags: complex"));
        CHECK(raw.contains("Plotname: Harmonic Balance Waveform"));
        CHECK(raw.contains("Flags: real"));

        const std::string csv = Nodalys::ExportCsv(hb_res);
        CHECK(csv.contains("Harmonic,Frequency_Hz,V(1)_Mag,V(1)_Phase_deg,V(1)_Re,V(1)_Im"));
        CHECK(csv.contains("Signal,THD_Percent"));
        CHECK(csv.contains("Time_s,V(1),I(V1)"));

        const std::string json = Nodalys::ExportJson(hb_res);
        CHECK(json.contains("\"analysis\": \"harmonic_balance\""));
        CHECK(json.contains("\"frequencies\": [0, 1000, 2000]"));
        CHECK(json.contains("\"node_phasors\""));
        CHECK(json.contains("\"thd_percent\""));
        CHECK(json.contains("\"node_waveforms\""));
    }

    SECTION("SaveStringToFile") {
        const std::string temp_path = "/tmp/nodalys_test_export.csv";
        const std::string csv_content = Nodalys::ExportCsv(*op);
        CHECK(Nodalys::SaveStringToFile(temp_path, csv_content));
        CHECK(std::filesystem::exists(temp_path));
        std::filesystem::remove(temp_path);
    }
}

TEST_CASE("SParameterResult computes derived RF figures of merit", "[nodalys][sp]") {
    Nodalys::SParameterResult result;
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
    const CliFolder folder;

    SECTION("It names its version") {
        CHECK(std::system(Quote(NODALYS_CLI_PATH).append(" --version").c_str()) == 0);
    }

    SECTION("It writes an operating point as JSON") {
        const std::optional<std::string> json = folder.Run("-b -f json", "* Divider\n"
                                                                         "V1 1 0 10\n"
                                                                         "R1 1 2 1k\n"
                                                                         "R2 2 0 1k\n"
                                                                         ".op\n"
                                                                         ".end\n");
        REQUIRE(json.has_value());
        CHECK(json->contains("\"analysis\": \"operating_point\""));
        CHECK(json->contains("\"1\": 10"));
        CHECK(json->contains("\"2\": 5"));
    }

    SECTION("It writes an operating point as CSV") {
        const std::optional<std::string> csv = folder.Run("-b -f csv", "* Divider\n"
                                                                       "V1 1 0 12\n"
                                                                       "R1 1 2 2k\n"
                                                                       "R2 2 0 2k\n"
                                                                       ".op\n"
                                                                       ".end\n");
        REQUIRE(csv.has_value());
        CHECK(csv->contains("Variable,Type,Value,Unit"));
        CHECK(csv->contains("V(1),Voltage,12,V"));
        CHECK(csv->contains("V(2),Voltage,6,V"));
    }

    SECTION("It reads the netlist from standard input") {
        const std::optional<std::string> csv = folder.Run("-b -f csv",
                                                          "* Divider\n"
                                                          "V1 1 0 12\n"
                                                          "R1 1 2 2k\n"
                                                          "R2 2 0 2k\n"
                                                          ".op\n"
                                                          ".end\n",
                                                          InputKind::STANDARD_INPUT);
        REQUIRE(csv.has_value());
        CHECK(csv->contains("V(2),Voltage,6,V"));
    }

    SECTION("It writes S-parameters as Touchstone") {
        const std::optional<std::string> touchstone =
            folder.Run("-b -a sp -f touchstone", "* Attenuator\n"
                                                 "Vsport1 1 0 dc 0 portnum 1 z0 50\n"
                                                 "Vsport2 2 0 dc 0 portnum 2 z0 50\n"
                                                 "R1 1 2 50\n"
                                                 ".sp dec 2 1e6 1e8\n"
                                                 ".end\n");
        REQUIRE(touchstone.has_value());
        CHECK(touchstone->contains("# HZ S RI R 50"));
        CHECK(touchstone->contains("! Nodalys S-Parameter Simulation"));
    }

    SECTION("It writes poles and zeros as JSON") {
        const std::optional<std::string> json = folder.Run("-b -a pz -f json", "* Pole-zero\n"
                                                                               "Vin 1 0 0V\n"
                                                                               "R1 1 2 1k\n"
                                                                               "C1 2 0 1u\n"
                                                                               ".pz 2 Vin pz\n"
                                                                               ".end\n");
        REQUIRE(json.has_value());
        CHECK(json->contains("\"analysis\": \"pole_zero\""));
        CHECK(json->contains("\"poles\""));
    }

    SECTION("It writes a harmonic balance as JSON") {
        const std::optional<std::string> json = folder.Run("-b -a hb -f json", "* Harmonic balance\n"
                                                                               "Vin 1 0 SIN(0 4 1k)\n"
                                                                               "R1 1 2 1k\n"
                                                                               "R2 2 0 1k\n"
                                                                               ".hb 1k 4 samples=32\n"
                                                                               ".end\n");
        REQUIRE(json.has_value());
        CHECK(json->contains("\"analysis\": \"harmonic_balance\""));
        CHECK(json->contains("\"frequencies\": [0, 1000, 2000, 3000, 4000]"));
    }
}
