/**
 * @file    ready_blocks_tests.cpp
 * @brief   Tests for the logic blocks that come with imcsim, read and run through ngspice.
 */

#include "components/clock.h"
#include "components/logic_input.h"
#include "components/net_label.h"
#include "components/subcircuit.h"
#include "digital_models.h"
#include "document/ready_blocks.h"
#include "document/schematic.h"
#include "editing/element_factory.h"
#include "simulation/circuit_checks.h"
#include "simulation/simulator.h"
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <format>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

/// Where the parts that drive the block go, away from it.
constexpr int DRIVER_ROW = 100;
/// A voltage at or above this reads as a logic 1 of the 5 V blocks.
constexpr double HIGH_VOLTS = 3.5;

const GUI::ReadyBlock &FindBlock(const std::string_view title) {
    for (const GUI::ReadyBlock &block : GUI::GetReadyBlocks()) {
        if (block.Title == title) {
            return block;
        }
    }
    FAIL(std::format("No ready block is titled {}", title));
    return GUI::GetReadyBlocks().front();
}

/**
 * @struct  Bench
 * @brief   A schematic with one ready block in it, and the drivers of its inputs.
 */
struct Bench {
    GUI::Schematic Schematic;
    /// The terminal of each pin of the block, by its name.
    std::map<std::string, GUI::GridPoint> Pins;
    std::size_t DriverCount = 0;
};

void Place(Bench &bench, const std::string_view title) {
    auto group = GUI::MakeReadyBlock(FindBlock(title));
    REQUIRE(group);
    REQUIRE(group->Elements.size() == 1);
    bench.Schematic.MergeSubcircuits(std::move(group->Subcircuits), group->Elements);
    bench.Schematic.AddElement(std::move(group->Elements.front()));
    const GUI::UIElement &block = *bench.Schematic.GetElements().back();
    const auto &instance = static_cast<const Core::SubcircuitInstance &>(block.GetComponent());
    const std::vector<GUI::GridPoint> terminals = block.GetTerminals();
    for (std::size_t pin = 0; pin < terminals.size(); ++pin) {
        bench.Pins[instance.GetPins()[pin]] = terminals[pin];
    }
}

/**
 * @brief   Joins a driver to a pin of the block through a pair of net labels.
 */
void Connect(Bench &bench, std::unique_ptr<GUI::UIElement> driver, const std::string &pin) {
    const std::string net = std::format("DRIVE_{}", pin);
    const GUI::GridPoint driver_point = driver->GetTerminals().front();
    bench.Schematic.AddElement(std::move(driver));
    for (const GUI::GridPoint point : {driver_point, bench.Pins.at(pin)}) {
        bench.Schematic.AddElement(GUI::CreateElement(Core::ComponentType::NET_LABEL, point, GUI::Rotation::R0));
        static_cast<Core::NetLabel &>(bench.Schematic.GetElements().back()->GetComponent()).SetNet(net);
    }
}

void DriveLogic(Bench &bench, const std::string &pin, const bool is_high) {
    auto input = GUI::CreateElement(Core::ComponentType::LOGIC_INPUT,
                                    {static_cast<int>(10 * bench.DriverCount++), DRIVER_ROW}, GUI::Rotation::R0);
    static_cast<Core::LogicInput &>(input->GetComponent()).SetHigh(is_high);
    Connect(bench, std::move(input), pin);
}

void DriveClock(Bench &bench, const std::string &pin) {
    auto clock = GUI::CreateElement(Core::ComponentType::CLOCK,
                                    {static_cast<int>(10 * bench.DriverCount++), DRIVER_ROW}, GUI::Rotation::R0);
    // 1 kHz with the first rising edge at 0.5 ms, so edges land at 0.5, 1.5, 2.5 ms...
    static_cast<Core::Clock &>(clock->GetComponent())
        .SetParameters({.Frequency = 1e3, .DutyCycle = 0.5, .LogicLevel = 5.0, .StartDelay = 0.5e-3});
    Connect(bench, std::move(clock), pin);
}

/**
 * @brief   Drives the pins PREFIX0, PREFIX1... with the bits of a number, the least significant on PREFIX0.
 */
void DriveNumber(Bench &bench, const std::string_view prefix, const int value, const int bit_count) {
    for (int bit = 0; bit < bit_count; ++bit) {
        DriveLogic(bench, std::format("{}{}", prefix, bit), ((value >> bit) & 1) != 0);
    }
}

/**
 * @brief   Reads pins as the bits of a number, the first one the least significant.
 */
int ReadNumber(const std::vector<bool> &levels) {
    int value = 0;
    for (std::size_t bit = 0; bit < levels.size(); ++bit) {
        value |= levels[bit] ? 1 << bit : 0;
    }
    return value;
}

/**
 * @brief   The logic levels of some pins at the operating point.
 */
std::vector<bool> ReadLevels(Bench &bench, const std::vector<std::string> &pins) {
    const Core::Circuit circuit = bench.Schematic.BuildCircuit();
    if (const auto problem = Core::CheckCircuit(circuit)) {
        FAIL(*problem);
    }
    const Core::OperatingPointRun run = Core::RunOperatingPoint(circuit);
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    std::vector<bool> levels;
    for (const std::string &pin : pins) {
        const std::optional<int> node = bench.Schematic.GetConnectivity().GetNode(bench.Pins.at(pin));
        REQUIRE(node);
        levels.push_back(run.Result->NodeVoltages[static_cast<std::size_t>(*node)] >= HIGH_VOLTS);
    }
    return levels;
}

/**
 * @brief   The logic levels of some pins at the end of a transient.
 */
std::vector<bool> ReadFinalLevels(Bench &bench, const std::vector<std::string> &pins, const double stop_time) {
    const Core::Circuit circuit = bench.Schematic.BuildCircuit();
    if (const auto problem = Core::CheckCircuit(circuit)) {
        FAIL(*problem);
    }
    const Core::TransientRun run = Core::RunTransient(circuit, {.StopTime = stop_time, .TimeStep = 10e-6});
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    std::vector<bool> levels;
    for (const std::string &pin : pins) {
        const std::optional<int> node = bench.Schematic.GetConnectivity().GetNode(bench.Pins.at(pin));
        REQUIRE(node);
        levels.push_back(run.Result->NodeVoltages[static_cast<std::size_t>(*node)].back() >= HIGH_VOLTS);
    }
    return levels;
}

} // namespace

TEST_CASE("Every ready block reads as one instance of its definition, with inputs left and outputs right",
          "[ready_blocks]") {
    // The outputs of each block; every other pin is an input
    const std::map<std::string, std::set<std::string>, std::less<>> outputs = {
        {"4-bit counter", {"Q0", "Q1", "Q2", "Q3"}},
        {"4-bit shift register", {"Q0", "Q1", "Q2", "Q3"}},
        {"4-to-1 multiplexer", {"Y"}},
        {"2-to-4 decoder", {"Y0", "Y1", "Y2", "Y3"}},
        {"4-bit adder", {"S0", "S1", "S2", "S3", "COUT"}},
        {"4-bit comparator", {"GT", "EQ", "LT"}},
        {"1-to-4 demultiplexer", {"Y0", "Y1", "Y2", "Y3"}},
        {"BCD to 7-segment decoder", {"a", "b", "c", "d", "e", "f", "g"}},
    };
    REQUIRE(GUI::GetReadyBlocks().size() == outputs.size());
    for (const GUI::ReadyBlock &block : GUI::GetReadyBlocks()) {
        CAPTURE(block.Title);
        const auto group = GUI::MakeReadyBlock(block);
        REQUIRE(group);
        REQUIRE(group->Elements.size() == 1);
        REQUIRE(group->Subcircuits.size() == 1);
        const auto &instance = static_cast<const Core::SubcircuitInstance &>(group->Elements[0]->GetComponent());
        CHECK(instance.GetDefinitionName() == group->Subcircuits[0].Name);
        CHECK(group->Subcircuits[0].Name == block.Name);
        CHECK(instance.GetPins().size() == group->Subcircuits[0].Pins.size());
        const auto block_outputs = outputs.find(block.Title);
        REQUIRE(block_outputs != outputs.end());
        for (const std::string &pin : instance.GetPins()) {
            CAPTURE(pin);
            const bool is_output = block_outputs->second.contains(pin);
            CHECK(instance.GetPinSide(pin) == (is_output ? Core::PinSide::RIGHT : Core::PinSide::LEFT));
        }
    }
}

TEST_CASE("The part search finds the ready blocks by their titles", "[ready_blocks]") {
    CHECK(GUI::FindReadyBlocks("").size() == GUI::GetReadyBlocks().size());
    const std::vector<const GUI::ReadyBlock *> counters = GUI::FindReadyBlocks("Counter");
    REQUIRE(counters.size() == 1);
    CHECK(counters[0]->Title == "4-bit counter");
    CHECK(GUI::FindReadyBlocks("4to1").size() == 1);
    // The definition name counts too, and DEMUX_1TO4 holds "mux" as well
    const std::vector<const GUI::ReadyBlock *> muxes = GUI::FindReadyBlocks("mux");
    REQUIRE(muxes.size() == 2);
    CHECK(muxes[0]->Title == "4-to-1 multiplexer");
    CHECK(muxes[1]->Title == "1-to-4 demultiplexer");
    CHECK(GUI::FindReadyBlocks("7seg").size() == 1);
    CHECK(GUI::FindReadyBlocks("resistor").empty());
}

TEST_CASE("The decoder raises the output its address selects, and none when disabled", "[ready_blocks][digital]") {
    SKIP_WITHOUT_DIGITAL_MODELS();
    for (int address = 0; address < 4; ++address) {
        for (const bool is_enabled : {true, false}) {
            CAPTURE(address, is_enabled);
            Bench bench;
            Place(bench, "2-to-4 decoder");
            DriveLogic(bench, "A0", (address & 1) != 0);
            DriveLogic(bench, "A1", (address & 2) != 0);
            DriveLogic(bench, "EN", is_enabled);
            const std::vector<bool> outputs = ReadLevels(bench, {"Y0", "Y1", "Y2", "Y3"});
            for (int output = 0; output < 4; ++output) {
                CHECK(outputs[static_cast<std::size_t>(output)] == (is_enabled && output == address));
            }
        }
    }
}

TEST_CASE("The multiplexer passes the input its select lines name", "[ready_blocks][digital]") {
    SKIP_WITHOUT_DIGITAL_MODELS();
    // Data inputs 1, 0, 1, 1, so every select gives a different answer from its neighbor
    const std::vector<bool> data = {true, false, true, true};
    for (int select = 0; select < 4; ++select) {
        CAPTURE(select);
        Bench bench;
        Place(bench, "4-to-1 multiplexer");
        for (std::size_t input = 0; input < data.size(); ++input) {
            DriveLogic(bench, std::format("D{}", input), data[input]);
        }
        DriveLogic(bench, "S0", (select & 1) != 0);
        DriveLogic(bench, "S1", (select & 2) != 0);
        CHECK(ReadLevels(bench, {"Y"}) == std::vector<bool>{data[static_cast<std::size_t>(select)]});
    }
}

TEST_CASE("The counter counts the rising edges of its clock in binary", "[ready_blocks][digital]") {
    SKIP_WITHOUT_DIGITAL_MODELS();
    Bench bench;
    Place(bench, "4-bit counter");
    DriveClock(bench, "CLK");
    // Nine rising edges by 8.9 ms: 0.5, 1.5 ... 8.5 ms, so Q3 Q2 Q1 Q0 read 1001
    CHECK(ReadFinalLevels(bench, {"Q0", "Q1", "Q2", "Q3"}, 8.9e-3) == std::vector<bool>{true, false, false, true});
}

TEST_CASE("The shift register moves its input one stage on at each rising edge", "[ready_blocks][digital]") {
    SKIP_WITHOUT_DIGITAL_MODELS();
    Bench bench;
    Place(bench, "4-bit shift register");
    DriveLogic(bench, "IN", true);
    DriveClock(bench, "CLK");
    // Two edges by 2 ms move the 1 into Q0 and then Q1
    CHECK(ReadFinalLevels(bench, {"Q0", "Q1", "Q2", "Q3"}, 2e-3) == std::vector<bool>{true, true, false, false});
}

TEST_CASE("RESET clears the counter and the shift register", "[ready_blocks][digital]") {
    SKIP_WITHOUT_DIGITAL_MODELS();
    for (const std::string_view title : {"4-bit counter", "4-bit shift register"}) {
        CAPTURE(title);
        Bench bench;
        Place(bench, title);
        if (title == "4-bit shift register") {
            DriveLogic(bench, "IN", true);
        }
        DriveLogic(bench, "RESET", true);
        DriveClock(bench, "CLK");
        CHECK(ReadFinalLevels(bench, {"Q0", "Q1", "Q2", "Q3"}, 3e-3) == std::vector<bool>{false, false, false, false});
    }
}

TEST_CASE("The adder adds two numbers and a carry, carrying out past 15", "[ready_blocks][digital]") {
    SKIP_WITHOUT_DIGITAL_MODELS();
    struct Sum {
        int A;
        int B;
        bool HasCarryIn;
    };
    // The last two carry out, and the carry in of 7 + 8 + 1 runs through every stage
    for (const Sum sum : {Sum{5, 9, false}, Sum{3, 4, true}, Sum{15, 1, false}, Sum{7, 8, true}}) {
        CAPTURE(sum.A, sum.B, sum.HasCarryIn);
        Bench bench;
        Place(bench, "4-bit adder");
        DriveNumber(bench, "A", sum.A, 4);
        DriveNumber(bench, "B", sum.B, 4);
        DriveLogic(bench, "CIN", sum.HasCarryIn);
        const int expected = sum.A + sum.B + (sum.HasCarryIn ? 1 : 0);
        CHECK(ReadNumber(ReadLevels(bench, {"S0", "S1", "S2", "S3", "COUT"})) == expected);
    }
}

TEST_CASE("The comparator tells whether A is greater than, equal to or less than B", "[ready_blocks][digital]") {
    SKIP_WITHOUT_DIGITAL_MODELS();
    // 8 against 7 is decided by the top bit alone, 2 against 3 by the bottom one
    for (const auto [a, b] : {std::pair{9, 5}, std::pair{5, 9}, std::pair{6, 6}, std::pair{8, 7}, std::pair{2, 3}}) {
        CAPTURE(a, b);
        Bench bench;
        Place(bench, "4-bit comparator");
        DriveNumber(bench, "A", a, 4);
        DriveNumber(bench, "B", b, 4);
        CHECK(ReadLevels(bench, {"GT", "EQ", "LT"}) == std::vector<bool>{a > b, a == b, a < b});
    }
}

TEST_CASE("The demultiplexer sends its input to the output its select lines name", "[ready_blocks][digital]") {
    SKIP_WITHOUT_DIGITAL_MODELS();
    for (int select = 0; select < 4; ++select) {
        for (const bool is_high : {true, false}) {
            CAPTURE(select, is_high);
            Bench bench;
            Place(bench, "1-to-4 demultiplexer");
            DriveLogic(bench, "D", is_high);
            DriveNumber(bench, "S", select, 2);
            const std::vector<bool> outputs = ReadLevels(bench, {"Y0", "Y1", "Y2", "Y3"});
            for (int output = 0; output < 4; ++output) {
                CHECK(outputs[static_cast<std::size_t>(output)] == (is_high && output == select));
            }
        }
    }
}

TEST_CASE("The BCD decoder lights the segments of each digit and none past 9", "[ready_blocks][digital]") {
    SKIP_WITHOUT_DIGITAL_MODELS();
    // Segments a to g of 0 to 9, with the tails on 6 and 9
    const std::map<int, std::string> lit = {
        {0, "abcdef"}, {1, "bc"},  {2, "abdeg"},   {3, "abcdg"},  {4, "bcfg"}, {5, "acdfg"},
        {6, "acdefg"}, {7, "abc"}, {8, "abcdefg"}, {9, "abcdfg"}, {10, ""},    {15, ""},
    };
    for (const auto &[digit, segments] : lit) {
        CAPTURE(digit);
        Bench bench;
        Place(bench, "BCD to 7-segment decoder");
        DriveNumber(bench, "D", digit, 4);
        const std::vector<bool> levels = ReadLevels(bench, {"a", "b", "c", "d", "e", "f", "g"});
        std::string read;
        for (std::size_t segment = 0; segment < levels.size(); ++segment) {
            if (levels[segment]) {
                read += static_cast<char>('a' + segment);
            }
        }
        CHECK(read == segments);
    }
}
