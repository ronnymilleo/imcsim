/**
 * @file    counter_examples_tests.cpp
 * @brief   Reads the count of the counter examples on Nodalys and compares it with ngspice as logic states.
 */

#include "backends/ngspice/ngspice_library.h"
#include "base/logic_state.h"
#include "nodalys_in_use.h"
#include "schematic/examples.h"
#include "schematic/schematic.h"
#include "simulation/simulator.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <format>
#include <memory>
#include <optional>
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
    schematic->Replace(std::move(loaded->Elements),
                       std::move(loaded->Wires),
                       std::move(loaded->Settings),
                       std::move(loaded->Subcircuits),
                       std::move(loaded->Models));
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
std::optional<int>
ReadCount(const Core::Transient &transient, const std::vector<std::size_t> &bit_nodes, const double instant) {
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

/**
 * @brief   Reads a counter after every edge on each engine and checks the sequence on Nodalys and that ngspice agrees.
 * @param[in] file_name    File of the example.
 * @param[in] bit_count    Outputs Q0, Q1... of the counter.
 * @param[in] modulus      Count after which it wraps to 0.
 * @param[in] edge_count   Rising edges of the clock to run, which sets the stop time.
 */
void CheckCounter(const std::string_view file_name,
                  const std::size_t bit_count,
                  const int modulus,
                  const std::size_t edge_count) {
    const LoadedExample example = LoadCounterExample(file_name);
    // Long enough for the last edge to settle, and short enough to keep the 8-bit counter quick on Nodalys
    Core::TransientSettings settings = example.Settings;
    settings.StopTime = FIRST_EDGE_SECONDS + static_cast<double>(edge_count) * CLOCK_PERIOD_SECONDS;
    const std::vector<std::size_t> bit_nodes = FindBitNodes(example.Circuit, bit_count);

    // An ngspice without the digital models (digital.cm) cannot run a counter, so only Nodalys is read there
    std::optional<std::vector<std::optional<int>>> on_ngspice;
    if (Core::AreDigitalModelsAvailable()) {
        const Core::TransientRun ngspice = Core::RunTransient(example.Circuit, settings);
        REQUIRE(ngspice.Result.has_value());
        on_ngspice = ReadCountPerEdge(*ngspice.Result, bit_nodes, settings.StopTime);
    }
    const TestSupport::NodalysInUse nodalys_in_use;
    const Core::TransientRun nodalys = Core::RunTransient(example.Circuit, settings);
    REQUIRE(nodalys.Result.has_value());

    const std::vector<std::optional<int>> on_nodalys = ReadCountPerEdge(*nodalys.Result, bit_nodes, settings.StopTime);
    CHECK(on_nodalys == ExpectedCounts(edge_count, modulus));
    if (on_ngspice) {
        // Logic states, not voltages: the engines draw the levels and the edges differently
        CHECK(on_nodalys == *on_ngspice);
    }
}

} // namespace

TEST_CASE("The decade counter counts from 0 to 9 and wraps to 0, on Nodalys as on ngspice", "[counter_examples]") {
    constexpr std::size_t BIT_COUNT = 4;
    constexpr int DECADE = 10;
    constexpr std::size_t EDGES = 12;
    // The reset that the AND of Q3 and Q1 gives at 10 is what the Nodalys flip-flops used to ignore
    CheckCounter(DECADE_COUNTER, BIT_COUNT, DECADE, EDGES);
}

TEST_CASE("The ripple counter counts from 0 to 7 and wraps to 0, on Nodalys as on ngspice", "[counter_examples]") {
    constexpr std::size_t BIT_COUNT = 3;
    constexpr int MODULUS = 8;
    constexpr std::size_t EDGES = 9;
    CheckCounter(RIPPLE_COUNTER, BIT_COUNT, MODULUS, EDGES);
}

TEST_CASE("The 8-bit counter counts up by one at every edge past the first nibble, on Nodalys as on ngspice",
          "[counter_examples]") {
    constexpr std::size_t BIT_COUNT = 8;
    constexpr int MODULUS = 256;
    // Seventeen edges carry from the low nibble into the high one
    constexpr std::size_t EDGES = 17;
    CheckCounter(BLOCKS_COUNTER, BIT_COUNT, MODULUS, EDGES);
}
