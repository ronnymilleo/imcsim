/**
 * @file    pole_zero_tests.cpp
 * @brief   Tests of the pole-zero analysis: how the roots a solver reports become rows, the messages that explain a
 *          failure, the checks before a run, and the run on ngspice against closed-form poles.
 */

#include "simulation/pole_zero.h"

#include "components/capacitor.h"
#include "components/current_source.h"
#include "components/ground.h"
#include "components/inductor.h"
#include "components/logic_part.h"
#include "components/op_amp.h"
#include "components/resistor.h"
#include "components/vcc.h"
#include "components/voltage_source.h"
#include "model/circuit.h"
#include "simulation/circuit_checks.h"
#include "simulation/simulation_backend.h"
#include "simulation/simulator.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

namespace {

constexpr double TWO_PI = 2.0 * std::numbers::pi;

/**
 * @struct  SeriesRLC
 * @brief   A source on node 1, then an inductor to node 2, a capacitor to node 3 and a resistor to ground; the output
 * is the voltage across the resistor, so the transfer is a band-pass one with a zero at the origin.
 */
struct SeriesRLC {
    Core::VoltageSource Source;
    Core::Inductor Coil;
    Core::Capacitor Plate;
    Core::Resistor Load;
    Core::Ground Reference;
    Core::Circuit Circuit;

    SeriesRLC() {
        Source.SetName("Vin1");
        Coil.SetName("L1");
        Coil.SetValue(10e-3);
        Plate.SetName("C1");
        Plate.SetValue(100e-9);
        Load.SetName("R1");
        Load.SetValue(100.0);
        Circuit.Add(Source, {1, 0});
        Circuit.Add(Coil, {1, 2});
        Circuit.Add(Plate, {2, 3});
        Circuit.Add(Load, {3, 0});
        Circuit.Add(Reference, {0});
    }
};

/// A 1k resistor and a 100n capacitor to ground, driven by a source on node 1, with the output on node 2.
struct LowPass {
    Core::VoltageSource Source;
    Core::Resistor Series;
    Core::Capacitor Shunt;
    Core::Ground Reference;
    Core::Circuit Circuit;

    LowPass() {
        Source.SetName("Vin1");
        Series.SetName("R1");
        Series.SetValue(1e3);
        Shunt.SetName("C1");
        Shunt.SetValue(100e-9);
        Circuit.Add(Source, {1, 0});
        Circuit.Add(Series, {1, 2});
        Circuit.Add(Shunt, {2, 0});
        Circuit.Add(Reference, {0});
    }
};

const Core::PoleZeroRoot &FindRoot(const Core::PoleZero &result, const bool is_pole, const std::size_t index = 0) {
    std::size_t seen = 0;
    for (const Core::PoleZeroRoot &root : result.Roots) {
        if (root.IsPole == is_pole && seen++ == index) {
            return root;
        }
    }
    FAIL("no such root");
    return result.Roots.front();
}

std::size_t CountRoots(const Core::PoleZero &result, const bool is_pole) {
    return static_cast<std::size_t>(std::ranges::count(result.Roots, is_pole, &Core::PoleZeroRoot::IsPole));
}

} // namespace

TEST_CASE("A conjugate pair becomes one row with its quality factor, damping and frequency", "[pole_zero]") {
    const LowPass filter;
    // -5000 +/- j31225 rad/s is the series RLC of 10 mH, 100 nF and 100 Ohm
    const Core::ComplexRoots poles = {{-5000.0, 31225.0}, {-5000.0, -31225.0}};
    const Core::PoleZero result =
        Core::BuildPoleZero(filter.Circuit, {.OutputNode = 2, .InputSource = "Vin1"}, poles, {});

    REQUIRE(result.Roots.size() == 1);
    const Core::PoleZeroRoot &root = result.Roots.front();
    CHECK(root.IsPole);
    CHECK(root.Real == -5000.0);
    CHECK(root.Imaginary == 31225.0);
    const double size = std::hypot(5000.0, 31225.0);
    CHECK_THAT(root.NaturalFrequencyHertz, Catch::Matchers::WithinRel(size / TWO_PI, 1e-12));
    REQUIRE(root.QualityFactor.has_value());
    CHECK_THAT(*root.QualityFactor, Catch::Matchers::WithinRel(size / 10000.0, 1e-12));
    REQUIRE(root.DampingRatio.has_value());
    CHECK_THAT(*root.DampingRatio, Catch::Matchers::WithinRel(5000.0 / size, 1e-12));
    // The damping ratio is 1 / (2 Q) for a pair
    CHECK_THAT(*root.DampingRatio * 2.0 * *root.QualityFactor, Catch::Matchers::WithinRel(1.0, 1e-12));
    CHECK_FALSE(root.IsRightHalfPlane);
    CHECK(result.InputUnit == "V");
}

TEST_CASE("A real root has a frequency but no quality factor or damping", "[pole_zero]") {
    const LowPass filter;
    const Core::PoleZero result =
        Core::BuildPoleZero(filter.Circuit, {.OutputNode = 2, .InputSource = "Vin1"}, {{-1e4, 0.0}}, {});
    REQUIRE(result.Roots.size() == 1);
    CHECK_THAT(result.Roots.front().NaturalFrequencyHertz, Catch::Matchers::WithinRel(1e4 / TWO_PI, 1e-12));
    CHECK_FALSE(result.Roots.front().QualityFactor.has_value());
    CHECK_FALSE(result.Roots.front().DampingRatio.has_value());
    CHECK(result.Roots.front().Imaginary == 0.0);
}

TEST_CASE("Rounding noise in the imaginary part of a real root is dropped", "[pole_zero]") {
    const LowPass filter;
    const Core::ComplexRoots poles = {{-1e4, 1e-6}, {-2e4, -1e-6}};
    const Core::PoleZero result =
        Core::BuildPoleZero(filter.Circuit, {.OutputNode = 2, .InputSource = "Vin1"}, poles, {});
    REQUIRE(result.Roots.size() == 2);
    CHECK(result.Roots[0].Imaginary == 0.0);
    CHECK(result.Roots[1].Imaginary == 0.0);
}

TEST_CASE("A pair on the imaginary axis has no finite quality factor", "[pole_zero]") {
    const LowPass filter;
    const Core::PoleZero result =
        Core::BuildPoleZero(filter.Circuit, {.OutputNode = 2, .InputSource = "Vin1"}, {{0.0, 1e3}, {0.0, -1e3}}, {});
    REQUIRE(result.Roots.size() == 1);
    CHECK_FALSE(result.Roots.front().QualityFactor.has_value());
    REQUIRE(result.Roots.front().DampingRatio.has_value());
    CHECK(*result.Roots.front().DampingRatio == 0.0);
}

TEST_CASE("Poles and zeros are listed by growing natural frequency, poles first", "[pole_zero]") {
    const LowPass filter;
    const Core::PoleZero result = Core::BuildPoleZero(filter.Circuit, {.OutputNode = 2, .InputSource = "Vin1"},
                                                      {{-3e3, 0.0}, {-1e2, 0.0}}, {{-7e5, 0.0}, {-5e4, 0.0}});
    REQUIRE(result.Roots.size() == 4);
    CHECK(result.Roots[0].Real == -1e2);
    CHECK(result.Roots[1].Real == -3e3);
    CHECK(result.Roots[2].Real == -5e4);
    CHECK_FALSE(result.Roots[2].IsPole);
    CHECK(result.Roots[3].Real == -7e5);
}

TEST_CASE("A pole and a zero at the same place cancel and are counted", "[pole_zero]") {
    const LowPass filter;
    // The zero at -1.0004e4 is within a thousandth of the pole, the one at -2e4 is not
    const Core::PoleZero result = Core::BuildPoleZero(filter.Circuit, {.OutputNode = 2, .InputSource = "Vin1"},
                                                      {{-1e4, 0.0}, {-3e3, 0.0}}, {{-1.0004e4, 0.0}, {-2e4, 0.0}});
    CHECK(result.CancelledPairs == 1);
    CHECK(CountRoots(result, true) == 1);
    CHECK(FindRoot(result, true).Real == -3e3);
    CHECK(CountRoots(result, false) == 1);
    CHECK(FindRoot(result, false).Real == -2e4);
}

TEST_CASE("A cancelling conjugate pair counts once, and roots at the origin cancel", "[pole_zero]") {
    const LowPass filter;
    const Core::PoleZero result = Core::BuildPoleZero(filter.Circuit, {.OutputNode = 2, .InputSource = "Vin1"},
                                                      {{-5e3, 2e4}, {-5e3, -2e4}, {0.0, 0.0}},
                                                      {{-5e3, 2e4}, {-5e3, -2e4}, {0.0, 0.0}, {-1e5, 0.0}});
    CHECK(result.CancelledPairs == 2);
    CHECK(CountRoots(result, true) == 0);
    REQUIRE(CountRoots(result, false) == 1);
    CHECK(FindRoot(result, false).Real == -1e5);
}

TEST_CASE("A root in the right half-plane is flagged", "[pole_zero]") {
    const LowPass filter;
    const Core::PoleZero result =
        Core::BuildPoleZero(filter.Circuit, {.OutputNode = 2, .InputSource = "Vin1"}, {{4e3, 0.0}, {-3e3, 0.0}}, {});
    REQUIRE(result.Roots.size() == 2);
    CHECK_FALSE(result.Roots[0].IsRightHalfPlane);
    CHECK(result.Roots[1].IsRightHalfPlane);
}

TEST_CASE("The unit of the input follows the kind of source", "[pole_zero]") {
    Core::CurrentSource source;
    source.SetName("I1");
    Core::Resistor load;
    load.SetName("R1");
    Core::Circuit circuit;
    circuit.Add(source, {0, 1});
    circuit.Add(load, {1, 0});
    CHECK(Core::BuildPoleZero(circuit, {.OutputNode = 1, .InputSource = "I1"}, {}, {}).InputUnit == "A");
}

TEST_CASE("The input port is the terminals of the source, and a current source is told apart", "[pole_zero]") {
    Core::VoltageSource voltage;
    voltage.SetName("V1");
    Core::CurrentSource current;
    current.SetName("I1");
    Core::VCC rail;
    rail.SetName("V2");
    Core::Resistor load;
    load.SetName("R1");
    Core::Circuit circuit;
    circuit.Add(voltage, {3, 4});
    circuit.Add(current, {5, 6});
    circuit.Add(rail, {7});
    circuit.Add(load, {3, 5});

    const std::optional<Core::PoleZeroInput> from_voltage = Core::FindPoleZeroInput(circuit, "V1");
    REQUIRE(from_voltage.has_value());
    CHECK(from_voltage->PositiveNode == 3);
    CHECK(from_voltage->NegativeNode == 4);
    CHECK_FALSE(from_voltage->IsCurrent);

    const std::optional<Core::PoleZeroInput> from_current = Core::FindPoleZeroInput(circuit, "I1");
    REQUIRE(from_current.has_value());
    CHECK(from_current->PositiveNode == 5);
    CHECK(from_current->NegativeNode == 6);
    CHECK(from_current->IsCurrent);

    // A supply rail has one terminal, the other being ground
    const std::optional<Core::PoleZeroInput> from_rail = Core::FindPoleZeroInput(circuit, "V2");
    REQUIRE(from_rail.has_value());
    CHECK(from_rail->PositiveNode == 7);
    CHECK(from_rail->NegativeNode == 0);

    CHECK_FALSE(Core::FindPoleZeroInput(circuit, "R1").has_value());
    CHECK_FALSE(Core::FindPoleZeroInput(circuit, "nothing").has_value());
}

TEST_CASE("The lines ngspice prints when it refuses are explained in plain words", "[pole_zero]") {
    const auto explain = [](const std::string &line) {
        return Core::DescribePoleZeroFailure({{line, true}, {"run simulation(s) aborted", true}});
    };
    const std::optional<std::string> shorted =
        explain("doAnalyses: The input signal is shorted on the way to the output");
    REQUIRE(shorted.has_value());
    CHECK(shorted->contains("capacitor or an inductor"));
    REQUIRE(explain("doAnalyses: Transfer function is unity").has_value());
    CHECK(explain("doAnalyses: Transfer function is unity")->contains("output node"));
    REQUIRE(explain("doAnalyses: Output is shorted").has_value());
    REQUIRE(explain("doAnalyses: Input is shorted").has_value());
    CHECK_FALSE(explain("something else entirely").has_value());
    CHECK_FALSE(Core::DescribePoleZeroFailure({}).has_value());
}

TEST_CASE("The warning about a solver that gave up is found in what ngspice printed", "[pole_zero]") {
    CHECK_FALSE(Core::FindPoleZeroWarning({{"Doing analysis at TEMP = 27.000000", false}}).has_value());
    const std::optional<std::string> warning =
        Core::FindPoleZeroWarning({{"Warning: Pole-zero iteration limit reached; giving up after 222 trials", true}});
    REQUIRE(warning.has_value());
    CHECK(warning->contains("may be missing"));
}

TEST_CASE("The pole-zero analysis checks its output node and its input source", "[pole_zero]") {
    const LowPass filter;
    CHECK_FALSE(Core::CheckPoleZeroSettings(filter.Circuit, {.OutputNode = 2, .InputSource = "Vin1"}).has_value());
    CHECK(Core::CheckPoleZeroSettings(filter.Circuit, {.OutputNode = 0, .InputSource = "Vin1"}).has_value());
    CHECK(Core::CheckPoleZeroSettings(filter.Circuit, {.OutputNode = 42, .InputSource = "Vin1"}).has_value());
    CHECK(Core::CheckPoleZeroSettings(filter.Circuit, {.OutputNode = 2, .InputSource = ""}).has_value());
    // A resistor is no source
    CHECK(Core::CheckPoleZeroSettings(filter.Circuit, {.OutputNode = 2, .InputSource = "R1"}).has_value());
    CHECK_FALSE(Core::RunPoleZero(filter.Circuit, {.OutputNode = 0, .InputSource = "Vin1"}).Result.has_value());
}

TEST_CASE("ngspice finds no poles through logic parts, and the analysis is refused before the run",
          "[pole_zero][simulator]") {
    Core::SetBackend(nullptr);
    const std::optional<std::string> reason =
        Core::FindUnsupported(Core::GetBackend(), {Core::ComponentType::AND}, Core::AnalysisKind::POLE_ZERO);
    REQUIRE(reason.has_value());
    CHECK(reason->contains("digital"));
    CHECK_FALSE(Core::FindUnsupported(Core::GetBackend(), {Core::ComponentType::RESISTOR, Core::ComponentType::NPN},
                                      Core::AnalysisKind::POLE_ZERO)
                    .has_value());
    CHECK(Core::GetAnalysisKindName(Core::AnalysisKind::POLE_ZERO) == "pole-zero");
}

TEST_CASE("The pole of an RC low-pass is at minus one over RC, with no zero", "[pole_zero][simulator]") {
    const LowPass filter;
    const Core::PoleZeroRun run = Core::RunPoleZero(filter.Circuit, {.OutputNode = 2, .InputSource = "Vin1"});
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    REQUIRE(CountRoots(*run.Result, true) == 1);
    CHECK(CountRoots(*run.Result, false) == 0);
    const Core::PoleZeroRoot &pole = FindRoot(*run.Result, true);
    CHECK_THAT(pole.Real, Catch::Matchers::WithinRel(-1.0 / (1e3 * 100e-9), 1e-6));
    CHECK_THAT(pole.NaturalFrequencyHertz, Catch::Matchers::WithinRel(1.0 / (TWO_PI * 1e3 * 100e-9), 1e-6));
    CHECK(run.Result->InputSource == "Vin1");
    CHECK(run.Result->InputUnit == "V");
    CHECK(run.Result->OutputNode == 2);
}

TEST_CASE("The poles of a series RLC across its resistor match the closed form, with a zero at the origin",
          "[pole_zero][simulator]") {
    const SeriesRLC filter;
    const Core::PoleZeroRun run = Core::RunPoleZero(filter.Circuit, {.OutputNode = 3, .InputSource = "Vin1"});
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    // s = -R/2L +/- j sqrt(1/LC - (R/2L)^2) and a zero at the origin from the capacitor blocking DC
    const double damping = 100.0 / (2.0 * 10e-3);
    const double resonance = 1.0 / std::sqrt(10e-3 * 100e-9);
    REQUIRE(CountRoots(*run.Result, true) == 1);
    const Core::PoleZeroRoot &pole = FindRoot(*run.Result, true);
    CHECK_THAT(pole.Real, Catch::Matchers::WithinRel(-damping, 1e-4));
    CHECK_THAT(pole.Imaginary, Catch::Matchers::WithinRel(std::sqrt(resonance * resonance - damping * damping), 1e-4));
    REQUIRE(pole.QualityFactor.has_value());
    CHECK_THAT(*pole.QualityFactor, Catch::Matchers::WithinRel(resonance / (2.0 * damping), 1e-4));
    REQUIRE(CountRoots(*run.Result, false) == 1);
    CHECK_THAT(FindRoot(*run.Result, false).Real, Catch::Matchers::WithinAbs(0.0, 1e-3));
}

TEST_CASE("A current source input sees its own port open, which a voltage source would short",
          "[pole_zero][simulator]") {
    // Source, 1k and 10k to ground, and 1u: the current source is open, so the pole is at 1 / (C x R2), 100 rad/s,
    // and a voltage source on the same node would give 1 / (C x (R1 || R2)) instead
    Core::CurrentSource source;
    source.SetName("I1");
    Core::Resistor series;
    series.SetName("R1");
    series.SetValue(1e3);
    Core::Resistor shunt;
    shunt.SetName("R2");
    shunt.SetValue(10e3);
    Core::Capacitor capacitor;
    capacitor.SetName("C1");
    capacitor.SetValue(1e-6);
    const Core::Ground reference;
    Core::Circuit circuit;
    circuit.Add(source, {0, 1});
    circuit.Add(series, {1, 2});
    circuit.Add(shunt, {2, 0});
    circuit.Add(capacitor, {2, 0});
    circuit.Add(reference, {0});

    const Core::PoleZeroRun run = Core::RunPoleZero(circuit, {.OutputNode = 2, .InputSource = "I1"});
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    REQUIRE(CountRoots(*run.Result, true) == 1);
    CHECK_THAT(FindRoot(*run.Result, true).Real, Catch::Matchers::WithinRel(-100.0, 1e-4));
    CHECK(run.Result->InputUnit == "A");
}

TEST_CASE("A circuit with no capacitor or inductor has no poles, and the reason is said", "[pole_zero][simulator]") {
    Core::VoltageSource source;
    source.SetName("Vin1");
    Core::Resistor top;
    top.SetName("R1");
    top.SetValue(1e3);
    Core::Resistor bottom;
    bottom.SetName("R2");
    bottom.SetValue(1e3);
    const Core::Ground reference;
    Core::Circuit circuit;
    circuit.Add(source, {1, 0});
    circuit.Add(top, {1, 2});
    circuit.Add(bottom, {2, 0});
    circuit.Add(reference, {0});

    const Core::PoleZeroRun run = Core::RunPoleZero(circuit, {.OutputNode = 2, .InputSource = "Vin1"});
    REQUIRE_FALSE(run.Result.has_value());
    CHECK(run.Result.error().contains("no capacitor or inductor"));
}

TEST_CASE("An ideal op-amp as a non-inverting amplifier has its pole at the gain-bandwidth product over the gain",
          "[pole_zero][simulator]") {
    // The operating point is at 0 V in, since 5 V would saturate the amplifier, which changes its poles
    Core::VoltageSource source;
    source.SetName("Vin1");
    source.SetValue(0.0);
    Core::OpAmp amplifier;
    amplifier.SetName("U1");
    amplifier.SetIdealBandwidth(1e6);
    Core::VCC positive;
    positive.SetName("V1");
    positive.SetValue(15.0);
    Core::VCC negative;
    negative.SetName("V2");
    negative.SetValue(-15.0);
    Core::Resistor ground_leg;
    ground_leg.SetName("R1");
    ground_leg.SetValue(1e3);
    Core::Resistor feedback;
    feedback.SetName("R2");
    feedback.SetValue(10e3);
    const Core::Ground reference;
    Core::Circuit circuit;
    circuit.Add(source, {1, 0});
    circuit.Add(amplifier, {3, 1, 2, 4, 5});
    circuit.Add(positive, {4});
    circuit.Add(negative, {5});
    circuit.Add(ground_leg, {2, 0});
    circuit.Add(feedback, {2, 3});
    circuit.Add(reference, {0});

    const Core::PoleZeroRun run = Core::RunPoleZero(circuit, {.OutputNode = 3, .InputSource = "Vin1"});
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    // The closed-loop gain is 11, so the pole sits at 2 pi x 1 MHz / 11
    REQUIRE(CountRoots(*run.Result, true) == 1);
    CHECK_THAT(FindRoot(*run.Result, true).Real, Catch::Matchers::WithinRel(-TWO_PI * 1e6 / 11.0, 1e-3));
}
