/**
 * @file    circuit_immutability_tests.cpp
 * @brief   Tests that a const Circuit is genuinely immutable across sequential and concurrent simulation runs.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <nodalys/nodalys.h>

#include <array>
#include <cmath>
#include <string>
#include <thread>
#include <vector>

namespace {

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

Nodalys::Circuit BuildDividerWithFuse() {
    Nodalys::CircuitBuilder builder(3);
    // 10 V across a 1 A fast fuse into a 1 ohm load drives ~9 A and blows the fuse quickly
    builder.AddVdc("V1", 1, 0, 10.0);
    builder.AddFuse("F1",
                    1,
                    2,
                    Nodalys::FuseParameters{.RatedCurrent = 1.0,
                                            .MeltingI2t = 1e-3,
                                            .ColdResistance = 0.05,
                                            .BlownResistance = 1e9});
    builder.AddResistor("R1", 2, 0, 1.0);
    auto built = builder.Build();
    REQUIRE(built.has_value());
    return std::move(*built);
}

Nodalys::Circuit BuildLiveDivider() {
    Nodalys::CircuitBuilder builder(3);
    builder.AddVdc("V1", 1, 0, 10.0);
    builder.AddResistor("R1", 1, 2, 1000.0);
    builder.AddResistor("R2", 2, 0, 1000.0);
    auto built = builder.Build();
    REQUIRE(built.has_value());
    return std::move(*built);
}

Nodalys::Circuit BuildConcurrentTestCircuit() {
    Nodalys::CircuitBuilder builder(4);
    builder.AddVnoise("V1",
                      1,
                      0,
                      Nodalys::NoiseParameters{.Offset = 5.0, .WhiteAmplitude = 0.25, .TimeStep = 50e-6},
                      1.0);
    builder.AddResistor("R1", 1, 2, 1000.0);
    builder.AddCapacitor("C1", 2, 0, 10e-9);
    builder.AddDiode("D1", 2, 3, Nodalys::DiodeParameters{});
    builder.AddResistor("R2", 3, 0, 1000.0);
    builder.AddPort(1, 1, 50.0);
    builder.AddPort(2, 2, 50.0);
    auto built = builder.Build();
    REQUIRE(built.has_value());
    return std::move(*built);
}

} // namespace

TEST_CASE("An empty default-constructed circuit reports an error instead of crashing",
          "[nodalys][circuit_immutability]") {
    const Nodalys::Circuit empty;
    const auto result = Nodalys::ComputeOperatingPoint(empty);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == "The circuit is empty");
}

TEST_CASE("A transient that blows a fuse leaves the const circuit intact for later runs",
          "[nodalys][circuit_immutability]") {
    const Nodalys::Circuit circuit = BuildDividerWithFuse();

    const auto op_before = Nodalys::ComputeOperatingPoint(circuit);
    REQUIRE(op_before.has_value());
    // Cold resistance 0.05 ohm in series with 1 ohm gives 10 / 1.05 V at node 2
    CHECK_THAT(op_before->NodeVoltages[2], WithinRel(10.0 / 1.05, 1e-4));

    const Nodalys::TransientConfig tran_cfg{.StopTime = 1e-3, .TimeStep = 10e-6, .MaxStep = 10e-6};
    const auto first_run = Nodalys::ComputeTransient(circuit, tran_cfg);
    REQUIRE(first_run.has_value());
    REQUIRE_FALSE(first_run->NodeVoltages[2].empty());
    // The fuse blows well before 1 ms, dropping node 2 near 0 V
    CHECK_THAT(first_run->NodeVoltages[2].back(), WithinAbs(0.0, 1e-3));

    const auto op_after = Nodalys::ComputeOperatingPoint(circuit);
    REQUIRE(op_after.has_value());
    CHECK_THAT(op_after->NodeVoltages[2], WithinRel(op_before->NodeVoltages[2], 1e-12));

    const auto second_run = Nodalys::ComputeTransient(circuit, tran_cfg);
    REQUIRE(second_run.has_value());
    REQUIRE(second_run->NodeVoltages[2].size() == first_run->NodeVoltages[2].size());
    CHECK_THAT(second_run->NodeVoltages[2].front(), WithinRel(first_run->NodeVoltages[2].front(), 1e-12));
    CHECK_THAT(second_run->NodeVoltages[2].back(), WithinAbs(first_run->NodeVoltages[2].back(), 1e-12));
}

TEST_CASE("Live resistor and voltage changes during a transient do not mutate the const circuit",
          "[nodalys][circuit_immutability]") {
    const Nodalys::Circuit circuit = BuildLiveDivider();

    bool has_sent_changes = false;
    Nodalys::RunMonitor monitor;
    monitor.ChunkSpan = 100e-6;
    monitor.TakeLiveChanges = [&has_sent_changes]() {
        if (has_sent_changes) {
            return std::vector<Nodalys::LiveChange>{};
        }
        has_sent_changes = true;
        return std::vector<Nodalys::LiveChange>{
            {.Element = "R1", .Quantity = Nodalys::LiveQuantity::RESISTANCE, .Value = 3000.0},
            {.Element = "V1", .Quantity = Nodalys::LiveQuantity::DC_VOLTAGE, .Value = 20.0}};
    };

    const Nodalys::TransientConfig tran_cfg{.StopTime = 500e-6, .TimeStep = 50e-6, .MaxStep = 50e-6};
    const auto tran = Nodalys::ComputeTransient(circuit, tran_cfg, false, monitor);
    REQUIRE(tran.has_value());

    // After the live run finishes, the original circuit must still be 10 V with R1 = R2 = 1 kOhm (5 V at node 2)
    const auto op = Nodalys::ComputeOperatingPoint(circuit);
    REQUIRE(op.has_value());
    CHECK_THAT(op->NodeVoltages[1], WithinRel(10.0, 1e-9));
    CHECK_THAT(op->NodeVoltages[2], WithinRel(5.0, 1e-9));
}

TEST_CASE("Concurrent analyses on one shared const circuit complete cleanly and deterministically",
          "[nodalys][circuit_immutability]") {
    const Nodalys::Circuit circuit = BuildConcurrentTestCircuit();

    const auto baseline_op = Nodalys::ComputeOperatingPoint(circuit);
    REQUIRE(baseline_op.has_value());

    const Nodalys::TransientConfig tran_seed_a{.StopTime = 500e-6, .TimeStep = 50e-6, .MaxStep = 50e-6, .Seed = 42};
    const Nodalys::TransientConfig tran_seed_b{.StopTime = 500e-6, .TimeStep = 50e-6, .MaxStep = 50e-6, .Seed = 99};
    const Nodalys::DcSweepConfig dc_cfg{.SweptSource = "V1", .SweptStart = 0.0, .SweptStop = 5.0, .SweptStep = 1.0};
    const Nodalys::AcSweepConfig ac_cfg{.StartFrequency = 100.0, .StopFrequency = 1e6, .PointsPerDecade = 5};
    const Nodalys::SParameterConfig sp_cfg{.SweepType = Nodalys::FrequencySweepType::LINEAR,
                                           .StartFrequency = 1e3,
                                           .StopFrequency = 1e6,
                                           .Points = 4,
                                           .Ports = circuit.GetPorts()};

    std::array<std::expected<Nodalys::TransientResult, std::string>, 4> tran_runs;
    std::expected<Nodalys::DcSweepResult, std::string> dc_run;
    std::expected<Nodalys::OperatingPointResult, std::string> op_run;
    std::expected<Nodalys::AcSweepResult, std::string> ac_run;
    std::expected<Nodalys::SParameterResult, std::string> sp_run;

    std::vector<std::thread> workers;
    workers.emplace_back([&]() { tran_runs[0] = Nodalys::ComputeTransient(circuit, tran_seed_a); });
    workers.emplace_back([&]() { tran_runs[1] = Nodalys::ComputeTransient(circuit, tran_seed_a); });
    workers.emplace_back([&]() { tran_runs[2] = Nodalys::ComputeTransient(circuit, tran_seed_b); });
    workers.emplace_back([&]() { tran_runs[3] = Nodalys::ComputeTransient(circuit, tran_seed_b); });
    workers.emplace_back([&]() { dc_run = Nodalys::ComputeDCSweep(circuit, dc_cfg); });
    workers.emplace_back([&]() { op_run = Nodalys::ComputeOperatingPoint(circuit); });
    workers.emplace_back([&]() { ac_run = Nodalys::ComputeACSweep(circuit, ac_cfg); });
    workers.emplace_back([&]() { sp_run = Nodalys::ComputeSParameters(circuit, sp_cfg); });
    for (std::thread &worker : workers) {
        worker.join();
    }

    for (const auto &run : tran_runs) {
        REQUIRE(run.has_value());
    }
    REQUIRE(dc_run.has_value());
    REQUIRE(op_run.has_value());
    REQUIRE(ac_run.has_value());
    REQUIRE(sp_run.has_value());

    // Runs sharing the same seed must match sample-for-sample despite running in parallel on the same const Circuit
    REQUIRE(tran_runs[0]->NodeVoltages[2] == tran_runs[1]->NodeVoltages[2]);
    REQUIRE(tran_runs[2]->NodeVoltages[2] == tran_runs[3]->NodeVoltages[2]);
    CHECK(tran_runs[0]->NodeVoltages[2] != tran_runs[2]->NodeVoltages[2]);
    CHECK_THAT(op_run->NodeVoltages[2], WithinRel(baseline_op->NodeVoltages[2], 1e-12));
    CHECK(ac_run->Frequencies.size() > 5);
    CHECK(sp_run->Frequencies.size() == 4);
}

TEST_CASE("CircuitBuilder supports fluent chaining and refuses repeated or moved-from Build() calls",
          "[nodalys][circuit_builder]") {
    Nodalys::CircuitBuilder builder;
    builder.AddVdc("V1", 1, 0, 10.0).AddResistor("R1", 1, 2, 1000.0).AddResistor("R2", 2, 0, 1000.0);

    const auto first = builder.Build();
    REQUIRE(first.has_value());
    const auto op = Nodalys::ComputeOperatingPoint(*first);
    REQUIRE(op.has_value());
    CHECK_THAT(op->NodeVoltages[2], WithinRel(5.0, 1e-9));

    const auto second = builder.Build();
    REQUIRE_FALSE(second.has_value());

    Nodalys::CircuitBuilder original;
    original.AddVdc("V1", 1, 0, 5.0).AddResistor("R1", 1, 0, 100.0);
    Nodalys::CircuitBuilder moved(std::move(original));
    const auto from_moved_from = original.Build();
    REQUIRE_FALSE(from_moved_from.has_value());
    const auto from_target = moved.Build();
    REQUIRE(from_target.has_value());
}
