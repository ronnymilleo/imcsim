/**
 * @file    fuse_tests.cpp
 * @brief   Tests for the fuse: its figures, its netlist, the ngspice runs that blow it and how the results and the
 *          schematic tell that it blew.
 */

#include "analysis/fuse_readings.h"
#include "circuit/circuit.h"
#include "circuit/components/fuse.h"
#include "circuit/components/ground.h"
#include "circuit/components/inductor.h"
#include "circuit/components/resistor.h"
#include "circuit/components/vcc.h"
#include "circuit/components/voltage_source.h"
#include "circuit/subcircuit.h"
#include "document/schematic_file.h"
#include "drawing/fuse_flash.h"
#include "editing/connectivity.h"
#include "editing/element_factory.h"
#include "preferences.h"
#include "simulation/fuse_state.h"
#include "simulation/ratings.h"
#include "simulation/simulation_backend.h"
#include "simulation/simulator.h"
#include "spice/spice_value.h"
#include "test_printers.h"
#include "ui_elements/ui_fuse.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace {

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

/// A 1 A fuse with a time constant of 150 ms, shorter than the ready ones, so the runs that blow it stay short.
constexpr Core::FuseParameters QUICK_FUSE = {
    .RatedCurrent = 1.0, .MeltingI2t = 0.15, .ColdResistance = 0.15, .BlownResistance = 1e9};

/// A 5 V supply on node 1 feeding a quick fuse to node 2, where a load goes to ground.
struct FuseLoop {
    Core::VCC Supply;
    Core::Fuse Part;
    Core::Resistor Load;
    Core::Ground Reference;
    Core::Circuit Circuit;

    explicit FuseLoop(const double load_resistance) {
        Supply.SetName("V1");
        Supply.SetValue(5.0);
        Part.SetName("FU1");
        Part.SetParameters(QUICK_FUSE);
        Load.SetName("R1");
        Load.SetValue(load_resistance);
        Circuit.Add(Supply, {1});
        Circuit.Add(Part, {1, 2});
        Circuit.Add(Load, {2, 0});
        Circuit.Add(Reference, {0});
    }
};

/// A pulse train on node 1 feeding the fuse to a load of 2.5 Ohm: 12.5 V is 5 A, a short overload of the quick fuse.
struct PulsedFuseLoop {
    Core::VoltageSource Source;
    Core::Fuse Part;
    Core::Resistor Load;
    Core::Ground Reference;
    Core::Circuit Circuit;

    PulsedFuseLoop(const double period, const double width) {
        Source.SetName("Vin1");
        Source.SetSourceType(Core::VoltageSource::SourceType::PULSE);
        Source.SetPulse({.Low = 0.0,
                         .High = 12.5,
                         .Delay = 1e-3,
                         .RiseTime = 10e-6,
                         .FallTime = 10e-6,
                         .Width = width,
                         .Period = period});
        Part.SetName("FU1");
        Part.SetParameters(QUICK_FUSE);
        Load.SetName("R1");
        Load.SetValue(2.5);
        Circuit.Add(Source, {1, 0});
        Circuit.Add(Part, {1, 2});
        Circuit.Add(Load, {2, 0});
        Circuit.Add(Reference, {0});
    }
};

/// ngspice falls back to slower ways of finding an operating point when the first one fails, and says so
bool HasConvergenceTrouble(const std::vector<Core::SimulatorMessage> &messages) {
    return std::ranges::any_of(messages, [](const Core::SimulatorMessage &message) {
        return message.Text.contains("failed") || message.Text.contains("singular") ||
               message.Text.contains("Transient op");
    });
}

bool HasBlewWarning(const Core::Transient &transient) {
    return std::ranges::any_of(transient.Warnings,
                               [](const std::string &warning) { return warning.starts_with("FU1 blew at "); });
}

/// Heat at which the latch of the model starts to charge, a margin above the 1 that the rated current settles at.
constexpr double LATCH_HEAT = 1.015;

/// The time the default fuse takes to blow at a current: the heat settles at the current squared over the rated one
/// squared, with the cooling time constant, and the latch fires when it reaches LATCH_HEAT.
double BlowTime(const double current, const Core::FuseParameters &parameters = QUICK_FUSE) {
    const double ratio = LATCH_HEAT * parameters.RatedCurrent * parameters.RatedCurrent / (current * current);
    return -Core::GetCoolingTimeConstant(parameters) * std::log(1.0 - ratio);
}

/// Four samples of a fuse on nodes 1 and 2 that carries 2 A at 0 s and 0.1 s, blows at 0.2 s and then sees 5 V.
Core::Transient BlowingFuseResult() {
    Core::Transient transient;
    transient.Times = {0.0, 0.1, 0.2, 0.3};
    transient.NodeVoltages = {{0.0, 0.0, 0.0, 0.0}, {5.0, 5.0, 5.0, 5.0}, {4.7, 4.7, 0.0, 0.0}};
    transient.Currents = {{"FU1", {2.0, 2.0, 5e-9, 5e-9}}};
    return transient;
}

} // namespace

TEST_CASE("A fuse starts as a 1 A fast fuse, and its cooling time constant follows from its two ratings", "[fuse]") {
    const Core::Fuse fuse;
    CHECK(fuse.GetType() == Core::ComponentType::FUSE);
    CHECK(std::string(fuse.GetNamePrefix()) == "FU");
    CHECK_FALSE(fuse.HasValue());
    CHECK(Core::ParseComponentType("Fuse") == Core::ComponentType::FUSE);
    CHECK(fuse.GetParameters() == Core::FuseParameters{});
    // I2t = Ih^2 tau
    CHECK_THAT(Core::GetCoolingTimeConstant({.RatedCurrent = 2.0, .MeltingI2t = 1.0}), WithinAbs(0.25, 1e-12));
    CHECK(Core::Fuse::IsValidParameters({}));
    CHECK_FALSE(Core::Fuse::IsValidParameters({.RatedCurrent = 0.0}));
    CHECK_FALSE(Core::Fuse::IsValidParameters({.MeltingI2t = -1.0}));
    CHECK_FALSE(Core::Fuse::IsValidParameters({.ColdResistance = 0.0}));
    CHECK_FALSE(Core::Fuse::IsValidParameters({.ColdResistance = 1.0, .BlownResistance = 0.5}));
}

TEST_CASE("The ready fuses are valid and differ in name and figures", "[fuse]") {
    const std::span<const Core::ReadyFuse> ready_fuses = Core::GetReadyFuses();
    REQUIRE(ready_fuses.size() == 5);
    for (std::size_t index = 0; index < ready_fuses.size(); ++index) {
        INFO(ready_fuses[index].Name);
        CHECK(Core::Fuse::IsValidParameters(ready_fuses[index].Parameters));
        for (std::size_t other = index + 1; other < ready_fuses.size(); ++other) {
            CHECK(std::string(ready_fuses[index].Name) != ready_fuses[other].Name);
            CHECK_FALSE(ready_fuses[index].Parameters == ready_fuses[other].Parameters);
        }
    }
    // A slow-blow fuse takes more I2t than a fast one of the same rating, so it cools more slowly
    const auto fast = std::ranges::find(ready_fuses, std::string("1 A fast"),
                                        [](const Core::ReadyFuse &ready) { return std::string(ready.Name); });
    const auto slow = std::ranges::find(ready_fuses, std::string("1 A slow-blow"),
                                        [](const Core::ReadyFuse &ready) { return std::string(ready.Name); });
    REQUIRE(fast != ready_fuses.end());
    REQUIRE(slow != ready_fuses.end());
    CHECK(Core::GetCoolingTimeConstant(slow->Parameters) > Core::GetCoolingTimeConstant(fast->Parameters));
}

TEST_CASE("A clone of a fuse keeps its figures and shares nothing with it", "[fuse]") {
    Core::Fuse fuse;
    fuse.SetName("FU3");
    fuse.SetParameters({.RatedCurrent = 5.0, .MeltingI2t = 3.0, .ColdResistance = 0.02, .BlownResistance = 1e8});
    const std::unique_ptr<Core::Component> copy = fuse.Clone();
    fuse.SetParameters({});
    REQUIRE(copy->GetType() == Core::ComponentType::FUSE);
    CHECK(copy->GetName() == "FU3");
    CHECK(static_cast<const Core::Fuse &>(*copy).GetParameters().RatedCurrent == 5.0);
}

TEST_CASE("A fuse writes an instance of its subcircuit and the Gear integration, once however many there are",
          "[fuse]") {
    FuseLoop loop(2.5);
    Core::Fuse second;
    second.SetName("FU2");
    second.SetParameters({.RatedCurrent = 0.5, .MeltingI2t = 0.0375, .ColdResistance = 0.5, .BlownResistance = 1e9});
    loop.Circuit.Add(second, {2, 3});

    const std::string netlist = loop.Circuit.ToSpiceNetlist();
    CHECK(netlist.contains("X-FU1 1 2 imcsim_fuse Ih=1 Tau=150m Rc=150m Rb=1G\n"));
    // Tau is the I2t over the rated current squared: 37.5 mA s / 0.25 A^2
    CHECK(netlist.contains("X-FU2 2 3 imcsim_fuse Ih=500m Tau=150m Rc=500m Rb=1G\n"));
    CHECK(netlist.find(".subckt imcsim_fuse") == netlist.rfind(".subckt imcsim_fuse"));
    CHECK(netlist.contains(".ends imcsim_fuse\n"));
    CHECK(netlist.find(".options method=gear\n") == netlist.rfind(".options method=gear\n"));
    CHECK(netlist.contains(".options method=gear\n"));
}

TEST_CASE("A circuit without a fuse keeps the default integration", "[fuse]") {
    Core::VCC supply;
    supply.SetName("V1");
    Core::Resistor load;
    load.SetName("R1");
    Core::Circuit circuit;
    circuit.Add(supply, {1});
    circuit.Add(load, {1, 0});
    CHECK_FALSE(circuit.HasFuse());
    CHECK_FALSE(circuit.ToSpiceNetlist().contains(".options"));
}

TEST_CASE("A fuse inside a block asks for the Gear integration of the whole netlist, once", "[fuse]") {
    Core::Fuse inside;
    inside.SetName("FU1");
    auto definition = std::make_shared<Core::SubcircuitDefinition>();
    definition->Name = "protected";
    definition->Pins = {{"IN", 1}, {"OUT", 2}};
    definition->Body.Add(inside, {1, 2});
    Core::SubcircuitInstance instance("protected", {"IN", "OUT"});
    instance.SetName("X1");
    Core::VCC supply;
    supply.SetName("V1");
    Core::Resistor load;
    load.SetName("R1");
    Core::Circuit circuit;
    circuit.Add(supply, {1});
    circuit.Add(instance, {1, 2});
    circuit.Add(load, {2, 0});
    circuit.AddSubcircuit(definition);

    CHECK(circuit.HasFuse());
    const std::string netlist = circuit.ToSpiceNetlist();
    CHECK(netlist.contains(".options method=gear\n"));
    CHECK(netlist.find(".options method=gear\n") == netlist.rfind(".options method=gear\n"));
    // The option is global, so it is not written inside the .subckt of the block
    const std::size_t option = netlist.find(".options method=gear\n");
    const std::size_t block_start = netlist.find(".subckt protected");
    const std::size_t block_end = netlist.find(".ends protected\n");
    REQUIRE(block_start != std::string::npos);
    CHECK((option < block_start || option > block_end));
}

TEST_CASE("A fuse blows when the heat of its current reaches the melting point, at the time the figures give",
          "[fuse][simulator]") {
    // 5 V over 2.5 Ohm and the cold resistance is 1.96 A, nearly twice the rating
    FuseLoop loop(2.5);
    const Core::TransientRun run = Core::RunTransient(loop.Circuit, {.StopTime = 0.5, .TimeStep = 1e-3});
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    const Core::Transient &transient = *run.Result;
    const double current = 5.0 / (2.5 + 0.15);
    // The fuse opens: the load loses its voltage in the same run and stays without it
    CHECK_THAT(transient.NodeVoltages[2].front(), WithinAbs(5.0 * 2.5 / 2.65, 1e-3));
    CHECK_THAT(transient.NodeVoltages[2].back(), WithinAbs(0.0, 1e-3));
    const std::optional<double> blow_time =
        Core::FindFuseBlowTime(*loop.Circuit.GetEntries()[1].Part, loop.Circuit.GetEntries()[1].Nodes, transient);
    REQUIRE(blow_time.has_value());
    INFO("blew at " << *blow_time << " s, against " << BlowTime(current) << " s");
    CHECK_THAT(*blow_time, WithinRel(BlowTime(current), 0.05));
    CHECK(HasBlewWarning(transient));
    CHECK(std::ranges::none_of(transient.Warnings,
                               [](const std::string &warning) { return warning.contains("exceeds its"); }));
}

TEST_CASE("A fuse never blows below its rated current, however long it carries it", "[fuse][simulator]") {
    // 0.97 A against 1 A: the heat settles at 0.94 of the melting point
    FuseLoop loop(5.0);
    const Core::TransientRun run = Core::RunTransient(loop.Circuit, {.StopTime = 2.0, .TimeStep = 5e-3});
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    CHECK_FALSE(HasBlewWarning(*run.Result));
    CHECK_THAT(run.Result->NodeVoltages[2].back(), WithinAbs(5.0 * 5.0 / 5.15, 1e-3));
}

TEST_CASE("A fuse at exactly its rated current never blows, over 20 time constants", "[fuse][simulator]") {
    // 5 V over 4.85 Ohm and the 0.15 Ohm cold resistance is 1 A, where the heat settles at the melting point
    FuseLoop loop(4.85);
    const double time_constant = Core::GetCoolingTimeConstant({});
    const Core::TransientRun run =
        Core::RunTransient(loop.Circuit, {.StopTime = 20.0 * time_constant, .TimeStep = 5e-3});
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    CHECK_FALSE(HasBlewWarning(*run.Result));
    CHECK_THAT(run.Result->NodeVoltages[2].back(), WithinAbs(4.85, 1e-3));
}

TEST_CASE("A fuse blows only once the heat of its current passes the latch, about 0.75% above its rated current",
          "[fuse][simulator]") {
    for (const double ratio : {0.999, 1.001, 1.03}) {
        // The current is the ratio times the rated 1 A, from a DC source
        FuseLoop loop(5.0 / ratio - 0.15);
        const double time_constant = Core::GetCoolingTimeConstant({});
        const Core::TransientRun run =
            Core::RunTransient(loop.Circuit, {.StopTime = 20.0 * time_constant, .TimeStep = 5e-3});
        if (!run.Result) {
            FAIL(run.Result.error());
        }
        INFO("current ratio " << ratio);
        // The heat settles at ratio^2, which is past the latch only from about 1.0075
        CHECK(HasBlewWarning(*run.Result) == (ratio > 1.0075));
    }
}

TEST_CASE("A fuse blows just above its rated current, after a long time", "[fuse][simulator]") {
    // 1.03 A: the heat settles at 1.06, and it melts after the time the first-order model gives
    FuseLoop loop(4.7);
    const double current = 5.0 / (4.7 + 0.15);
    const Core::TransientRun run = Core::RunTransient(loop.Circuit, {.StopTime = 2.0, .TimeStep = 5e-3});
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    const std::optional<double> blow_time =
        Core::FindFuseBlowTime(*loop.Circuit.GetEntries()[1].Part, loop.Circuit.GetEntries()[1].Nodes, *run.Result);
    REQUIRE(blow_time.has_value());
    CHECK_THAT(*blow_time, WithinRel(BlowTime(current), 0.1));
}

TEST_CASE("An operating point, a DC sweep and an AC sweep see a whole fuse, even far past its rating",
          "[fuse][simulator]") {
    // 5 V over 0.5 Ohm is 7.7 A, several times what the fuse carries for long
    FuseLoop loop(0.5);
    const Core::OperatingPointRun point = Core::RunOperatingPoint(loop.Circuit);
    if (!point.Result) {
        FAIL(point.Result.error());
    }
    CHECK_THAT(point.Result->NodeVoltages[2], WithinAbs(5.0 * 0.5 / 0.65, 1e-3));
    CHECK(point.Result->Warnings.empty());
    CHECK_FALSE(HasConvergenceTrouble(point.Messages));

    const Core::DCSweepRun sweep =
        Core::RunDCSweep(loop.Circuit, {.Swept = {.Source = "V1", .Start = 0.0, .Stop = 20.0, .Step = 5.0}});
    if (!sweep.Result) {
        FAIL(sweep.Result.error());
    }
    CHECK_THAT(sweep.Result->Curves[0].NodeVoltages[2].back(), WithinAbs(20.0 * 0.5 / 0.65, 1e-3));
    CHECK_FALSE(HasConvergenceTrouble(sweep.Messages));

    Core::VoltageSource source;
    source.SetName("Vin1");
    source.SetSourceType(Core::VoltageSource::SourceType::AC);
    Core::Circuit circuit;
    circuit.Add(source, {1, 0});
    circuit.Add(loop.Part, {1, 2});
    circuit.Add(loop.Load, {2, 0});
    circuit.Add(loop.Reference, {0});
    const Core::ACSweepRun response =
        Core::RunACSweep(circuit, {.StartFrequency = 10.0, .StopFrequency = 1e3, .PointsPerDecade = 2});
    if (!response.Result) {
        FAIL(response.Result.error());
    }
    for (const double magnitude : response.Result->NodeMagnitudesDecibels[2]) {
        CHECK_THAT(magnitude, WithinAbs(20.0 * std::log10(0.5 / 0.65), 1e-3));
    }
}

TEST_CASE("A noise analysis and a transfer function run with a whole fuse in the circuit", "[fuse][simulator]") {
    FuseLoop loop(2.5);
    Core::VoltageSource source;
    source.SetName("Vin1");
    source.SetSourceType(Core::VoltageSource::SourceType::AC);
    Core::Circuit circuit;
    circuit.Add(source, {1, 0});
    circuit.Add(loop.Part, {1, 2});
    circuit.Add(loop.Load, {2, 0});
    circuit.Add(loop.Reference, {0});
    const Core::NoiseRun noise = Core::RunNoise(
        circuit,
        {.OutputNode = 2, .InputSource = "Vin1", .StartFrequency = 10.0, .StopFrequency = 100.0, .PointsPerDecade = 1});
    if (!noise.Result) {
        FAIL(noise.Result.error());
    }
    CHECK(noise.Result->OutputTotal > 0.0);
    const Core::TransferFunctionRun transfer =
        Core::RunTransferFunction(circuit, {.OutputNode = 2, .InputSource = "Vin1"});
    if (!transfer.Result) {
        FAIL(transfer.Result.error());
    }
    CHECK_THAT(transfer.Result->Gain, WithinAbs(2.5 / 2.65, 1e-6));
}

TEST_CASE("Overloads far apart in time do not add up, and overloads close together do", "[fuse][simulator]") {
    // Each 5 A pulse of 1 ms adds a sixth of the melting heat, which the fuse loses again in a second
    PulsedFuseLoop apart(1.0, 1e-3);
    const Core::TransientRun spaced = Core::RunTransient(apart.Circuit, {.StopTime = 5.0, .TimeStep = 200e-6});
    if (!spaced.Result) {
        FAIL(spaced.Result.error());
    }
    CHECK_FALSE(HasBlewWarning(*spaced.Result));
    CHECK(spaced.Result->NodeVoltages[2].back() > -1e-6);

    PulsedFuseLoop together(10e-3, 1e-3);
    const Core::TransientRun close = Core::RunTransient(together.Circuit, {.StopTime = 0.5, .TimeStep = 200e-6});
    if (!close.Result) {
        FAIL(close.Result.error());
    }
    CHECK(HasBlewWarning(*close.Result));
}

TEST_CASE("A transient run in chunks keeps the fuse blown across the cuts", "[fuse][simulator]") {
    FuseLoop loop(2.5);
    std::vector<Core::Transient> chunks;
    Core::RunMonitor monitor;
    monitor.OnChunk = [&chunks](Core::Transient chunk) { chunks.push_back(std::move(chunk)); };
    const Core::TransientRun run =
        Core::RunTransient(loop.Circuit, {.StopTime = 0.5, .TimeStep = 1e-3}, std::nullopt, monitor);
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    // The blow comes in a chunk, and every point after it stays open
    CHECK(chunks.size() > 1);
    CHECK(HasBlewWarning(*run.Result));
    const std::vector<double> &load = run.Result->NodeVoltages[2];
    const auto first_open = std::ranges::find_if(load, [](const double voltage) { return voltage < 0.1; });
    REQUIRE(first_open != load.end());
    CHECK(std::ranges::all_of(first_open, load.end(), [](const double voltage) { return voltage < 0.1; }));
}

TEST_CASE("A fuse cuts the current of an inductive load without ringing for ever", "[fuse][simulator]") {
    // 5 V over 2.5 Ohm in series with 10 mH: the fuse opens with the inductor carrying 2 A
    FuseLoop loop(2.5);
    Core::Inductor inductor;
    inductor.SetName("L1");
    inductor.SetValue(10e-3);
    Core::Circuit circuit;
    circuit.Add(loop.Supply, {1});
    circuit.Add(loop.Part, {1, 2});
    circuit.Add(inductor, {2, 3});
    circuit.Add(loop.Load, {3, 0});
    circuit.Add(loop.Reference, {0});
    const Core::TransientRun run = Core::RunTransient(circuit, {.StopTime = 0.5, .TimeStep = 1e-3});
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    REQUIRE(HasBlewWarning(*run.Result));
    // With the Gear integration the node beside the fuse settles at once; the trapezoidal rule swings it by tens of
    // volts for ever
    const std::vector<double> &node = run.Result->NodeVoltages[2];
    CHECK_THAT(node.back(), WithinAbs(0.0, 1e-3));
    const std::size_t last_quarter = node.size() * 3 / 4;
    const double swing =
        std::ranges::max(std::span(node).subspan(last_quarter), {}, [](const double v) { return std::abs(v); });
    CHECK_THAT(swing, WithinAbs(0.0, 1e-2));
}

TEST_CASE("The blow time is the first point where the resistance of the fuse is open", "[fuse]") {
    FuseLoop loop(2.5);
    const Core::CircuitEntry &entry = loop.Circuit.GetEntries()[1];
    const Core::Transient transient = BlowingFuseResult();
    CHECK(Core::FindFuseBlowTime(*entry.Part, entry.Nodes, transient) == 0.2);

    SECTION("a fuse that conducts to the end did not blow") {
        Core::Transient whole = BlowingFuseResult();
        whole.NodeVoltages[2] = {4.7, 4.7, 4.7, 4.7};
        whole.Currents[0].Values = {2.0, 2.0, 2.0, 2.0};
        CHECK_FALSE(Core::FindFuseBlowTime(*entry.Part, entry.Nodes, whole).has_value());
    }
    SECTION("no voltage across the fuse says nothing, and the fuse is not taken for blown") {
        Core::Transient idle = BlowingFuseResult();
        idle.NodeVoltages[2] = {5.0, 5.0, 5.0, 5.0};
        idle.Currents[0].Values = {0.0, 0.0, 0.0, 0.0};
        CHECK_FALSE(Core::FindFuseBlowTime(*entry.Part, entry.Nodes, idle).has_value());
    }
    SECTION("a part that is not a fuse never blows") {
        const Core::CircuitEntry &resistor = loop.Circuit.GetEntries()[2];
        CHECK_FALSE(Core::FindFuseBlowTime(*resistor.Part, resistor.Nodes, transient).has_value());
    }
    SECTION("a result without the current of the fuse says nothing") {
        Core::Transient missing = BlowingFuseResult();
        missing.Currents.clear();
        CHECK_FALSE(Core::FindFuseBlowTime(*entry.Part, entry.Nodes, missing).has_value());
    }
}

TEST_CASE("A transient that blew a fuse warns with the time, and an operating point never does", "[fuse]") {
    FuseLoop loop(2.5);
    Core::Transient transient = BlowingFuseResult();
    transient.NodeVoltages.push_back({0.0, 0.0, 0.0, 0.0});
    Core::AddRatingWarnings(loop.Circuit, transient);
    REQUIRE(transient.Warnings.size() == 1);
    CHECK(transient.Warnings[0] == "FU1 blew at 200ms");
}

TEST_CASE("The schematic finds the blow time of each fuse and tells whether it is open at an instant", "[fuse]") {
    std::vector<std::unique_ptr<GUI::UIElement>> elements;
    elements.push_back(GUI::CreateElement(Core::ComponentType::VCC, {0, 0}, GUI::Rotation::R0));
    elements.push_back(GUI::CreateElement(Core::ComponentType::FUSE, {6, 0}, GUI::Rotation::R0));
    elements.push_back(GUI::CreateElement(Core::ComponentType::RESISTOR, {10, 0}, GUI::Rotation::R0));
    elements.push_back(GUI::CreateElement(Core::ComponentType::GROUND, {12, 4}, GUI::Rotation::R0));
    elements[0]->GetComponent().SetName("V1");
    elements[1]->GetComponent().SetName("FU1");
    elements[2]->GetComponent().SetName("R1");
    // The supply joins the left of the fuse, and the right of the fuse is the top of the resistor
    const std::vector<GUI::UIWire> wires = {{{0, 0}, {4, 0}}, {{12, 0}, {12, 4}}};
    const GUI::Connectivity connectivity(elements, wires);
    const std::optional<int> supply_node = connectivity.GetNode({4, 0});
    const std::optional<int> load_node = connectivity.GetNode({8, 0});
    REQUIRE(supply_node.has_value());
    REQUIRE(load_node.has_value());

    Core::Transient transient = BlowingFuseResult();
    transient.NodeVoltages.assign(3, std::vector<double>(4, 0.0));
    transient.NodeVoltages[static_cast<std::size_t>(*supply_node)] = {5.0, 5.0, 5.0, 5.0};
    transient.NodeVoltages[static_cast<std::size_t>(*load_node)] = {4.7, 4.7, 0.0, 0.0};
    const std::vector<std::optional<double>> blow_times = GUI::FindFuseBlowTimes(elements, connectivity, transient);
    REQUIRE(blow_times.size() == 4);
    CHECK_FALSE(blow_times[0].has_value());
    CHECK(blow_times[1] == 0.2);
    CHECK_FALSE(blow_times[2].has_value());
    CHECK_FALSE(blow_times[3].has_value());
}

TEST_CASE("A fuse has two terminals like a resistor, and its display state does not change what it is", "[fuse]") {
    GUI::UIFuse fuse({5, 5}, GUI::Rotation::R0);
    CHECK(fuse.GetComponent().GetType() == Core::ComponentType::FUSE);
    CHECK(fuse.GetTerminals() == std::vector<GUI::GridPoint>{{3, 5}, {7, 5}});
    CHECK(fuse.Contains({5.0f, 5.0f}));
    fuse.SetDisplayState(true, 1.0f);
    CHECK(fuse.GetTerminals() == std::vector<GUI::GridPoint>{{3, 5}, {7, 5}});
    CHECK(static_cast<const Core::Fuse &>(fuse.GetComponent()).GetParameters() == Core::FuseParameters{});
}

TEST_CASE("The flash of a blown fuse starts at full strength and fades out in 150 ms", "[fuse]") {
    CHECK(GUI::GetFuseFlashLevel(-0.01) == 0.0f);
    CHECK(GUI::GetFuseFlashLevel(0.0) == 1.0f);
    CHECK_THAT(GUI::GetFuseFlashLevel(GUI::FUSE_FLASH_SECONDS / 2.0), WithinAbs(0.5, 1e-6));
    CHECK(GUI::GetFuseFlashLevel(GUI::FUSE_FLASH_SECONDS) == 0.0f);
    CHECK(GUI::GetFuseFlashLevel(10.0) == 0.0f);
}

TEST_CASE("A fuse is open at an instant once its blow time has passed, and an operating point always sees it whole",
          "[fuse]") {
    CHECK(GUI::IsFuseBlownAt(0.2, 0.2));
    CHECK(GUI::IsFuseBlownAt(0.2, 0.5));
    CHECK_FALSE(GUI::IsFuseBlownAt(0.2, 0.1));
    CHECK_FALSE(GUI::IsFuseBlownAt(std::nullopt, 0.5));
    CHECK_FALSE(GUI::IsFuseBlownAt(0.2, std::nullopt));
}

TEST_CASE("A fuse keeps its figures in the file, and one with figures out of range is skipped", "[fuse]") {
    std::vector<std::unique_ptr<GUI::UIElement>> elements;
    elements.push_back(GUI::CreateElement(Core::ComponentType::FUSE, {4, 4}, GUI::Rotation::R90));
    auto &fuse = static_cast<Core::Fuse &>(elements.back()->GetComponent());
    fuse.SetName("FU1");
    fuse.SetParameters({.RatedCurrent = 5.0, .MeltingI2t = 3.75, .ColdResistance = 0.02, .BlownResistance = 1e8});

    const auto loaded = GUI::LoadSchematic(GUI::SaveSchematic(elements, {}, {}, {}));
    REQUIRE(loaded.has_value());
    CHECK(loaded->Warnings.empty());
    REQUIRE(loaded->Elements.size() == 1);
    const auto &copy = static_cast<const Core::Fuse &>(loaded->Elements[0]->GetComponent());
    CHECK(copy.GetName() == "FU1");
    CHECK(copy.GetParameters() == fuse.GetParameters());

    const std::string out_of_range =
        R"({"format": "imcsim-schematic", "version": 1, "elements": [{"type": "Fuse", "x": 1, "y": 1, "rotation": 0, )"
        R"("name": "FU1", "parameters": {"rated_current": 0.0}}], "wires": []})";
    const auto skipped = GUI::LoadSchematic(out_of_range);
    REQUIRE(skipped.has_value());
    CHECK(skipped->Elements.empty());
    CHECK(skipped->Warnings.size() == 1);

    const std::string plain =
        R"({"format": "imcsim-schematic", "version": 1, "elements": [{"type": "Fuse", "x": 1, "y": 1, "rotation": 0, )"
        R"("name": "FU1"}], "wires": []})";
    const auto defaults = GUI::LoadSchematic(plain);
    REQUIRE(defaults.has_value());
    REQUIRE(defaults->Elements.size() == 1);
    CHECK(static_cast<const Core::Fuse &>(defaults->Elements[0]->GetComponent()).GetParameters() ==
          Core::FuseParameters{});
}

TEST_CASE("The fuse flash is on for a new user, and the other effects are not", "[fuse][preferences]") {
    const GUI::Preferences preferences;
    CHECK(preferences.IsEffectOn(GUI::LiveEffect::FUSE));
    CHECK_FALSE(preferences.IsEffectOn(GUI::LiveEffect::LED_GLOW));
    CHECK(std::string(GUI::GetLiveEffectName(GUI::LiveEffect::FUSE)) == "Fuse");
    GUI::Preferences off;
    off.SetEffectOn(GUI::LiveEffect::FUSE, false);
    CHECK_FALSE(GUI::LoadPreferences(GUI::SavePreferences(off)).Choices.IsEffectOn(GUI::LiveEffect::FUSE));
}
