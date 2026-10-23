/**
 * @file    transient_chunks_tests.cpp
 * @brief   Tests for the transient that ngspice runs in stretches of simulated time, which can be stopped and shown
 *          while it goes on.
 */

#include "components/capacitor.h"
#include "components/ground.h"
#include "components/resistor.h"
#include "components/voltage_source.h"
#include "examples.h"
#include "ngspice_library.h"
#include "schematic.h"
#include "simulator.h"
#include "test_printers.h"
#include <algorithm>
#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace {

// A 5 V source charging 1 uF through 1k, for 5 ms, from an empty capacitor
struct ChargingCapacitor {
    Core::VoltageSource Source;
    Core::Resistor Series;
    Core::Capacitor Shunt;
    Core::Ground Reference;
    Core::Circuit Circuit;
    Core::TransientSettings Settings{.StopTime = 5e-3, .TimeStep = 10e-6};

    ChargingCapacitor() {
        Source.SetName("V1");
        Source.SetValue(5.0);
        Series.SetName("R1");
        Series.SetValue(1e3);
        Shunt.SetName("C1");
        Shunt.SetValue(1e-6);
        Shunt.SetInitialCondition(0.0);
        Circuit.Add(Source, {1, 0});
        Circuit.Add(Series, {1, 2});
        Circuit.Add(Shunt, {2, 0});
        Circuit.Add(Reference, {0});
    }
};

// What a monitored run handed over: each chunk, so the tests see where the run was cut
struct Received {
    std::vector<Core::Transient> Chunks;
    Core::RunMonitor Monitor;

    explicit Received(Core::RunControl *control = nullptr) {
        Monitor.Control = control;
        Monitor.OnChunk = [this](Core::Transient chunk) { Chunks.push_back(std::move(chunk)); };
    }
};

// Value of a trace at a time, between the two points that surround it
double ValueAt(const std::vector<double> &times, const std::vector<double> &values, const double time) {
    const auto after = std::ranges::lower_bound(times, time);
    if (after == times.begin()) {
        return values.front();
    }
    if (after == times.end()) {
        return values.back();
    }
    const std::size_t index = static_cast<std::size_t>(after - times.begin());
    const double fraction = (time - times[index - 1]) / (times[index] - times[index - 1]);
    return values[index - 1] + fraction * (values[index] - values[index - 1]);
}

// Circuits that switch when a slow input barely passes a threshold, so the instant of the switching depends on the
// step sequence by a hundred microseconds or so; they are compared by what they reach, not point by point
constexpr auto THRESHOLD_SENSITIVE_EXAMPLES = std::to_array<std::string_view>({"Schmitt trigger"});

// Lowest and highest value of a trace within a window of time around an instant
std::pair<double, double> RangeAround(const std::vector<double> &times, const std::vector<double> &values,
                                      const double time, const double half_window) {
    double lowest = ValueAt(times, values, time - half_window);
    double highest = lowest;
    for (const double edge : {time, time + half_window}) {
        lowest = std::min(lowest, ValueAt(times, values, edge));
        highest = std::max(highest, ValueAt(times, values, edge));
    }
    const auto first = std::ranges::lower_bound(times, time - half_window);
    for (std::size_t index = static_cast<std::size_t>(first - times.begin());
         index < times.size() && times[index] <= time + half_window; ++index) {
        lowest = std::min(lowest, values[index]);
        highest = std::max(highest, values[index]);
    }
    return {lowest, highest};
}

double Span(const std::vector<double> &values) {
    const auto [lowest, highest] = std::ranges::minmax(values);
    return highest - lowest;
}

} // namespace

TEST_CASE("A monitored transient hands over its points in chunks that add up to the whole result",
          "[transient_chunks]") {
    ChargingCapacitor setup;
    Received received;
    const Core::TransientRun run = Core::RunTransient(setup.Circuit, setup.Settings, std::nullopt, received.Monitor);
    REQUIRE(run.Result.has_value());

    // The run was cut more than once, and every chunk has the points of each node and current
    CHECK(received.Chunks.size() > 2);
    std::vector<double> times;
    std::vector<double> capacitor_voltage;
    for (const Core::Transient &chunk : received.Chunks) {
        REQUIRE_FALSE(chunk.Times.empty());
        REQUIRE(chunk.NodeVoltages.size() == 3);
        CHECK(chunk.NodeVoltages[2].size() == chunk.Times.size());
        times.insert(times.end(), chunk.Times.begin(), chunk.Times.end());
        capacitor_voltage.insert(capacitor_voltage.end(), chunk.NodeVoltages[2].begin(), chunk.NodeVoltages[2].end());
    }
    CHECK(times == run.Result->Times);
    CHECK(capacitor_voltage == run.Result->NodeVoltages[2]);
    CHECK(run.Result->IsComplete);
    CHECK(run.Result->Times.back() == Catch::Approx(setup.Settings.StopTime));
}

TEST_CASE("A chunked transient follows the one-piece run, and the RC curve, away from where it was cut",
          "[transient_chunks]") {
    ChargingCapacitor setup;
    const Core::TransientRun one_piece = Core::RunTransient(setup.Circuit, setup.Settings);
    Received received;
    const Core::TransientRun chunked =
        Core::RunTransient(setup.Circuit, setup.Settings, std::nullopt, received.Monitor);
    REQUIRE(one_piece.Result.has_value());
    REQUIRE(chunked.Result.has_value());

    const double time_constant = 1e3 * 1e-6;
    for (std::size_t index = 0; index < chunked.Result->Times.size(); ++index) {
        const double time = chunked.Result->Times[index];
        const double expected = 5.0 * (1.0 - std::exp(-time / time_constant));
        CHECK(std::abs(chunked.Result->NodeVoltages[2][index] - expected) < 5e-3);
        const double other = ValueAt(one_piece.Result->Times, one_piece.Result->NodeVoltages[2], time);
        CHECK(std::abs(chunked.Result->NodeVoltages[2][index] - other) < 5e-3);
    }
}

TEST_CASE("A chunked transient matches the one-piece run on every example", "[transient_chunks][examples]") {
    for (const GUI::Example &example : GUI::GetExamples()) {
        INFO(example.Title);
        // The digital examples open everywhere, but run only where ngspice has the digital models
        if (std::string_view(example.Group) == "Digital" && !Core::AreDigitalModelsAvailable()) {
            continue;
        }
        auto loaded = GUI::LoadExample(example);
        REQUIRE(loaded.has_value());
        GUI::Schematic schematic;
        schematic.Replace(std::move(loaded->Elements), std::move(loaded->Wires), std::move(loaded->Settings));
        const Core::Circuit circuit = schematic.BuildCircuit();
        const Core::TransientSettings settings = schematic.GetSimulationSettings().Transient;

        const Core::TransientRun one_piece = Core::RunTransient(circuit, settings);
        Received received;
        const Core::TransientRun chunked = Core::RunTransient(circuit, settings, std::nullopt, received.Monitor);
        REQUIRE(one_piece.Result.has_value());
        REQUIRE(chunked.Result.has_value());
        CHECK(chunked.Result->IsComplete);
        CHECK(received.Chunks.size() > 1);

        // Every node reaches the same lows and highs, to a small share of its size
        for (std::size_t node = 1; node < chunked.Result->NodeVoltages.size(); ++node) {
            const auto [chunked_lowest, chunked_highest] = std::ranges::minmax(chunked.Result->NodeVoltages[node]);
            const auto [lowest, highest] = std::ranges::minmax(one_piece.Result->NodeVoltages[node]);
            const double tolerance = 0.02 * Span(one_piece.Result->NodeVoltages[node]) + 1e-6;
            INFO("node " << node);
            CHECK(std::abs(chunked_lowest - lowest) <= tolerance);
            CHECK(std::abs(chunked_highest - highest) <= tolerance);
        }
        if (std::ranges::contains(THRESHOLD_SENSITIVE_EXAMPLES, std::string_view(example.Title))) {
            continue;
        }

        // A cut shifts the step sequence a little, and that moves the points around a steep edge in time. So a chunked
        // value has to lie, to a small share of the signal, within what the one-piece run does in a short window
        // around the same instant, which spans a few time steps
        const double half_window = std::max(settings.StopTime * 0.005, settings.TimeStep * 10.0);
        for (std::size_t node = 1; node < chunked.Result->NodeVoltages.size(); ++node) {
            const double tolerance = 0.02 * Span(one_piece.Result->NodeVoltages[node]) + 1e-6;
            std::size_t outside = 0;
            std::optional<double> first_time;
            for (std::size_t index = 0; index < chunked.Result->Times.size(); ++index) {
                const double value = chunked.Result->NodeVoltages[node][index];
                const auto [lowest, highest] =
                    RangeAround(one_piece.Result->Times, one_piece.Result->NodeVoltages[node],
                                chunked.Result->Times[index], half_window);
                if (value < lowest - tolerance || value > highest + tolerance) {
                    ++outside;
                    first_time = first_time.value_or(chunked.Result->Times[index]);
                }
            }
            INFO("node " << node << ", first outside at " << first_time.value_or(0.0) << " s");
            CHECK(outside == 0);
        }
    }
}

TEST_CASE("A stop request ends the transient between chunks and keeps the points made so far", "[transient_chunks]") {
    ChargingCapacitor setup;
    Core::RunControl control;
    Received received(&control);
    received.Monitor.OnChunk = [&](Core::Transient chunk) {
        received.Chunks.push_back(std::move(chunk));
        control.Cancel();
    };
    const Core::TransientRun run = Core::RunTransient(setup.Circuit, setup.Settings, std::nullopt, received.Monitor);
    REQUIRE(run.Result.has_value());
    CHECK_FALSE(run.Result->IsComplete);
    CHECK(received.Chunks.size() == 1);
    CHECK(run.Result->Times == received.Chunks.front().Times);
    CHECK(run.Result->Times.back() < setup.Settings.StopTime);

    // The library was cleaned, so the next run starts from nothing
    const Core::TransientRun next = Core::RunTransient(setup.Circuit, setup.Settings);
    REQUIRE(next.Result.has_value());
    CHECK(next.Result->IsComplete);
    CHECK(next.Result->Times.back() == Catch::Approx(setup.Settings.StopTime));
}

TEST_CASE("A monitored transient reports its progress up to the end", "[transient_chunks]") {
    ChargingCapacitor setup;
    Core::RunControl control;
    Received received(&control);
    double last_progress = 0.0;
    bool is_increasing = true;
    received.Monitor.OnChunk = [&](Core::Transient) {
        is_increasing = is_increasing && control.GetProgress() > last_progress;
        last_progress = control.GetProgress();
    };
    const Core::TransientRun run = Core::RunTransient(setup.Circuit, setup.Settings, std::nullopt, received.Monitor);
    REQUIRE(run.Result.has_value());
    CHECK(is_increasing);
    CHECK(control.GetProgress() == 1.0);
}

TEST_CASE("A run with a stepped value can be monitored, and a run without a monitor is complete",
          "[transient_chunks]") {
    ChargingCapacitor setup;
    Received received;
    const Core::TransientRun stepped = Core::RunTransient(
        setup.Circuit, setup.Settings, Core::ParameterValue{.Part = "R1", .Value = 2e3}, received.Monitor);
    REQUIRE(stepped.Result.has_value());
    CHECK(received.Chunks.size() > 1);

    const Core::TransientRun plain = Core::RunTransient(setup.Circuit, setup.Settings);
    REQUIRE(plain.Result.has_value());
    CHECK(plain.Result->IsComplete);
}

TEST_CASE("The lines ngspice prints at each chunk are left out of the messages, so they report no warnings",
          "[transient_chunks]") {
    ChargingCapacitor setup;
    Received received;
    const Core::TransientRun run = Core::RunTransient(setup.Circuit, setup.Settings, std::nullopt, received.Monitor);
    REQUIRE(run.Result.has_value());
    CHECK(std::ranges::none_of(run.Messages,
                               [](const Core::SimulatorMessage &message) { return message.FromErrorStream; }));
    const auto analysis_lines = std::ranges::count_if(
        run.Messages, [](const Core::SimulatorMessage &message) { return message.Text.starts_with("Doing analysis"); });
    CHECK(analysis_lines == 1);
}
