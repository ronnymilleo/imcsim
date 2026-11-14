/**
 * @file    counter_examples_tests.cpp
 * @brief   Reads the count of the counter examples on Nodalys and compares it with ngspice as logic states.
 */

#include "base/logic_state.h"
#include "nodalys_in_use.h"
#include "schematic/examples.h"
#include "schematic/schematic.h"
#include "simulation/simulator.h"
#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

/// The clock of the counter examples: 1 kHz, 5 V, first rising edge at 0.5 ms.
constexpr double CLOCK_PERIOD_SECONDS = 1e-3;
constexpr double FIRST_EDGE_SECONDS = 0.5e-3;
constexpr double LOGIC_LEVEL_VOLTS = 5.0;
/// A quarter of a period after a rising edge, where the clock is high and every output has settled.
constexpr double SETTLED_AFTER_EDGE_SECONDS = 0.25e-3;
/// Before the first rising edge the counter holds its start state.
constexpr double BEFORE_FIRST_EDGE_SECONDS = 0.25e-3;
constexpr std::string_view DECADE_COUNTER = "decade_counter.imcsim";
constexpr std::string_view RIPPLE_COUNTER = "ripple_counter.imcsim";
constexpr std::string_view BLOCKS_COUNTER = "counter_8bit_blocks.imcsim";

/// An example loaded, ready to run on either engine. The circuit refers to the schematic it was built from, which so
/// stays alive beside it.
struct LoadedExample {
    std::unique_ptr<GUI::Schematic> Schematic;
    Core::Circuit Circuit;
    Core::TransientSettings Settings;
};

LoadedExample LoadCounterExample(const std::string_view file_name) {
    const auto example = std::ranges::find(GUI::GetExamples(), file_name, &GUI::Example::FileName);
    REQUIRE(example != GUI::GetExamples().end());
    auto loaded = GUI::LoadExample(*example);
    REQUIRE(loaded.has_value());
    auto schematic = std::make_unique<GUI::Schematic>();
    schematic->Replace(std::move(loaded->Elements), std::move(loaded->Wires), std::move(loaded->Settings),
                       std::move(loaded->Subcircuits), std::move(loaded->Models));
    Core::Circuit circuit = schematic->BuildCircuit();
    const Core::TransientSettings settings = schematic->GetSimulationSettings().Transient;
    return {.Schematic = std::move(schematic), .Circuit = std::move(circuit), .Settings = settings};
}

/// The nodes of the nets Q0, Q1... of a circuit, up to the bit count.
std::vector<std::size_t> FindBitNodes(const Core::Circuit &circuit, const std::size_t bit_count) {
    std::vector<std::size_t> nodes;
    for (std::size_t bit = 0; bit < bit_count; ++bit) {
        const std::optional<int> node = circuit.FindNodeOfNet(std::format("Q{}", bit));
        REQUIRE(node);
        nodes.push_back(static_cast<std::size_t>(*node));
    }
    return nodes;
}

/// The voltage of a node at an instant, taken from the last point at or before it.
double VoltageAt(const Core::Transient &transient, const std::size_t node, const double instant) {
    const auto after = std::ranges::upper_bound(transient.Times, instant);
    REQUIRE(after != transient.Times.begin());
    return transient.NodeVoltages[node][static_cast<std::size_t>(after - transient.Times.begin()) - 1];
}

/// What the counter reads at an instant, a bit at a time, or no value when a bit is neither high nor low.
std::optional<int> ReadCount(const Core::Transient &transient, const std::vector<std::size_t> &bit_nodes,
                             const double instant) {
    int count = 0;
    for (std::size_t bit = 0; bit < bit_nodes.size(); ++bit) {
        const GUI::LogicState state =
            GUI::ReadLogicState(VoltageAt(transient, bit_nodes[bit], instant), LOGIC_LEVEL_VOLTS);
        if (state == GUI::LogicState::UNKNOWN) {
            return std::nullopt;
        }
        if (state == GUI::LogicState::HIGH) {
            count |= 1 << bit;
        }
    }
    return count;
}

/// The count before the first edge, then after each rising edge that fits in the run.
std::vector<std::optional<int>> ReadCountPerEdge(const Core::Transient &transient,
                                                 const std::vector<std::size_t> &bit_nodes,
                                                 const double stop_time_seconds) {
    std::vector<std::optional<int>> counts{ReadCount(transient, bit_nodes, BEFORE_FIRST_EDGE_SECONDS)};
    for (std::size_t edge_index = 0;; ++edge_index) {
        const double instant =
            FIRST_EDGE_SECONDS + static_cast<double>(edge_index) * CLOCK_PERIOD_SECONDS + SETTLED_AFTER_EDGE_SECONDS;
        if (instant >= stop_time_seconds) {
            break;
        }
        counts.push_back(ReadCount(transient, bit_nodes, instant));
    }
    return counts;
}

/// The counts a counter that wraps after the largest one shows from the start: 0, 1, 2...
std::vector<std::optional<int>> ExpectedCounts(const std::size_t edge_count, const int modulus) {
    std::vector<std::optional<int>> counts;
    for (std::size_t edge = 0; edge <= edge_count; ++edge) {
        counts.emplace_back(static_cast<int>(edge) % modulus);
    }
    return counts;
}

/// Runs the saved transient of a counter on Nodalys and reads its count after every edge.
std::vector<std::optional<int>> CountOnNodalys(const std::string_view file_name, const std::size_t bit_count) {
    const LoadedExample example = LoadCounterExample(file_name);
    const TestSupport::NodalysInUse nodalys;
    const Core::TransientRun run = Core::RunTransient(example.Circuit, example.Settings);
    REQUIRE(run.Result.has_value());
    return ReadCountPerEdge(*run.Result, FindBitNodes(example.Circuit, bit_count), example.Settings.StopTime);
}

} // namespace

TEST_CASE("The decade counter counts from 0 to 9 and wraps to 0 on Nodalys", "[counter_examples]") {
    constexpr std::size_t BIT_COUNT = 4;
    constexpr int DECADE = 10;
    constexpr std::size_t EDGES_IN_RUN = 12;
    // The reset that the AND of Q3 and Q1 gives at 10 is what the Nodalys flip-flops used to ignore
    CHECK(CountOnNodalys(DECADE_COUNTER, BIT_COUNT) == ExpectedCounts(EDGES_IN_RUN, DECADE));
}

TEST_CASE("The ripple counter counts from 0 to 7 and wraps to 0 on Nodalys", "[counter_examples]") {
    constexpr std::size_t BIT_COUNT = 3;
    constexpr int MODULUS = 8;
    constexpr std::size_t EDGES_IN_RUN = 9;
    CHECK(CountOnNodalys(RIPPLE_COUNTER, BIT_COUNT) == ExpectedCounts(EDGES_IN_RUN, MODULUS));
}

TEST_CASE("The 8-bit counter counts up by one at every edge on Nodalys", "[counter_examples]") {
    constexpr std::size_t BIT_COUNT = 8;
    constexpr int MODULUS = 256;
    constexpr std::size_t EDGES_IN_RUN = 40;
    CHECK(CountOnNodalys(BLOCKS_COUNTER, BIT_COUNT) == ExpectedCounts(EDGES_IN_RUN, MODULUS));
}

TEST_CASE("The counter examples read the same count on ngspice and on Nodalys after every edge", "[counter_examples]") {
    struct Counter {
        std::string_view FileName;
        std::size_t BitCount;
    };
    constexpr std::array COUNTERS{Counter{DECADE_COUNTER, 4}, Counter{RIPPLE_COUNTER, 3}, Counter{BLOCKS_COUNTER, 8}};
    for (const Counter &counter : COUNTERS) {
        INFO(counter.FileName);
        const LoadedExample example = LoadCounterExample(counter.FileName);
        const std::vector<std::size_t> bit_nodes = FindBitNodes(example.Circuit, counter.BitCount);
        const Core::TransientRun ngspice = Core::RunTransient(example.Circuit, example.Settings);
        REQUIRE(ngspice.Result.has_value());
        const std::vector<std::optional<int>> expected =
            ReadCountPerEdge(*ngspice.Result, bit_nodes, example.Settings.StopTime);
        const std::vector<std::optional<int>> actual = CountOnNodalys(counter.FileName, counter.BitCount);
        CHECK(actual == expected);
        // Every reading is a count, never a bit between the levels
        CHECK(std::ranges::all_of(expected, [](const std::optional<int> &count) { return count.has_value(); }));
    }
}
