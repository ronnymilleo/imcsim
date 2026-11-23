/**
 * @file    analysis_preamble_tests.cpp
 * @brief   Tests of the steps that the small-signal analyses take before their own: the checks of the output node and
 *          the input source, the operating point and the reactive system linearized around it.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "analyses/common/analysis_preamble.h"
#include "api/circuit_impl.h"
#include "devices/sources/independent_sources.h"

#include <nodalys/nodalys.h>

#include <string>
#include <utility>
#include <vector>

namespace {

using Catch::Matchers::WithinRel;

/// A source of 10 V and a current source driving a divider; the middle of the divider is node 2.
const std::string DIVIDER = "divider\n"
                            "V1 1 0 DC 10 AC 1\n"
                            "I1 0 3 DC 1m\n"
                            "R1 1 2 1k\n"
                            "R2 2 0 1k\n"
                            "R3 3 0 1k\n"
                            "C1 2 0 1u\n"
                            ".end\n";

/// A circuit parsed from a netlist, which the analyses work on.
Nodalys::Circuit ParseCircuit(const std::string &netlist) {
    auto parsed = Nodalys::ParseNetlist(netlist);
    REQUIRE(parsed.has_value());
    return std::move(parsed->CircuitInstance);
}

} // namespace

TEST_CASE("An output node must be in the circuit and cannot be ground", "[nodalys][analysis_preamble]") {
    const Nodalys::Circuit circuit = ParseCircuit(DIVIDER);
    const Nodalys::CompiledCircuit &compiled = Nodalys::CircuitAccess::GetCompiled(circuit);

    CHECK_FALSE(Nodalys::CheckOutputNode(compiled, 1).has_value());
    CHECK_FALSE(Nodalys::CheckOutputNode(compiled, compiled.CircuitNodeCount - 1).has_value());
    CHECK(Nodalys::CheckOutputNode(compiled, 0) == "The circuit has no output node 0");
    CHECK(Nodalys::CheckOutputNode(compiled, compiled.CircuitNodeCount) == "The circuit has no output node 4");
    CHECK(Nodalys::CheckOutputNode(compiled, -2) == "The circuit has no output node -2");
}

TEST_CASE("An input source is found by name and told apart as a voltage or a current source",
          "[nodalys][analysis_preamble]") {
    const Nodalys::Circuit circuit = ParseCircuit(DIVIDER);
    const Nodalys::CompiledCircuit &compiled = Nodalys::CircuitAccess::GetCompiled(circuit);

    const auto voltage = Nodalys::ResolveInputSource(compiled, "V1", "a test");
    REQUIRE(voltage.has_value());
    CHECK(voltage->IsVoltage());
    CHECK(voltage->AsVoltage() != nullptr);
    CHECK(&voltage->GetSource() == compiled.SourcesByName.at("V1"));
    CHECK(&voltage->GetDevice() == compiled.SourcesByName.at("V1"));

    const auto current = Nodalys::ResolveInputSource(compiled, "I1", "a test");
    REQUIRE(current.has_value());
    CHECK_FALSE(current->IsVoltage());
    CHECK(current->AsVoltage() == nullptr);
    CHECK(&current->GetSource() == compiled.SourcesByName.at("I1"));
    CHECK(&current->GetDevice() == compiled.SourcesByName.at("I1"));
}

TEST_CASE("A name that is no source is refused, and so is a source that cannot drive the analysis",
          "[nodalys][analysis_preamble]") {
    const Nodalys::Circuit circuit = ParseCircuit(DIVIDER);
    auto run = Nodalys::CircuitAccess::CloneForRun(circuit);
    REQUIRE(run.has_value());
    Nodalys::CompiledCircuit &compiled = *run;

    const auto missing = Nodalys::ResolveInputSource(compiled, "nosuch", "a test");
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error() == "The circuit has no source named nosuch");

    // No device but a voltage or a current source is listed by name, so one is listed here to reach the refusal
    compiled.SourcesByName["R1"] = compiled.ElementsByName.at("R1");
    const auto resistor = Nodalys::ResolveInputSource(compiled, "R1", "a noise analysis");
    REQUIRE_FALSE(resistor.has_value());
    CHECK(resistor.error() == "The source R1 cannot drive a noise analysis");
    CHECK(Nodalys::FindSourceByName(compiled, "R1").has_value());
}

TEST_CASE("A source is found by its name in any case, as SPICE reads names", "[nodalys][analysis_preamble]") {
    const Nodalys::Circuit circuit = ParseCircuit(DIVIDER);
    const Nodalys::CompiledCircuit &compiled = Nodalys::CircuitAccess::GetCompiled(circuit);
    const auto source = Nodalys::FindSourceByName(compiled, "v1");
    REQUIRE(source.has_value());
    CHECK(*source == compiled.SourcesByName.at("V1"));
}

TEST_CASE("The operating point of a divider is solved and its convergence reported", "[nodalys][analysis_preamble]") {
    const Nodalys::Circuit circuit = ParseCircuit(DIVIDER);
    auto run = Nodalys::CircuitAccess::CloneForRun(circuit);
    REQUIRE(run.has_value());
    Nodalys::CompiledCircuit &compiled = *run;
    std::vector<Nodalys::DiagnosticMessage> messages;

    const auto x_dc = Nodalys::SolveOperatingPoint(compiled, Nodalys::ConvergenceOptions{}, "unused", &messages);

    REQUIRE(x_dc.has_value());
    // Half of the 10 V, since the capacitor is open at DC
    const auto middle = static_cast<std::size_t>(compiled.System->NodeIndex(2));
    CHECK_THAT((*x_dc)[middle], WithinRel(5.0, 1e-6));
    REQUIRE(messages.size() == 1);
    CHECK(messages[0].Text.starts_with("nodalys: operating point converged in "));
}

TEST_CASE("A circuit with no operating point is refused with the message of the analysis",
          "[nodalys][analysis_preamble]") {
    const Nodalys::Circuit circuit = ParseCircuit("diode\nV1 1 0 DC 5\nR1 1 2 100\nD1 2 0 DMOD\n"
                                                  ".model DMOD D(IS=1e-14)\n.end\n");
    auto run = Nodalys::CircuitAccess::CloneForRun(circuit);
    REQUIRE(run.has_value());
    Nodalys::CompiledCircuit &compiled = *run;
    Nodalys::ConvergenceOptions options;
    options.MaxIterations = 1;
    options.GminSteps = 0;
    options.SourceSteps = 0;
    std::vector<Nodalys::DiagnosticMessage> messages;

    const auto x_dc = Nodalys::SolveOperatingPoint(compiled, options, "No operating point for the test", &messages);

    REQUIRE_FALSE(x_dc.has_value());
    CHECK(x_dc.error().starts_with("No operating point for the test"));
    CHECK(messages.empty());
}

TEST_CASE("An empty system made like another has its unknowns and no stamps", "[nodalys][analysis_preamble]") {
    const Nodalys::Circuit circuit = ParseCircuit(DIVIDER);
    const Nodalys::MnaSystem &original = *Nodalys::CircuitAccess::GetCompiled(circuit).System;

    const Nodalys::MnaSystem empty = Nodalys::MakeEmptySystemLike(original);

    REQUIRE(empty.GetSize() == original.GetSize());
    for (int index = 0; index < original.GetSize(); ++index) {
        CHECK(empty.IsNodeUnknown(index) == original.IsNodeUnknown(index));
        for (int column = 0; column < original.GetSize(); ++column) {
            CHECK(empty.Matrix().At(static_cast<std::size_t>(index), static_cast<std::size_t>(column)) == 0.0);
        }
    }
}

TEST_CASE("The reactive system holds the capacitance of a capacitor and the AC excitation of a source",
          "[nodalys][analysis_preamble]") {
    const Nodalys::Circuit circuit = ParseCircuit(DIVIDER);
    const Nodalys::CompiledCircuit &compiled = Nodalys::CircuitAccess::GetCompiled(circuit);
    const std::vector<double> x_dc(static_cast<std::size_t>(compiled.System->GetSize()), 0.0);

    const Nodalys::MnaSystem reactive = Nodalys::BuildReactiveSystem(compiled, x_dc);

    const auto middle = static_cast<std::size_t>(compiled.System->NodeIndex(2));
    CHECK_THAT(reactive.Matrix().At(middle, middle), WithinRel(1e-6, 1e-12));
    const auto branch = static_cast<std::size_t>(
        dynamic_cast<const Nodalys::VoltageSourceDevice *>(compiled.SourcesByName.at("V1"))->GetBranch());
    CHECK_THAT(reactive.Rhs()[branch], WithinRel(1.0, 1e-12));
}
