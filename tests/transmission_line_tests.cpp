/**
 * @file    transmission_line_tests.cpp
 * @brief   Tests for the transmission line component, its schematic symbol and serialization, netlist and current
 *          readout wiring, and Nodalys vs. ngspice backend capabilities.
 */

#include "backends/ngspice/ngspice_backend.h"
#include "backends/nodalys/nodalys_backend.h"
#include "circuit/circuit_currents.h"
#include "circuit/circuit_netlist.h"
#include "circuit/components/ground.h"
#include "circuit/components/resistor.h"
#include "circuit/components/subcircuit_pin.h"
#include "circuit/components/transformer.h"
#include "circuit/components/transmission_line.h"
#include "circuit/components/vcc.h"
#include "circuit/components/voltage_source.h"
#include "nodalys_in_use.h"
#include "results/current_flow.h"
#include "schematic/element_factory.h"
#include "schematic/schematic.h"
#include "schematic/schematic_file.h"
#include "simulation/analysis_suggestions.h"
#include "simulation/simulation_backend.h"
#include "simulation/simulator.h"
#include "ui_elements/ui_transmission_line.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cmath>
#include <complex>
#include <memory>
#include <set>
#include <string>
#include <vector>

using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

TEST_CASE("TransmissionLine validates its characteristic impedance and dispersive loss parameters",
          "[transmission_line]") {
    Core::TransmissionLine line;
    line.SetName("TL1");
    line.SetValue(75.0);
    CHECK(line.GetType() == Core::ComponentType::TRANSMISSION_LINE);
    CHECK(std::string(line.GetTypeName()) == "TransmissionLine");
    CHECK(std::string(line.GetNamePrefix()) == "TL");
    CHECK(std::string(line.GetUnit()) == "Ohm");
    CHECK(line.GetValue() == 75.0);
    CHECK_FALSE(line.GetParameters().IsLossy());
    // Default delay is 1 ns -> quarter-wave frequency is 1 / (4 * 1 ns) = 250 MHz
    CHECK_THAT(line.GetQuarterWaveFrequency(), WithinRel(250e6, 1e-12));

    CHECK(Core::TransmissionLine::IsValidParameters({.Delay = 1e-12}));
    CHECK_FALSE(Core::TransmissionLine::IsValidParameters({.Delay = 0.0}));
    CHECK_FALSE(Core::TransmissionLine::IsValidParameters({.Delay = -1e-9}));
    CHECK_FALSE(Core::TransmissionLine::IsValidParameters({.DcResistance = -0.1}));
    CHECK_FALSE(Core::TransmissionLine::IsValidParameters({.SkinResistance = -0.1}));
    CHECK_FALSE(Core::TransmissionLine::IsValidParameters({.DcConductance = -0.1}));
    CHECK_FALSE(Core::TransmissionLine::IsValidParameters({.LossTangent = -0.01}));
    CHECK_FALSE(Core::TransmissionLine::IsValidParameters({.ReferenceFrequency = 0.0}));

    line.SetParameters({.Delay = 1e-9, .SkinResistance = 2.5});
    CHECK(line.GetParameters().IsLossy());
    line.SetParameters({.Delay = 1e-9, .SkinResistance = -1.0});
    CHECK(line.GetParameters().SkinResistance == 2.5);
}

TEST_CASE("Transmission lines take TL names without colliding with transformers, and suggest RF AC sweeps",
          "[transmission_line][schematic]") {
    GUI::Schematic schematic;
    schematic.AddElement(GUI::CreateElement(Core::ComponentType::TRANSFORMER, {0, 0}, GUI::Rotation::R0));
    schematic.AddElement(GUI::CreateElement(Core::ComponentType::TRANSMISSION_LINE, {6, 0}, GUI::Rotation::R0));
    schematic.AddElement(GUI::CreateElement(Core::ComponentType::TRANSMISSION_LINE, {12, 0}, GUI::Rotation::R0));

    CHECK(schematic.GetElement(0).GetComponent().GetName() == "T1");
    CHECK(schematic.GetElement(1).GetComponent().GetName() == "TL1");
    CHECK(schematic.GetElement(2).GetComponent().GetName() == "TL2");

    Core::Circuit circuit;
    Core::VoltageSource source;
    source.SetName("Vin1");
    source.SetSourceType(Core::Source::SourceType::AC);
    Core::TransmissionLine line;
    line.SetName("TL1");
    line.SetValue(50.0);
    line.SetParameters({.Delay = 1e-9});
    Core::Resistor load;
    load.SetName("R1");
    load.SetValue(50.0);
    Core::Ground ground;
    circuit.Add(source, {1, 0});
    circuit.Add(line, {1, 0, 2, 0});
    circuit.Add(load, {2, 0});
    circuit.Add(ground, {0});

    const auto suggested = Core::SuggestACSweepSettings(circuit);
    REQUIRE(suggested.has_value());
    // Quarter-wave is 250 MHz, so the suggested sweep reaches into the GHz range rather than stopping at audio
    CHECK(suggested->StopFrequency >= 250e6);
}

TEST_CASE("Netlist writing, current probes and schematic current arrows cover all four terminals of a line",
          "[transmission_line][netlist][currents]") {
    Core::Circuit circuit;
    Core::VCC supply;
    supply.SetName("V1");
    supply.SetValue(5.0);
    Core::Resistor source_resistor;
    source_resistor.SetName("RS");
    source_resistor.SetValue(50.0);
    Core::TransmissionLine line;
    line.SetName("TL1");
    line.SetValue(50.0);
    line.SetParameters({.Delay = 2e-9,
                        .DcResistance = 1.5,
                        .SkinResistance = 4.0,
                        .DcConductance = 1e-5,
                        .LossTangent = 0.01,
                        .ReferenceFrequency = 2e9});
    Core::Resistor load;
    load.SetName("RL");
    load.SetValue(50.0);
    Core::Ground ground;
    circuit.Add(supply, {1});
    circuit.Add(source_resistor, {1, 2});
    circuit.Add(line, {2, 0, 3, 0});
    circuit.Add(load, {3, 0});
    circuit.Add(ground, {0});

    const std::string netlist = Core::WriteSpiceNetlist(circuit);
    CHECK_THAT(netlist, ContainsSubstring("TL1 2 0 3 0 Z0=50 TD=2n"));
    CHECK_THAT(netlist, ContainsSubstring("RDC=1.5"));
    CHECK_THAT(netlist, ContainsSubstring("RSKIN=4"));
    CHECK_THAT(netlist, ContainsSubstring("GDC=10u"));
    CHECK_THAT(netlist, ContainsSubstring("TAND=10m"));
    CHECK_THAT(netlist, ContainsSubstring("FREF=2G"));

    const std::vector<Core::CurrentProbe> probes = Core::GetCurrentProbes(line);
    REQUIRE(probes.size() == 2);
    CHECK(probes[0].Terminal == 0);
    CHECK(std::string(probes[0].Label) == "P1");
    CHECK(probes[1].Terminal == 2);
    CHECK(std::string(probes[1].Label) == "P2");

    const std::vector<Core::ComponentCurrent> currents{{"TL1.P1", 0.05}, {"TL1.P2", -0.05}};
    CHECK_THAT(*GUI::GetTerminalCurrent(line, 0, currents), WithinAbs(0.05, 1e-12));
    CHECK_THAT(*GUI::GetTerminalCurrent(line, 1, currents), WithinAbs(-0.05, 1e-12));
    CHECK_THAT(*GUI::GetTerminalCurrent(line, 2, currents), WithinAbs(-0.05, 1e-12));
    CHECK_THAT(*GUI::GetTerminalCurrent(line, 3, currents), WithinAbs(0.05, 1e-12));
}

TEST_CASE("A transmission line symbol exposes four grid terminals and round-trips all six parameters through JSON",
          "[transmission_line][schematic_file][ui_element]") {
    std::vector<std::unique_ptr<GUI::UIElement>> elements;
    elements.push_back(GUI::CreateElement(Core::ComponentType::TRANSMISSION_LINE, {4, 6}, GUI::Rotation::R0));
    auto &line = static_cast<Core::TransmissionLine &>(elements.back()->GetComponent());
    line.SetName("TL1");
    line.SetValue(75.0);
    line.SetParameters({.Delay = 2.5e-9,
                        .DcResistance = 0.8,
                        .SkinResistance = 3.2,
                        .DcConductance = 2e-5,
                        .LossTangent = 0.015,
                        .ReferenceFrequency = 2.4e9});

    const std::vector<GUI::GridPoint> terminals = elements.back()->GetTerminals();
    REQUIRE(terminals.size() == 4);
    CHECK(terminals == std::vector<GUI::GridPoint>{{2, 5}, {2, 7}, {6, 5}, {6, 7}});

    const std::string saved = GUI::SaveSchematic(elements, {}, {}, {});
    const auto loaded = GUI::LoadSchematic(saved);
    REQUIRE(loaded.has_value());
    CHECK(loaded->Warnings.empty());
    REQUIRE(loaded->Elements.size() == 1);

    const auto *copy = dynamic_cast<const Core::TransmissionLine *>(&loaded->Elements[0]->GetComponent());
    REQUIRE(copy != nullptr);
    CHECK(copy->GetName() == "TL1");
    CHECK_THAT(copy->GetValue(), WithinAbs(75.0, 1e-12));
    CHECK_THAT(copy->GetParameters().Delay, WithinAbs(2.5e-9, 1e-18));
    CHECK_THAT(copy->GetParameters().DcResistance, WithinAbs(0.8, 1e-12));
    CHECK_THAT(copy->GetParameters().SkinResistance, WithinAbs(3.2, 1e-12));
    CHECK_THAT(copy->GetParameters().DcConductance, WithinAbs(2e-5, 1e-18));
    CHECK_THAT(copy->GetParameters().LossTangent, WithinAbs(0.015, 1e-12));
    CHECK_THAT(copy->GetParameters().ReferenceFrequency, WithinAbs(2.4e9, 1e-3));

    // A non-positive delay is skipped with a warning
    const auto invalid =
        GUI::LoadSchematic(R"({"format": "imcsim-schematic", "version": 1, "elements": [)"
                           R"({"type": "TransmissionLine", "name": "TL1", "value": 50, "x": 0, "y": 0, "rotation": 0, )"
                           R"("parameters": {"delay": 0}}], "wires": []})");
    REQUIRE(invalid.has_value());
    CHECK(invalid->Elements.empty());
    CHECK(invalid->Warnings.size() == 1);
}

TEST_CASE("On ngspice a one-port line is refused for its part, not for its port count",
          "[transmission_line][simulator]") {
    Core::SetBackend(nullptr);
    Core::TransmissionLine line;
    line.SetName("TL1");
    line.SetValue(100.0);
    line.SetParameters({.Delay = 1e-9});
    Core::Resistor load;
    load.SetName("RL");
    load.SetValue(200.0);
    Core::Ground ground;
    Core::Circuit circuit;
    circuit.AddPort({.Number = 1, .Node = 1, .ReferenceOhms = 50.0});
    circuit.Add(line, {1, 0, 2, 0});
    circuit.Add(load, {2, 0});
    circuit.Add(ground, {0});

    // A second port would not help, since ngspice cannot simulate the line at all
    const Core::SParametersRun run = Core::RunSParameters(circuit, {});
    REQUIRE_FALSE(run.Result.has_value());
    CHECK_THAT(run.Result.error(), ContainsSubstring("Transmission lines are simulated natively by Nodalys"));
    CHECK_THAT(run.Result.error(), !ContainsSubstring("S-parameter ports"));
}

TEST_CASE("Nodalys simulates lossless and lossy transmission lines while ngspice and pole-zero refuse them cleanly",
          "[transmission_line][nodalys_backend][simulation_backend]") {
    Core::NgspiceBackend ngspice;
    CHECK(Core::FindUnsupported(ngspice, {Core::ComponentType::TRANSMISSION_LINE}, Core::AnalysisKind::OPERATING_POINT)
              .has_value());

    const TestSupport::NodalysInUse guard;
    Core::SimulationBackend &backend = Core::GetBackend();
    CHECK_FALSE(
        Core::FindUnsupported(backend, {Core::ComponentType::TRANSMISSION_LINE}, Core::AnalysisKind::OPERATING_POINT)
            .has_value());

    // Lossless quarter-wave transformer (Z0 = 100 ohm, Td = 1 ns) matching 200 ohm load to a 50 ohm S-parameter port
    Core::Circuit lossless_circuit;
    Core::TransmissionLine lossless_line;
    lossless_line.SetName("TL1");
    lossless_line.SetValue(100.0);
    lossless_line.SetParameters({.Delay = 1e-9});
    Core::Resistor load;
    load.SetName("RL");
    load.SetValue(200.0);
    Core::Ground ground;
    lossless_circuit.AddPort({.Number = 1, .Node = 1, .ReferenceOhms = 50.0});
    lossless_circuit.Add(lossless_line, {1, 0, 2, 0});
    lossless_circuit.Add(load, {2, 0});
    lossless_circuit.Add(ground, {0});

    CHECK(Core::FindUnsupported(backend, lossless_circuit, Core::AnalysisKind::POLE_ZERO).has_value());
    CHECK_FALSE(Core::FindUnsupported(backend, lossless_circuit, Core::AnalysisKind::TRANSIENT).has_value());

    const Core::SParametersRun sp_run = backend.RunSParameters(
        lossless_circuit,
        {.StartFrequency = 250e6, .StopFrequency = 500e6, .IsLogarithmic = false, .LinearPoints = 2});
    REQUIRE(sp_run.Result.has_value());
    CHECK_THAT(std::abs(sp_run.Result->Get(0, 1, 1)), WithinAbs(0.0, 1e-8));
    CHECK_THAT(sp_run.Result->Get(1, 1, 1).real(), WithinAbs(0.6, 1e-8));

    // Transient pulse reflection on the lossless line through NodalysBackend
    Core::Circuit tran_circuit;
    Core::VoltageSource pulse;
    pulse.SetName("Vin1");
    pulse.SetSourceType(Core::Source::SourceType::PULSE);
    pulse.SetPulse({.Low = 0.0,
                    .High = 2.0,
                    .Delay = 0.5e-9,
                    .RiseTime = 0.1e-9,
                    .FallTime = 0.1e-9,
                    .Width = 20e-9,
                    .Period = 40e-9});
    Core::Resistor source_resistor;
    source_resistor.SetName("RS");
    source_resistor.SetValue(50.0);
    Core::TransmissionLine matched_line;
    matched_line.SetName("TL1");
    matched_line.SetValue(50.0);
    matched_line.SetParameters({.Delay = 2e-9});
    Core::Resistor mismatch_load;
    mismatch_load.SetName("RL");
    mismatch_load.SetValue(150.0);
    tran_circuit.Add(pulse, {1, 0});
    tran_circuit.Add(source_resistor, {1, 2});
    tran_circuit.Add(matched_line, {2, 0, 3, 0});
    tran_circuit.Add(mismatch_load, {3, 0});
    tran_circuit.Add(ground, {0});

    const Core::TransientRun tran_run =
        backend.RunTransient(tran_circuit, {.StopTime = 6e-9, .TimeStep = 0.1e-9}, std::nullopt, {});
    REQUIRE(tran_run.Result.has_value());
    const auto &node3 = tran_run.Result->NodeVoltages[3];
    REQUIRE_FALSE(node3.empty());
    CHECK_THAT(node3.back(), WithinAbs(1.5, 1e-3));

    // Lossy dispersive line: runs in AC, S-parameters and Harmonic Balance, and is refused before a Transient run
    Core::Circuit lossy_circuit;
    Core::VoltageSource sine;
    sine.SetName("Vin1");
    sine.SetSourceType(Core::Source::SourceType::AC);
    sine.SetAC({.Amplitude = 2.0, .Frequency = 250e6, .Offset = 0.0, .Magnitude = 2.0});
    Core::TransmissionLine lossy_line;
    lossy_line.SetName("TL1");
    lossy_line.SetValue(50.0);
    lossy_line.SetParameters({.Delay = 1e-9,
                              .DcResistance = 2.0,
                              .SkinResistance = 8.0,
                              .DcConductance = 0.0,
                              .LossTangent = 0.02,
                              .ReferenceFrequency = 1e9});
    Core::Resistor matched_load;
    matched_load.SetName("RL");
    matched_load.SetValue(50.0);
    lossy_circuit.Add(sine, {1, 0});
    lossy_circuit.Add(source_resistor, {1, 2});
    lossy_circuit.Add(lossy_line, {2, 0, 3, 0});
    lossy_circuit.Add(matched_load, {3, 0});
    lossy_circuit.Add(ground, {0});

    const std::optional<std::string> tran_problem =
        Core::FindUnsupported(backend, lossy_circuit, Core::AnalysisKind::TRANSIENT);
    REQUIRE(tran_problem.has_value());
    CHECK_THAT(*tran_problem, ContainsSubstring("lossless transmission lines only"));
    CHECK_FALSE(Core::FindUnsupported(backend, lossy_circuit, Core::AnalysisKind::AC_SWEEP).has_value());
    CHECK_FALSE(Core::FindUnsupported(backend, lossy_circuit, Core::AnalysisKind::HARMONIC_BALANCE).has_value());

    const Core::ACSweepRun ac_run =
        backend.RunACSweep(lossy_circuit,
                           {.StartFrequency = 250e6, .StopFrequency = 2.5e9, .PointsPerDecade = 5},
                           std::nullopt);
    REQUIRE(ac_run.Result.has_value());
    CHECK(ac_run.Result->NodeMagnitudesDecibels[3].back() < ac_run.Result->NodeMagnitudesDecibels[3].front());

    const Core::HarmonicBalanceRun hb_run =
        backend.RunHarmonicBalance(lossy_circuit, {.FundamentalFrequency = 250e6, .Harmonics = 4}, nullptr);
    REQUIRE(hb_run.Result.has_value());
    CHECK_THAT(20.0 * std::log10(std::abs(hb_run.Result->NodePhasors[3][1])),
               WithinAbs(ac_run.Result->NodeMagnitudesDecibels[3].front(), 1e-4));
}
