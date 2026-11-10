/**
 * @file    sensitivity_tests.cpp
 * @brief   Tests of the sensitivity analysis: how the vectors of the elements become the sensitivity of each part, the
 *          checks before a run, and the run on ngspice against closed-form values.
 */

#include "simulation/sensitivity.h"

#include "components/capacitor.h"
#include "components/controlled_source.h"
#include "components/diode.h"
#include "components/ground.h"
#include "components/logic_part.h"
#include "components/potentiometer.h"
#include "components/resistor.h"
#include "components/subcircuit.h"
#include "components/vcc.h"
#include "components/voltage_source.h"
#include "model/circuit.h"
#include "model/subcircuit.h"
#include "ngspice/ngspice_library.h"
#include "simulation/circuit_checks.h"
#include "simulation/simulation_backend.h"
#include "simulation/simulator.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <memory>
#include <optional>
#include <set>
#include <string>

namespace {

/**
 * @struct  Divider
 * @brief   A 5 V source on node 1 feeding 1k and 2k resistors in series to ground, with the middle on node 2.
 */
struct Divider {
    Core::VoltageSource Source;
    Core::Resistor Top;
    Core::Resistor Bottom;
    Core::Capacitor Filter;
    Core::Diode Clamp{Core::ComponentType::DIODE};
    Core::Ground Reference;
    Core::Circuit Circuit;

    Divider() {
        Source.SetName("V1");
        Source.SetValue(5.0);
        Top.SetName("R1");
        Top.SetValue(1e3);
        Bottom.SetName("R2");
        Bottom.SetValue(2e3);
        Filter.SetName("C1");
        Filter.SetValue(1e-6);
        Clamp.SetName("D1");
        Circuit.Add(Source, {1, 0});
        Circuit.Add(Top, {1, 2});
        Circuit.Add(Bottom, {2, 0});
        Circuit.Add(Filter, {2, 0});
        Circuit.Add(Clamp, {0, 2});
        Circuit.Add(Reference, {0});
    }
};

const Core::PartSensitivity &FindPart(const Core::Sensitivity &sensitivity, const std::string &name) {
    const auto found = std::ranges::find(sensitivity.Parts, name, &Core::PartSensitivity::Name);
    REQUIRE(found != sensitivity.Parts.end());
    return *found;
}

} // namespace

TEST_CASE("The sensitivity lists the parts with a value, the largest influence first", "[sensitivity]") {
    const Divider divider;
    // ngspice gives resistors both vectors, sources only their absolute one and has none for a capacitor at DC
    const Core::SensitivityVectors vectors = {{"r1", -1e-3},     {"r1_scale", -1.0}, {"r2", 5e-4},
                                              {"r2_scale", 1.0}, {"v1", 0.5},        {"d1_area", 7.0}};
    const Core::Sensitivity sensitivity = Core::BuildSensitivity(divider.Circuit, 2, vectors);

    CHECK(sensitivity.OutputNode == 2);
    // The diode and the ground have no value, so only the source, the two resistors and the capacitor are listed
    REQUIRE(sensitivity.Parts.size() == 4);
    // The source: normalized is its value times the absolute sensitivity, 5 V x 0.5
    CHECK(sensitivity.Parts[0].Name == "V1");
    CHECK_THAT(sensitivity.Parts[0].Normalized, Catch::Matchers::WithinRel(2.5, 1e-12));
    CHECK(sensitivity.Parts[0].Unit == "V");
    // The resistors keep their sign: a larger R1 lowers the output
    CHECK(FindPart(sensitivity, "R1").Normalized == -1.0);
    CHECK(FindPart(sensitivity, "R1").Absolute == -1e-3);
    CHECK(FindPart(sensitivity, "R1").Value == 1e3);
    CHECK(FindPart(sensitivity, "R2").Normalized == 1.0);
    // The capacitor has no vector at DC, so it counts as zero and comes last
    CHECK(sensitivity.Parts.back().Name == "C1");
    CHECK(sensitivity.Parts.back().Normalized == 0.0);
}

TEST_CASE("A controlled source reports its sensitivity to its gain", "[sensitivity]") {
    Core::ControlledSource source(Core::ComponentType::VCVS);
    source.SetName("E1");
    source.SetValue(4.0);
    Core::Circuit circuit;
    circuit.Add(source, {1, 0, 2, 0});
    const Core::Sensitivity sensitivity = Core::BuildSensitivity(circuit, 1, {{"e1_gain", 0.25}});
    REQUIRE(sensitivity.Parts.size() == 1);
    CHECK(sensitivity.Parts[0].Absolute == 0.25);
    CHECK(sensitivity.Parts[0].Normalized == 1.0);
}

TEST_CASE("A potentiometer reports the sum of its two halves, as its total resistance", "[sensitivity]") {
    Core::Potentiometer pot;
    pot.SetName("RV1");
    pot.SetValue(10e3);
    Core::Circuit circuit;
    circuit.Add(pot, {1, 0, 2});
    const Core::SensitivityVectors vectors = {{"r-rv1a_scale", -0.75}, {"r-rv1b_scale", 0.25}, {"r-rv1a", 1.0}};
    const Core::Sensitivity sensitivity = Core::BuildSensitivity(circuit, 2, vectors);
    REQUIRE(sensitivity.Parts.size() == 1);
    CHECK(sensitivity.Parts[0].Name == "RV1");
    CHECK_THAT(sensitivity.Parts[0].Normalized, Catch::Matchers::WithinRel(-0.5, 1e-12));
    // The absolute value is the change for one ohm of the total, 1/10k of the change for 100 %
    CHECK_THAT(sensitivity.Parts[0].Absolute, Catch::Matchers::WithinRel(-0.5 / 10e3, 1e-12));
    CHECK(sensitivity.Parts[0].Value == 10e3);
}

TEST_CASE("A block reports the element inside it with the largest influence, named by its path", "[sensitivity]") {
    Core::SubcircuitInstance first("filter", {"A"});
    first.SetName("X1");
    Core::SubcircuitInstance second("filter", {"A"});
    second.SetName("X2");
    Core::SubcircuitInstance empty("nothing", {"A"});
    empty.SetName("X3");
    Core::Circuit circuit;
    circuit.Add(first, {1});
    circuit.Add(second, {1});
    circuit.Add(empty, {1});
    const Core::SensitivityVectors vectors = {{"r.x1.r1", -2e-3},
                                              {"r.x1.r1_scale", -2.0},
                                              {"r.x2.r1", -1e-3},
                                              {"r.x2.r1_scale", -1.0},
                                              {"c.x2.c1", 5e5},
                                              {"c.x2.c1_scale", 0.5},
                                              // A nested block is named by the outer instance and the path inside it
                                              {"r.x2.x4.r7", 4e-4},
                                              {"r.x2.x4.r7_scale", 4.0},
                                              // The macromodel of an op-amp inside a block is not one of its values
                                              {"r.x2.x-u1.r2", 1.0},
                                              {"r.x2.x-u1.r2_scale", 90.0},
                                              {"r.x2.r-u1", 1.0},
                                              {"r.x2.r-u1_scale", 80.0},
                                              // Without its _scale vector a name is not an element of the block
                                              {"r.x1.rlone", 9.0}};
    const Core::Sensitivity sensitivity = Core::BuildSensitivity(circuit, 1, vectors);

    // X3 holds nothing with a value, so it is not listed
    REQUIRE(sensitivity.Parts.size() == 2);
    const Core::PartSensitivity &largest = sensitivity.Parts[0];
    CHECK(largest.Name == "X2");
    CHECK(largest.Element == "X4.R7");
    CHECK(largest.Normalized == 4.0);
    // The value is not in the results, but the two vectors give it back: 4 / 4e-4
    REQUIRE(largest.Value.has_value());
    CHECK_THAT(*largest.Value, Catch::Matchers::WithinRel(1e4, 1e-12));
    CHECK(largest.Unit == "Ohm");
    CHECK(sensitivity.Parts[1].Name == "X1");
    CHECK(sensitivity.Parts[1].Element == "R1");
}

TEST_CASE("The sensitivity analysis refuses ground as its output and a circuit with logic parts", "[sensitivity]") {
    const Divider divider;
    CHECK_FALSE(Core::CheckSensitivitySettings(divider.Circuit, {.OutputNode = 2}).has_value());
    CHECK(Core::CheckSensitivitySettings(divider.Circuit, {.OutputNode = 0}).has_value());
    CHECK(Core::CheckSensitivitySettings(divider.Circuit, {.OutputNode = 42}).has_value());

    // ngspice cannot recover from a sensitivity analysis of logic parts, so the run stops before it
    Core::LogicPart gate(Core::ComponentType::AND);
    gate.SetName("U1");
    const std::set<Core::ComponentType> parts = {Core::ComponentType::RESISTOR, Core::ComponentType::AND};
    const std::optional<std::string> reason =
        Core::FindUnsupported(Core::GetBackend(), parts, Core::AnalysisKind::SENSITIVITY);
    REQUIRE(reason.has_value());
    CHECK(reason->contains("logic"));
    // Any other analysis of those parts is the backend's business as before: refused only without the digital models
    CHECK(Core::FindUnsupported(Core::GetBackend(), parts, Core::AnalysisKind::OPERATING_POINT).has_value() ==
          !Core::AreDigitalModelsAvailable());
    CHECK_FALSE(
        Core::FindUnsupported(Core::GetBackend(), {Core::ComponentType::RESISTOR}, Core::AnalysisKind::SENSITIVITY)
            .has_value());
}

TEST_CASE("The sensitivity of a voltage divider matches its closed-form derivatives", "[sensitivity][simulator]") {
    const Divider divider;
    const Core::SensitivityRun run = Core::RunSensitivity(divider.Circuit, {.OutputNode = 2});
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    const Core::Sensitivity &sensitivity = *run.Result;
    // The output is 5 V x R2 / (R1 + R2), 3.33 V: the diode is reverse biased and the capacitor is open at DC
    const double total = 3e3;
    const double top = FindPart(sensitivity, "R1").Absolute;
    const double bottom = FindPart(sensitivity, "R2").Absolute;
    CHECK_THAT(top, Catch::Matchers::WithinRel(-5.0 * 2e3 / (total * total), 1e-4));
    CHECK_THAT(bottom, Catch::Matchers::WithinRel(5.0 * 1e3 / (total * total), 1e-4));
    CHECK_THAT(FindPart(sensitivity, "R1").Normalized, Catch::Matchers::WithinRel(1e3 * top, 1e-4));
    CHECK_THAT(FindPart(sensitivity, "R2").Normalized, Catch::Matchers::WithinRel(2e3 * bottom, 1e-4));
    // The source gives its share of the output: 5 V x 2/3
    CHECK_THAT(FindPart(sensitivity, "V1").Absolute, Catch::Matchers::WithinRel(2.0 / 3.0, 1e-4));
    CHECK_THAT(FindPart(sensitivity, "V1").Normalized, Catch::Matchers::WithinRel(10.0 / 3.0, 1e-4));
    CHECK(sensitivity.Parts.front().Name == "V1");
    CHECK_THAT(FindPart(sensitivity, "C1").Normalized, Catch::Matchers::WithinAbs(0.0, 1e-12));
}

TEST_CASE("The sensitivity analysis rejects an output that is ground or not a node", "[sensitivity][simulator]") {
    const Divider divider;
    CHECK_FALSE(Core::RunSensitivity(divider.Circuit, {.OutputNode = 0}).Result.has_value());
    CHECK_FALSE(Core::RunSensitivity(divider.Circuit, {.OutputNode = 42}).Result.has_value());
}

TEST_CASE("The sensitivity of a potentiometer divider does not depend on its total resistance",
          "[sensitivity][simulator]") {
    Core::VCC supply;
    supply.SetName("V1");
    supply.SetValue(10.0);
    Core::Potentiometer pot;
    pot.SetName("RV1");
    pot.SetValue(10e3);
    pot.SetWiper(0.25);
    const Core::Ground reference;
    Core::Circuit circuit;
    circuit.Add(supply, {1});
    circuit.Add(pot, {1, 0, 2});
    circuit.Add(reference, {0});
    const Core::SensitivityRun run = Core::RunSensitivity(circuit, {.OutputNode = 2});
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    // The wiper stays at the same share of the supply when both halves scale, which the sum of the halves shows
    CHECK_THAT(FindPart(*run.Result, "RV1").Normalized, Catch::Matchers::WithinAbs(0.0, 1e-6));
    CHECK_THAT(FindPart(*run.Result, "V1").Normalized, Catch::Matchers::WithinRel(7.5, 1e-4));
}

TEST_CASE("The sensitivity of a block is that of the largest element inside it", "[sensitivity][simulator]") {
    // A chain of 1k, 2k and 3k inside the block; the output is the middle of the 1k and the rest
    Core::Resistor top;
    top.SetName("R1");
    top.SetValue(1e3);
    Core::Resistor middle;
    middle.SetName("R2");
    middle.SetValue(2e3);
    Core::Resistor bottom;
    bottom.SetName("R3");
    bottom.SetValue(3e3);
    auto definition = std::make_shared<Core::SubcircuitDefinition>();
    definition->Name = "chain";
    definition->Pins = {{"A", 1}, {"B", 2}};
    definition->Body.Add(top, {1, 2});
    definition->Body.Add(middle, {2, 3});
    definition->Body.Add(bottom, {3, 0});
    Core::SubcircuitInstance instance("chain", {"A", "B"});
    instance.SetName("X1");

    Core::VoltageSource source;
    source.SetName("V1");
    source.SetValue(5.0);
    const Core::Ground reference;
    Core::Circuit circuit;
    circuit.Add(source, {1, 0});
    circuit.Add(instance, {1, 2});
    circuit.Add(reference, {0});
    circuit.AddSubcircuit(definition);

    const Core::SensitivityRun run = Core::RunSensitivity(circuit, {.OutputNode = 2});
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    const Core::PartSensitivity &block = FindPart(*run.Result, "X1");
    // Output 5 V x 5k / 6k; the top resistor moves it most: -5 V x 1k x 5k / (6k)^2 for 100 %
    CHECK(block.Element == "R1");
    CHECK_THAT(block.Normalized, Catch::Matchers::WithinRel(-5.0 * 1e3 * 5e3 / 36e6, 1e-4));
    CHECK_THAT(block.Absolute, Catch::Matchers::WithinRel(-5.0 * 5e3 / 36e6, 1e-4));
    REQUIRE(block.Value.has_value());
    CHECK_THAT(*block.Value, Catch::Matchers::WithinRel(1e3, 1e-4));
}

TEST_CASE("The sensitivity of a controlled source is that of its gain", "[sensitivity][simulator]") {
    Core::VoltageSource source;
    source.SetName("V1");
    source.SetValue(2.0);
    Core::ControlledSource amplifier(Core::ComponentType::VCVS);
    amplifier.SetName("E1");
    amplifier.SetValue(4.0);
    Core::Resistor load;
    load.SetName("R1");
    const Core::Ground reference;
    Core::Circuit circuit;
    circuit.Add(source, {2, 0});
    circuit.Add(amplifier, {1, 0, 2, 0});
    circuit.Add(load, {1, 0});
    circuit.Add(reference, {0});
    const Core::SensitivityRun run = Core::RunSensitivity(circuit, {.OutputNode = 1});
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    // The output is gain x 2 V: 2 V per unit of gain, and 8 V for 100 % more gain or 100 % more input
    CHECK_THAT(FindPart(*run.Result, "E1").Absolute, Catch::Matchers::WithinRel(2.0, 1e-4));
    CHECK_THAT(FindPart(*run.Result, "E1").Normalized, Catch::Matchers::WithinRel(8.0, 1e-4));
    CHECK_THAT(FindPart(*run.Result, "V1").Normalized, Catch::Matchers::WithinRel(8.0, 1e-4));
}
