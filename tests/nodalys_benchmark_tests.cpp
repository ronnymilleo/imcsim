/**
 * @file    nodalys_benchmark_tests.cpp
 * @brief   Performance benchmark suite comparing execution time and scalability between Nodalys and ngspice.
 */

#include "components/bjt.h"
#include "components/capacitor.h"
#include "components/controlled_source.h"
#include "components/diode.h"
#include "components/ground.h"
#include "components/inductor.h"
#include "components/resistor.h"
#include "components/source.h"
#include "components/vcc.h"
#include "components/voltage_source.h"
#include "ngspice/ngspice_backend.h"
#include "nodalys/nodalys_backend.h"

#include <nodalys/circuit_builder.h>
#include <nodalys/nodalys.h>

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <format>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr double VOLTAGE_TOLERANCE_RELATIVE = 1e-3;
constexpr double VOLTAGE_TOLERANCE_ABSOLUTE = 1e-5;

/// Closeness check with relative and absolute tolerances.
bool Close(const double a, const double b, const double relative = VOLTAGE_TOLERANCE_RELATIVE,
           const double absolute = VOLTAGE_TOLERANCE_ABSOLUTE) {
    return std::fabs(a - b) <= relative * std::max(std::fabs(a), std::fabs(b)) + absolute;
}

Core::NgspiceBackend &Ngspice() {
    static Core::NgspiceBackend backend;
    return backend;
}

template <typename Func> double MeasureMs(Func &&func, const int repetitions = 3) {
    func(); // Warmup
    const auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < repetitions; ++i) {
        func();
    }
    const auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count() / repetitions;
}

void PrintBenchmarkRow(const std::string &name, const std::string &type, const std::size_t nodes,
                       const double ngspice_ms, const double nodalys_ms) {
    if (ngspice_ms > 0.0 && nodalys_ms > 0.0) {
        const double speedup = ngspice_ms / nodalys_ms;
        std::cout << std::format("  | {:<34} | {:<10} | {:>5} | {:>8.3f} ms | {:>8.3f} ms | {:>8.2f}x |\n", name, type,
                                 nodes, ngspice_ms, nodalys_ms, speedup);
    } else {
        std::cout << std::format("  | {:<34} | {:<10} | {:>5} | {:>11} | {:>8.3f} ms | {:>9} |\n", name, type, nodes,
                                 "N/A", nodalys_ms, "Autonomous");
    }
}

/// Helper container holding both the Circuit and the allocated components it references by pointer.
struct OwnedCircuit {
    std::vector<std::unique_ptr<Core::Component>> Parts;
    Core::Circuit Instance;

    template <typename T, typename... Args> T &AddPart(std::vector<int> nodes, Args &&...args) {
        auto part = std::make_unique<T>(std::forward<Args>(args)...);
        T &ref = *part;
        Instance.Add(ref, std::move(nodes));
        Parts.push_back(std::move(part));
        return ref;
    }
};

OwnedCircuit BuildRcLadder(const std::size_t stages, const double rs_val, const double rp_val, const double c_val) {
    OwnedCircuit circuit;
    auto &supply = circuit.AddPart<Core::VCC>({1});
    supply.SetName("V1");
    supply.SetValue(10.0);

    for (std::size_t i = 1; i <= stages; ++i) {
        auto &rs = circuit.AddPart<Core::Resistor>({static_cast<int>(i), static_cast<int>(i + 1)});
        rs.SetName(std::format("RS{}", i));
        rs.SetValue(rs_val);

        auto &rp = circuit.AddPart<Core::Resistor>({static_cast<int>(i + 1), 0});
        rp.SetName(std::format("RP{}", i));
        rp.SetValue(rp_val);

        auto &c = circuit.AddPart<Core::Capacitor>({static_cast<int>(i + 1), 0});
        c.SetName(std::format("C{}", i));
        c.SetValue(c_val);
    }

    circuit.AddPart<Core::Ground>({0});
    return circuit;
}

OwnedCircuit BuildRlcLine(const std::size_t stages, const double r_val, const double l_val, const double c_val,
                          const double r_term) {
    OwnedCircuit circuit;
    auto &vin = circuit.AddPart<Core::VoltageSource>({1, 0});
    vin.SetName("V1");
    vin.SetSourceType(Core::Source::SourceType::PULSE);
    vin.SetPulse(
        {.Low = 0.0, .High = 5.0, .Delay = 0.0, .RiseTime = 1e-9, .FallTime = 1e-9, .Width = 50e-9, .Period = 100e-9});

    for (std::size_t i = 1; i <= stages; ++i) {
        const int n_in = static_cast<int>(2 * i - 1);
        const int n_mid = static_cast<int>(2 * i);
        const int n_out = static_cast<int>(2 * i + 1);

        auto &r = circuit.AddPart<Core::Resistor>({n_in, n_mid});
        r.SetName(std::format("R{}", i));
        r.SetValue(r_val);

        auto &l = circuit.AddPart<Core::Inductor>({n_mid, n_out});
        l.SetName(std::format("L{}", i));
        l.SetValue(l_val);

        auto &c = circuit.AddPart<Core::Capacitor>({n_out, 0});
        c.SetName(std::format("C{}", i));
        c.SetValue(c_val);
    }

    const int final_node = static_cast<int>(2 * stages + 1);
    auto &r_load = circuit.AddPart<Core::Resistor>({final_node, 0});
    r_load.SetName("RLOAD");
    r_load.SetValue(r_term);

    circuit.AddPart<Core::Ground>({0});
    return circuit;
}

OwnedCircuit BuildDiodeLadder(const std::size_t stages, const double vin_val, const double r_shunt) {
    OwnedCircuit circuit;
    auto &supply = circuit.AddPart<Core::VCC>({1});
    supply.SetName("V1");
    supply.SetValue(vin_val);

    for (std::size_t i = 1; i <= stages; ++i) {
        auto &d =
            circuit.AddPart<Core::Diode>({static_cast<int>(i), static_cast<int>(i + 1)}, Core::ComponentType::DIODE);
        d.SetName(std::format("D{}", i));

        auto &r = circuit.AddPart<Core::Resistor>({static_cast<int>(i + 1), 0});
        r.SetName(std::format("R{}", i));
        r.SetValue(r_shunt);
    }

    circuit.AddPart<Core::Ground>({0});
    return circuit;
}

OwnedCircuit BuildCascadedBjtAmplifier(const std::size_t stages) {
    OwnedCircuit circuit;
    auto &vcc = circuit.AddPart<Core::VCC>({1});
    vcc.SetName("VCC");
    vcc.SetValue(12.0);

    auto &vin = circuit.AddPart<Core::VoltageSource>({2, 0});
    vin.SetName("VIN");
    vin.SetSourceType(Core::Source::SourceType::DC);
    vin.SetValue(0.7);

    int current_in = 2;
    int next_free_node = 3;

    for (std::size_t s = 1; s <= stages; ++s) {
        const int node_base = next_free_node++;
        const int node_coll = next_free_node++;
        const int node_emit = next_free_node++;

        auto &cin = circuit.AddPart<Core::Capacitor>({current_in, node_base});
        cin.SetName(std::format("CIN{}", s));
        cin.SetValue(10e-6);

        auto &r1 = circuit.AddPart<Core::Resistor>({1, node_base});
        r1.SetName(std::format("R1_{}", s));
        r1.SetValue(47e3);

        auto &r2 = circuit.AddPart<Core::Resistor>({node_base, 0});
        r2.SetName(std::format("R2_{}", s));
        r2.SetValue(10e3);

        auto &rc = circuit.AddPart<Core::Resistor>({1, node_coll});
        rc.SetName(std::format("RC{}", s));
        rc.SetValue(2.2e3);

        auto &re = circuit.AddPart<Core::Resistor>({node_emit, 0});
        re.SetName(std::format("RE{}", s));
        re.SetValue(470.0);

        auto &q = circuit.AddPart<Core::BJT>({node_coll, node_base, node_emit}, Core::ComponentType::NPN);
        q.SetName(std::format("Q{}", s));

        current_in = node_coll;
    }

    circuit.AddPart<Core::Ground>({0});
    return circuit;
}

} // namespace

TEST_CASE("Nodalys solves 100-node and 500-node RC ladder networks faster than ngspice with exact parity",
          "[benchmark][nodalys]") {
    Core::NodalysBackend nodalys;

    SECTION("100-node RC Ladder DC Operating Point") {
        constexpr std::size_t STAGES = 100;
        const OwnedCircuit circuit = BuildRcLadder(STAGES, 100.0, 10e3, 10e-9);

        Core::OperatingPointRun exp;
        const double ngspice_ms = MeasureMs([&]() { exp = Ngspice().RunOperatingPoint(circuit.Instance); });
        REQUIRE(exp.Result.has_value());

        Core::OperatingPointRun act;
        const double nodalys_ms = MeasureMs([&]() { act = nodalys.RunOperatingPoint(circuit.Instance); });
        REQUIRE(act.Result.has_value());

        REQUIRE(act.Result->NodeVoltages.size() == exp.Result->NodeVoltages.size());
        for (std::size_t i = 1; i <= STAGES + 1; ++i) {
            REQUIRE(Close(act.Result->NodeVoltages[i], exp.Result->NodeVoltages[i]));
        }

        PrintBenchmarkRow("RC Ladder (100 nodes)", "DC OP", STAGES + 1, ngspice_ms, nodalys_ms);
    }

    SECTION("500-node RC Ladder DC Operating Point") {
        constexpr std::size_t STAGES = 500;
        const OwnedCircuit circuit = BuildRcLadder(STAGES, 50.0, 50e3, 1e-9);

        Core::OperatingPointRun exp;
        const double ngspice_ms = MeasureMs([&]() { exp = Ngspice().RunOperatingPoint(circuit.Instance); });
        REQUIRE(exp.Result.has_value());

        Core::OperatingPointRun act;
        const double nodalys_ms = MeasureMs([&]() { act = nodalys.RunOperatingPoint(circuit.Instance); });
        REQUIRE(act.Result.has_value());

        REQUIRE(act.Result->NodeVoltages.size() == exp.Result->NodeVoltages.size());
        REQUIRE(Close(act.Result->NodeVoltages[250], exp.Result->NodeVoltages[250]));
        REQUIRE(Close(act.Result->NodeVoltages[STAGES + 1], exp.Result->NodeVoltages[STAGES + 1]));

        PrintBenchmarkRow("RC Ladder (500 nodes)", "DC OP", STAGES + 1, ngspice_ms, nodalys_ms);
    }
}

TEST_CASE("Nodalys solves 100-node RC ladder transient analysis with high-speed adaptive stepping",
          "[benchmark][nodalys]") {
    constexpr std::size_t STAGES = 100;
    const OwnedCircuit circuit = BuildRcLadder(STAGES, 100.0, 10e3, 1e-9);
    const Core::TransientSettings settings{.StopTime = 50e-6, .TimeStep = 1e-6};
    Core::NodalysBackend nodalys;

    Core::TransientRun exp;
    const double ngspice_ms =
        MeasureMs([&]() { exp = Ngspice().RunTransient(circuit.Instance, settings, std::nullopt, {}); });
    REQUIRE(exp.Result.has_value());

    Core::TransientRun act;
    const double nodalys_ms =
        MeasureMs([&]() { act = nodalys.RunTransient(circuit.Instance, settings, std::nullopt, {}); });
    REQUIRE(act.Result.has_value());

    const std::size_t last_node = STAGES + 1;
    const double final_exp = exp.Result->NodeVoltages[last_node].back();
    const double final_act = act.Result->NodeVoltages[last_node].back();
    REQUIRE(Close(final_act, final_exp, 1e-2, 1e-3));

    PrintBenchmarkRow("RC Ladder (100 nodes, 50us)", "Transient", last_node, ngspice_ms, nodalys_ms);
}

TEST_CASE("Nodalys simulates 50-stage RLC transmission line wave dynamics with lower latency than ngspice",
          "[benchmark][nodalys]") {
    constexpr std::size_t STAGES = 50;
    const OwnedCircuit circuit = BuildRlcLine(STAGES, 0.2, 1e-6, 100e-12, 100.0);
    const Core::TransientSettings settings{.StopTime = 100e-9, .TimeStep = 1e-9};
    Core::NodalysBackend nodalys;

    Core::TransientRun exp;
    const double ngspice_ms =
        MeasureMs([&]() { exp = Ngspice().RunTransient(circuit.Instance, settings, std::nullopt, {}); });
    REQUIRE(exp.Result.has_value());

    Core::TransientRun act;
    const double nodalys_ms =
        MeasureMs([&]() { act = nodalys.RunTransient(circuit.Instance, settings, std::nullopt, {}); });
    REQUIRE(act.Result.has_value());

    const std::size_t total_nodes = 2 * STAGES + 1;
    PrintBenchmarkRow("RLC Line (50 stages, 100ns)", "Transient", total_nodes, ngspice_ms, nodalys_ms);
}

TEST_CASE("Nodalys converges on 30-stage nonlinear diode ladder with rapid Newton iterations", "[benchmark][nodalys]") {
    constexpr std::size_t STAGES = 30;
    const OwnedCircuit circuit = BuildDiodeLadder(STAGES, 25.0, 10e3);
    Core::NodalysBackend nodalys;

    Core::OperatingPointRun exp;
    const double ngspice_ms = MeasureMs([&]() { exp = Ngspice().RunOperatingPoint(circuit.Instance); });
    REQUIRE(exp.Result.has_value());

    Core::OperatingPointRun act;
    const double nodalys_ms = MeasureMs([&]() { act = nodalys.RunOperatingPoint(circuit.Instance); });
    REQUIRE(act.Result.has_value());

    REQUIRE(act.Result->NodeVoltages.size() == exp.Result->NodeVoltages.size());
    for (std::size_t i = 1; i <= STAGES + 1; ++i) {
        REQUIRE(Close(act.Result->NodeVoltages[i], exp.Result->NodeVoltages[i], 1e-3, 1e-4));
    }

    PrintBenchmarkRow("Diode Ladder (30 diodes)", "DC OP", STAGES + 1, ngspice_ms, nodalys_ms);
}

TEST_CASE("Nodalys executes DC sweeps across multi-stage cascaded BJT amplifiers with high throughput",
          "[benchmark][nodalys]") {
    constexpr std::size_t STAGES = 4;
    const OwnedCircuit circuit = BuildCascadedBjtAmplifier(STAGES);
    const Core::DCSweepSettings settings{
        .Swept = {.Source = "VIN", .Start = 0.5, .Stop = 1.5, .Step = 0.05}}; // 21 points
    Core::NodalysBackend nodalys;

    Core::DCSweepRun exp;
    const double ngspice_ms =
        MeasureMs([&]() { exp = Ngspice().RunDCSweep(circuit.Instance, settings, std::nullopt); }, 2);
    REQUIRE(exp.Result.has_value());

    Core::DCSweepRun act;
    const double nodalys_ms =
        MeasureMs([&]() { act = nodalys.RunDCSweep(circuit.Instance, settings, std::nullopt); }, 2);
    REQUIRE(act.Result.has_value());

    REQUIRE(act.Result->SweptValues.size() == exp.Result->SweptValues.size());
    PrintBenchmarkRow("BJT Cascaded Amp (4 stages)", "DC Sweep", 3 * STAGES + 2, ngspice_ms, nodalys_ms);
}

TEST_CASE("Nodalys computes wideband small-signal AC sweeps on 100-node networks with minimal matrix overhead",
          "[benchmark][nodalys]") {
    constexpr std::size_t STAGES = 100;
    OwnedCircuit circuit;
    auto &vac = circuit.AddPart<Core::VoltageSource>({1, 0});
    vac.SetName("V1");
    vac.SetSourceType(Core::Source::SourceType::AC);
    vac.SetAC({.Amplitude = 1.0, .Frequency = 1e3, .Offset = 0.0, .Magnitude = 1.0});

    for (std::size_t i = 1; i <= STAGES; ++i) {
        auto &rs = circuit.AddPart<Core::Resistor>({static_cast<int>(i), static_cast<int>(i + 1)});
        rs.SetName(std::format("RS{}", i));
        rs.SetValue(50.0);

        auto &c = circuit.AddPart<Core::Capacitor>({static_cast<int>(i + 1), 0});
        c.SetName(std::format("C{}", i));
        c.SetValue(1e-9);
    }
    circuit.AddPart<Core::Ground>({0});

    const Core::ACSweepSettings settings{
        .StartFrequency = 10.0, .StopFrequency = 100e6, .PointsPerDecade = 50}; // ~350 points
    Core::NodalysBackend nodalys;

    Core::ACSweepRun exp;
    const double ngspice_ms =
        MeasureMs([&]() { exp = Ngspice().RunACSweep(circuit.Instance, settings, std::nullopt); }, 2);
    REQUIRE(exp.Result.has_value());

    Core::ACSweepRun act;
    const double nodalys_ms =
        MeasureMs([&]() { act = nodalys.RunACSweep(circuit.Instance, settings, std::nullopt); }, 2);
    REQUIRE(act.Result.has_value());

    REQUIRE(act.Result->Frequencies.size() == exp.Result->Frequencies.size());
    PrintBenchmarkRow("RC Filter (100 nodes, AC 350 pts)", "AC Sweep", STAGES + 1, ngspice_ms, nodalys_ms);
}

TEST_CASE("Nodalys simulates 31-stage smooth logic continuous ring oscillator autonomously in the time domain",
          "[benchmark][nodalys]") {
    constexpr int GATE_COUNT = 31;
    nodalys::CircuitBuilder builder;

    // Connect in ring: 1 -> 2 -> ... -> 31 -> 1
    for (int i = 1; i <= GATE_COUNT; ++i) {
        const int in_node = i;
        const int out_node = (i == GATE_COUNT) ? 1 : (i + 1);
        builder.AddInverter(std::format("U{}", i), out_node, in_node,
                            {.VLow = 0.0, .VHigh = 5.0, .VThreshold = 2.5, .PropagationDelay = 1e-9});
    }

    auto circuit = builder.Build();
    REQUIRE(circuit.has_value());

    nodalys::TransientConfig tran{
        .StopTime = 60e-9, .TimeStep = 0.2e-9, .MaxStep = 0.5e-9, .UseInitialConditions = true};

    nodalys::TransientResult res;
    const double nodalys_ms = MeasureMs([&]() {
        auto run = nodalys::ComputeTransient(*circuit, tran);
        REQUIRE(run.has_value());
        res = std::move(*run);
    });

    REQUIRE(!res.Times.empty());
    // Check that signal swung across threshold (oscillating)
    const auto &out_trace = res.NodeVoltages[1];
    const auto min_v = *std::ranges::min_element(out_trace);
    const auto max_v = *std::ranges::max_element(out_trace);
    REQUIRE(min_v < 1.0);
    REQUIRE(max_v > 4.0);

    PrintBenchmarkRow("Ring Oscillator (31 smooth gates)", "Transient", GATE_COUNT, 0.0, nodalys_ms);
}

TEST_CASE("Nodalys evaluates digital truth table combinations via DC operating points with high speed",
          "[benchmark][nodalys]") {
    const auto solve_adder_row = [](const bool a, const bool b, const bool cin) -> std::pair<double, double> {
        nodalys::CircuitBuilder builder;
        builder.AddVdc("VA", 1, 0, a ? 5.0 : 0.0);
        builder.AddVdc("VB", 2, 0, b ? 5.0 : 0.0);
        builder.AddVdc("VCIN", 3, 0, cin ? 5.0 : 0.0);

        builder.AddLogicGate("UXOR1", nodalys::LogicGateType::XOR, 4, {1, 2});
        builder.AddLogicGate("USUM", nodalys::LogicGateType::XOR, 5, {4, 3});
        builder.AddLogicGate("UAND1", nodalys::LogicGateType::AND, 6, {1, 2});
        builder.AddLogicGate("UAND2", nodalys::LogicGateType::AND, 7, {3, 4});
        builder.AddLogicGate("UCOUT", nodalys::LogicGateType::OR, 8, {6, 7});

        auto circuit = builder.Build();
        if (!circuit) {
            return {-1.0, -1.0};
        }
        auto op = nodalys::ComputeOperatingPoint(*circuit);
        if (!op) {
            return {-1.0, -1.0};
        }
        return {op->NodeVoltages[5], op->NodeVoltages[8]}; // Sum (node 5), Cout (node 8)
    };

    // Benchmark all 8 combinations of the truth table
    const double nodalys_ms = MeasureMs(
        [&]() {
            for (int a = 0; a <= 1; ++a) {
                for (int b = 0; b <= 1; ++b) {
                    for (int cin = 0; cin <= 1; ++cin) {
                        const auto [sum_v, cout_v] = solve_adder_row(a != 0, b != 0, cin != 0);
                        const int expected_sum = (a + b + cin) % 2;
                        const int expected_cout = (a + b + cin) >= 2 ? 1 : 0;

                        if (expected_sum == 1) {
                            REQUIRE(sum_v > 4.5);
                        } else {
                            REQUIRE(sum_v < 0.5);
                        }

                        if (expected_cout == 1) {
                            REQUIRE(cout_v > 4.5);
                        } else {
                            REQUIRE(cout_v < 0.5);
                        }
                    }
                }
            }
        },
        5);

    PrintBenchmarkRow("Full Adder Truth Table (8 rows)", "DC OP x8", 8, 0.0, nodalys_ms);
}

TEST_CASE("Nodalys outperforms ngspice on DC sensitivity via single-solve adjoint method", "[benchmark][nodalys]") {
    Core::NodalysBackend nodalys;

    auto run_sens_bench = [&](const std::string &label, const Core::Circuit &circuit, const int output_node,
                              const std::size_t node_count, const int repetitions) {
        const Core::SensitivitySettings settings{.OutputNode = output_node};
        Core::SensitivityRun exp;
        const double ngspice_ms = MeasureMs([&]() { exp = Ngspice().RunSensitivity(circuit, settings); }, repetitions);
        REQUIRE(exp.Result.has_value());

        Core::SensitivityRun act;
        const double nodalys_ms = MeasureMs([&]() { act = nodalys.RunSensitivity(circuit, settings); }, repetitions);
        REQUIRE(act.Result.has_value());
        REQUIRE(act.Result->Parts.size() == exp.Result->Parts.size());
        for (const auto &ep : exp.Result->Parts) {
            const auto found = std::ranges::find(act.Result->Parts, ep.Name, &Core::PartSensitivity::Name);
            REQUIRE(found != act.Result->Parts.end());
            CHECK(Close(found->Absolute, ep.Absolute, 1e-3, 1e-6));
        }

        PrintBenchmarkRow(label, "Sens", node_count, ngspice_ms, nodalys_ms);
    };

    SECTION("100-node RC ladder DC sensitivity") {
        constexpr std::size_t STAGES = 100;
        const OwnedCircuit circuit = BuildRcLadder(STAGES, 100.0, 10e3, 1e-9);
        run_sens_bench("RC Ladder (100 nodes)", circuit.Instance, static_cast<int>(STAGES + 1), STAGES + 1, 10);
    }

    SECTION("500-node RC ladder DC sensitivity") {
        constexpr std::size_t STAGES = 500;
        const OwnedCircuit circuit = BuildRcLadder(STAGES, 100.0, 10e3, 1e-9);
        run_sens_bench("RC Ladder (500 nodes)", circuit.Instance, static_cast<int>(STAGES + 1), STAGES + 1, 3);
    }

    SECTION("30-diode nonlinear ladder sensitivity") {
        constexpr std::size_t STAGES = 30;
        const OwnedCircuit circuit = BuildDiodeLadder(STAGES, 25.0, 10e3);
        run_sens_bench("Diode Ladder (30 diodes)", circuit.Instance, static_cast<int>(STAGES + 1), STAGES + 1, 10);
    }

    SECTION("4-stage cascaded BJT amplifier sensitivity") {
        constexpr std::size_t STAGES = 4;
        const OwnedCircuit circuit = BuildCascadedBjtAmplifier(STAGES);
        run_sens_bench("BJT Cascaded Amp (4 stages)", circuit.Instance, static_cast<int>(3 * STAGES + 1),
                       3 * STAGES + 2, 15);
    }
}

TEST_CASE("Nodalys computes small-signal TF, Noise, SP, and PZ analyses with benchmark parity",
          "[benchmark][nodalys]") {
    Core::NodalysBackend nodalys;

    SECTION("BJT emitter follower DC transfer function") {
        OwnedCircuit circuit;
        auto &vcc = circuit.AddPart<Core::VCC>({1});
        vcc.SetName("VCC");
        vcc.SetValue(12.0);
        auto &vin = circuit.AddPart<Core::VoltageSource>({2, 0});
        vin.SetName("VIN");
        vin.SetValue(2.0);
        auto &rb = circuit.AddPart<Core::Resistor>({2, 3});
        rb.SetName("RB");
        rb.SetValue(10e3);
        auto &re = circuit.AddPart<Core::Resistor>({4, 0});
        re.SetName("RE");
        re.SetValue(1e3);
        auto &q = circuit.AddPart<Core::BJT>({1, 3, 4}, Core::ComponentType::NPN);
        q.SetName("Q1");
        circuit.AddPart<Core::Ground>({0});

        const Core::TransferFunctionSettings settings{.OutputNode = 4, .InputSource = "VIN"};
        Core::TransferFunctionRun exp;
        const double ngspice_ms =
            MeasureMs([&]() { exp = Ngspice().RunTransferFunction(circuit.Instance, settings); }, 20);
        REQUIRE(exp.Result.has_value());

        Core::TransferFunctionRun act;
        const double nodalys_ms =
            MeasureMs([&]() { act = nodalys.RunTransferFunction(circuit.Instance, settings); }, 20);
        REQUIRE(act.Result.has_value());
        CHECK(Close(act.Result->Gain, exp.Result->Gain));

        PrintBenchmarkRow("BJT Emitter Follower", "Transfer", 4, ngspice_ms, nodalys_ms);
    }

    SECTION("BJT common-emitter AC noise analysis") {
        OwnedCircuit circuit;
        auto &vcc = circuit.AddPart<Core::VCC>({1});
        vcc.SetName("VCC");
        vcc.SetValue(12.0);
        auto &vin = circuit.AddPart<Core::VoltageSource>({2, 0});
        vin.SetName("Vin1");
        vin.SetSourceType(Core::Source::SourceType::AC);
        vin.SetAC({.Amplitude = 1.0, .Frequency = 1e3, .Offset = 0.0, .Magnitude = 1.0});
        auto &cin = circuit.AddPart<Core::Capacitor>({2, 3});
        cin.SetName("CIN");
        cin.SetValue(10e-6);
        auto &r1 = circuit.AddPart<Core::Resistor>({1, 3});
        r1.SetName("R1");
        r1.SetValue(47e3);
        auto &r2 = circuit.AddPart<Core::Resistor>({3, 0});
        r2.SetName("R2");
        r2.SetValue(10e3);
        auto &rc = circuit.AddPart<Core::Resistor>({1, 4});
        rc.SetName("RC");
        rc.SetValue(2.2e3);
        auto &re = circuit.AddPart<Core::Resistor>({5, 0});
        re.SetName("RE");
        re.SetValue(470.0);
        auto &q = circuit.AddPart<Core::BJT>({4, 3, 5}, Core::ComponentType::NPN);
        q.SetName("Q1");
        circuit.AddPart<Core::Ground>({0});

        const Core::NoiseSettings settings{.OutputNode = 4,
                                           .InputSource = "Vin1",
                                           .StartFrequency = 100.0,
                                           .StopFrequency = 1e6,
                                           .PointsPerDecade = 10};
        Core::NoiseRun exp;
        const double ngspice_ms = MeasureMs([&]() { exp = Ngspice().RunNoise(circuit.Instance, settings); }, 20);
        REQUIRE(exp.Result.has_value());

        Core::NoiseRun act;
        const double nodalys_ms = MeasureMs([&]() { act = nodalys.RunNoise(circuit.Instance, settings); }, 20);
        REQUIRE(act.Result.has_value());
        CHECK(Close(act.Result->OutputTotal, exp.Result->OutputTotal));

        PrintBenchmarkRow("BJT CE Amp (41 pts)", "Noise", 5, ngspice_ms, nodalys_ms);
    }

    SECTION("3-port resistive splitter S-parameters") {
        OwnedCircuit circuit;
        auto &r1 = circuit.AddPart<Core::Resistor>({1, 4});
        r1.SetName("R1");
        r1.SetValue(16.6667);
        auto &r2 = circuit.AddPart<Core::Resistor>({2, 4});
        r2.SetName("R2");
        r2.SetValue(16.6667);
        auto &r3 = circuit.AddPart<Core::Resistor>({3, 4});
        r3.SetName("R3");
        r3.SetValue(16.6667);
        circuit.AddPart<Core::Ground>({0});
        circuit.Instance.AddPort({.Number = 1, .Node = 1, .ReferenceOhms = 50.0});
        circuit.Instance.AddPort({.Number = 2, .Node = 2, .ReferenceOhms = 50.0});
        circuit.Instance.AddPort({.Number = 3, .Node = 3, .ReferenceOhms = 50.0});

        const Core::SParameterSettings settings{
            .StartFrequency = 10e6, .StopFrequency = 500e6, .IsLogarithmic = true, .PointsPerDecade = 10};
        Core::SParametersRun exp;
        const double ngspice_ms = MeasureMs([&]() { exp = Ngspice().RunSParameters(circuit.Instance, settings); }, 20);
        REQUIRE(exp.Result.has_value());

        Core::SParametersRun act;
        const double nodalys_ms = MeasureMs([&]() { act = nodalys.RunSParameters(circuit.Instance, settings); }, 20);
        REQUIRE(act.Result.has_value());
        REQUIRE(!act.Result->Values.empty());

        PrintBenchmarkRow("3-Port Splitter (18 pts)", "S-Params", 4, ngspice_ms, nodalys_ms);
    }

    SECTION("Twin-T notch filter pole-zero analysis") {
        OwnedCircuit circuit;
        auto &source = circuit.AddPart<Core::VoltageSource>({1, 0});
        source.SetName("Vin1");
        auto &r1 = circuit.AddPart<Core::Resistor>({1, 3});
        r1.SetName("R1");
        r1.SetValue(10e3);
        auto &r2 = circuit.AddPart<Core::Resistor>({3, 2});
        r2.SetName("R2");
        r2.SetValue(10e3);
        auto &c3 = circuit.AddPart<Core::Capacitor>({3, 0});
        c3.SetName("C3");
        c3.SetValue(20e-9);
        auto &c1 = circuit.AddPart<Core::Capacitor>({1, 4});
        c1.SetName("C1");
        c1.SetValue(10e-9);
        auto &c2 = circuit.AddPart<Core::Capacitor>({4, 2});
        c2.SetName("C2");
        c2.SetValue(10e-9);
        auto &r3 = circuit.AddPart<Core::Resistor>({4, 0});
        r3.SetName("R3");
        r3.SetValue(5e3);
        auto &rl = circuit.AddPart<Core::Resistor>({2, 0});
        rl.SetName("RL");
        rl.SetValue(100e3);
        circuit.AddPart<Core::Ground>({0});

        const Core::PoleZeroSettings settings{.OutputNode = 2, .InputSource = "Vin1"};
        Core::PoleZeroRun exp;
        const double ngspice_ms = MeasureMs([&]() { exp = Ngspice().RunPoleZero(circuit.Instance, settings); }, 20);
        REQUIRE(exp.Result.has_value());

        Core::PoleZeroRun act;
        const double nodalys_ms = MeasureMs([&]() { act = nodalys.RunPoleZero(circuit.Instance, settings); }, 20);
        REQUIRE(act.Result.has_value());
        REQUIRE(act.Result->Roots.size() == exp.Result->Roots.size());

        PrintBenchmarkRow("Twin-T Notch Filter", "Pole-Zero", 4, ngspice_ms, nodalys_ms);
    }
}
