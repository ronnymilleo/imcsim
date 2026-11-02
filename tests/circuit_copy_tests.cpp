/**
 * @file    circuit_copy_tests.cpp
 * @brief   Tests for copying a circuit and its parts, which a simulation on another thread works on.
 */

#include "circuit_copy.h"

#include "components/capacitor.h"
#include "components/diode.h"
#include "components/ground.h"
#include "components/op_amp.h"
#include "components/resistor.h"
#include "components/voltage_source.h"
#include "examples.h"
#include "schematic.h"
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <string>
#include <utility>

TEST_CASE("A cloned part is an independent copy with the same kind, name, value and settings", "[circuit_copy]") {
    Core::Capacitor original;
    original.SetName("C7");
    original.SetValue(47e-9);
    original.SetInitialCondition(2.5);

    const std::unique_ptr<Core::Component> clone = original.Clone();
    CHECK(clone.get() != &original);
    CHECK(clone->GetType() == Core::ComponentType::CAPACITOR);
    CHECK(clone->GetName() == "C7");
    CHECK(clone->GetValue() == 47e-9);
    CHECK(clone->GetInitialCondition() == 2.5);

    original.SetValue(1e-6);
    original.ClearInitialCondition();
    CHECK(clone->GetValue() == 47e-9);
    CHECK(clone->GetInitialCondition() == 2.5);
}

TEST_CASE("A clone keeps the settings of the derived class, which a copy of the base would lose", "[circuit_copy]") {
    Core::OpAmp amplifier;
    amplifier.SetIdealBandwidth(2e6);
    Core::VoltageSource source;
    source.SetSourceType(Core::Source::SourceType::AC);

    const std::unique_ptr<Core::Component> amplifier_clone = amplifier.Clone();
    const std::unique_ptr<Core::Component> source_clone = source.Clone();
    CHECK(static_cast<const Core::OpAmp &>(*amplifier_clone).GetIdealBandwidth() == 2e6);
    CHECK(static_cast<const Core::VoltageSource &>(*source_clone).GetSourceType() == Core::Source::SourceType::AC);
}

TEST_CASE("A circuit copy writes the same netlist and outlives the parts it was made from", "[circuit_copy]") {
    auto resistor = std::make_unique<Core::Resistor>();
    resistor->SetName("R1");
    resistor->SetValue(2.2e3);
    auto diode = std::make_unique<Core::Diode>(Core::ComponentType::DIODE);
    diode->SetName("D1");
    const Core::Ground ground;
    Core::Circuit circuit;
    circuit.Add(*resistor, {1, 2});
    circuit.Add(*diode, {2, 0});
    circuit.Add(ground, {0});
    const std::string netlist = circuit.ToSpiceNetlist("", true);

    Core::CircuitCopy copy(circuit);
    CHECK(copy.GetCircuit().ToSpiceNetlist("", true) == netlist);
    CHECK(copy.GetCircuit().GetEntries()[0].Part != resistor.get());

    // The copy is what a worker thread reads while the user edits or deletes the originals
    resistor->SetValue(1.0);
    resistor.reset();
    diode.reset();
    CHECK(copy.GetCircuit().ToSpiceNetlist("", true) == netlist);

    // Moving it keeps the circuit pointing at the parts, which stay where they are on the heap
    const Core::CircuitCopy moved = std::move(copy);
    CHECK(moved.GetCircuit().ToSpiceNetlist("", true) == netlist);
}

TEST_CASE("The copy of every example circuit writes the netlist of the original", "[circuit_copy][examples]") {
    for (const GUI::Example &example : GUI::GetExamples()) {
        INFO(example.Title);
        auto loaded = GUI::LoadExample(example);
        REQUIRE(loaded.has_value());
        GUI::Schematic schematic;
        schematic.Replace(std::move(loaded->Elements), std::move(loaded->Wires), std::move(loaded->Settings));
        const Core::Circuit circuit = schematic.BuildCircuit();
        const Core::CircuitCopy copy(circuit);
        CHECK(copy.GetCircuit().ToSpiceNetlist("", true) == circuit.ToSpiceNetlist("", true));
        CHECK(copy.GetCircuit().GetEntries().size() == circuit.GetEntries().size());
    }
}
