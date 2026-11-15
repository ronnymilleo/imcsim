/**
 * @file    result_store_tests.cpp
 * @brief   Tests for the store of the simulation results: versions, chunks of a transient, steps and what a change of
 *          the circuit drops.
 */

#include "results/result_store.h"
#include "test_printers.h"
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <vector>

TEST_CASE("Each analysis has its own results version", "[result_store]") {
    GUI::ResultStore results;
    const std::size_t transient_version = results.GetTransientVersion();
    const std::size_t sweep_version = results.GetACSweepVersion();

    results.SetTransient(Core::Transient{});
    CHECK(results.GetTransientVersion() != transient_version);
    CHECK(results.GetACSweepVersion() == sweep_version);

    results.SetACSweep(Core::ACSweep{});
    results.SetOperatingPoint(Core::OperatingPoint{});
    CHECK(results.GetACSweepVersion() != sweep_version);
}

TEST_CASE("Chunks of a transient make the result grow, and only the first counts as a new result", "[result_store]") {
    GUI::ResultStore results;
    const auto make_chunk = [](const std::vector<double> &times) {
        Core::Transient chunk;
        chunk.Times = times;
        chunk.NodeVoltages = {std::vector<double>(times.size(), 0.0), times};
        chunk.Currents = {{"R1", times}};
        return chunk;
    };

    results.AppendTransient(make_chunk({0.0, 1.0}));
    REQUIRE(results.GetTransient().has_value());
    // A transient that is still being made is not complete
    CHECK_FALSE(results.GetTransient()->IsComplete);
    const std::size_t result_version = results.GetTransientVersion();
    const std::size_t data_version = results.GetTransientDataVersion();

    results.AppendTransient(make_chunk({2.0, 3.0}));
    CHECK(results.GetTransient()->Times == std::vector<double>{0.0, 1.0, 2.0, 3.0});
    CHECK(results.GetTransient()->NodeVoltages[1] == std::vector<double>{0.0, 1.0, 2.0, 3.0});
    CHECK(results.GetTransient()->Currents[0].Values == std::vector<double>{0.0, 1.0, 2.0, 3.0});
    CHECK(results.GetTransientVersion() == result_version);
    CHECK(results.GetTransientDataVersion() == data_version + 1);

    // A chunk made for another circuit, with other nodes or other currents, is dropped
    Core::Transient other = make_chunk({4.0});
    other.NodeVoltages.push_back({4.0});
    results.AppendTransient(other);
    other = make_chunk({4.0});
    other.Currents[0].Name = "R2";
    results.AppendTransient(other);
    CHECK(results.GetTransient()->Times.size() == 4);
    CHECK(results.GetTransientDataVersion() == data_version + 1);

    // The whole run replaces what the chunks made, as a new result
    results.SetTransient(make_chunk({0.0, 5.0}));
    CHECK(results.GetTransient()->IsComplete);
    CHECK(results.GetTransientVersion() == result_version + 1);
    CHECK(results.GetTransientDataVersion() == data_version + 2);
}

TEST_CASE("A change of the circuit drops every result, and each drop is kept apart from a new result",
          "[result_store]") {
    GUI::ResultStore results;
    results.SetOperatingPoint(Core::OperatingPoint{{0.0, 5.0}});
    results.SetTransient(Core::Transient{});
    results.SetACSweep(Core::ACSweep{});
    results.SetDCSweep(Core::DCSweep{});
    results.SetNoise(Core::Noise{});
    results.SetTransferFunction(Core::TransferFunction{});
    results.SetSensitivity(Core::Sensitivity{});
    results.SetPoleZero(Core::PoleZero{});
    results.SetSParameters(Core::SParameters{});
    results.SetTruthTable(Core::TruthTable{});
    results.SetHarmonicBalance(Core::HarmonicBalance{});
    results.SetMonteCarlo(GUI::MonteCarloTable{});

    results.OnCircuitChanged();

    CHECK_FALSE(results.GetOperatingPoint());
    CHECK_FALSE(results.GetTransient());
    CHECK_FALSE(results.GetACSweep());
    CHECK_FALSE(results.GetDCSweep());
    CHECK_FALSE(results.GetNoise());
    CHECK_FALSE(results.GetTransferFunction());
    CHECK_FALSE(results.GetSensitivity());
    CHECK_FALSE(results.GetPoleZero());
    CHECK_FALSE(results.GetSParameters());
    CHECK_FALSE(results.GetTruthTable());
    CHECK_FALSE(results.GetHarmonicBalance());
    CHECK_FALSE(results.GetMonteCarlo());
}

TEST_CASE("A change in a live run keeps the transient and drops the other results", "[result_store]") {
    GUI::ResultStore results;
    results.SetTransient(Core::Transient{.Times = {0.0}, .NodeVoltages = {}, .Currents = {}, .Warnings = {}});
    results.SetOperatingPoint(Core::OperatingPoint{});
    results.SetMonteCarlo(GUI::MonteCarloTable{});
    const std::size_t transient_version = results.GetTransientVersion();
    const std::size_t monte_carlo_version = results.GetMonteCarloVersion();

    results.OnLiveChanged();

    CHECK(results.GetTransient().has_value());
    CHECK(results.GetTransientVersion() == transient_version);
    CHECK_FALSE(results.GetOperatingPoint().has_value());
    CHECK_FALSE(results.GetMonteCarlo().has_value());
    // Dropping the table is a change the windows see
    CHECK(results.GetMonteCarloVersion() != monte_carlo_version);
}

TEST_CASE("A stepped transient shows the selected step and a single run replaces the steps", "[result_store]") {
    GUI::ResultStore results;
    GUI::StepFamily<Core::Transient> steps{
        .Parameter = "R1",
        .Unit = "Ohm",
        .Values = {1e3, 2e3},
        .Results = {Core::Transient{.Times = {0.0}}, Core::Transient{.Times = {0.0, 1.0}}},
        .Selected = 0};
    results.SetTransientSteps(steps);
    REQUIRE(results.GetTransient().has_value());
    CHECK(results.GetTransient()->Times.size() == 1);

    const std::size_t data_version = results.GetTransientDataVersion();
    results.SelectTransientStep(1);
    CHECK(results.GetTransient()->Times.size() == 2);
    CHECK(results.GetTransientSteps()->Selected == 1);
    CHECK(results.GetTransientDataVersion() != data_version);
    // A step out of range is ignored
    results.SelectTransientStep(9);
    CHECK(results.GetTransientSteps()->Selected == 1);

    results.SetTransient(Core::Transient{});
    CHECK_FALSE(results.GetTransientSteps().has_value());
}

TEST_CASE("A stepped AC sweep shows the selected step", "[result_store]") {
    GUI::ResultStore results;
    Core::ACSweep first;
    first.Frequencies = {1.0};
    Core::ACSweep second;
    second.Frequencies = {1.0, 2.0};
    results.SetACSweepSteps({.Parameter = "C1", .Unit = "F", .Values = {1e-9, 2e-9}, .Results = {first, second}});
    REQUIRE(results.GetACSweep().has_value());
    CHECK(results.GetACSweep()->Frequencies.size() == 1);

    const std::size_t version = results.GetACSweepVersion();
    results.SelectACSweepStep(1);
    CHECK(results.GetACSweep()->Frequencies.size() == 2);
    CHECK(results.GetACSweepVersion() != version);
}

TEST_CASE("Storing a trace drops the table of an earlier Monte Carlo analysis", "[result_store]") {
    GUI::ResultStore results;
    results.SetMonteCarlo(GUI::MonteCarloTable{});
    REQUIRE(results.GetMonteCarlo().has_value());

    results.SetDCSweep(Core::DCSweep{});

    CHECK_FALSE(results.GetMonteCarlo().has_value());
}
