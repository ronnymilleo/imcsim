/**
 * @file    pole_zero_robustness_tests.cpp
 * @brief   Tests that the Nodalys pole-zero analysis finds every root of the circuits where ngspice fails or drops
 *          some, checked against the AC sweep of the same engine and against roots worked out by hand, and that it
 *          says why when a circuit has none to show.
 */

#include "analyses/pz/pole_zero_oracle.h"
#include "analysis/analysis_inputs.h"
#include "backends/nodalys/compiler.h"
#include "backends/nodalys/nodalys_backend.h"
#include "circuit/components/capacitor.h"
#include "circuit/components/ground.h"
#include "circuit/components/logic_part.h"
#include "circuit/components/op_amp.h"
#include "circuit/components/resistor.h"
#include "circuit/components/source.h"
#include "circuit/components/voltage_source.h"
#include "schematic/examples.h"
#include "schematic/schematic.h"
#include "simulation/analysis_sources.h"
#include "simulation/simulation_backend.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <complex>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Catch::Matchers::WithinRel;

constexpr double TWO_PI = 2.0 * std::numbers::pi;
/// Open-loop DC gain and the share of it that the ideal op-amp's gain-bandwidth product is set against.
constexpr double IDEAL_OP_AMP_GAIN = 1e5;

/**
 * @struct  LoadedExample
 * @brief   An example with its circuit and the pole-zero request the tests make of it.
 */
struct LoadedExample {
    /// The circuit refers to the parts of the schematic, so the schematic lives as long as it does.
    std::unique_ptr<GUI::Schematic> Schematic;
    Core::Circuit Circuit;
    Nodalys::PoleZeroConfig Config;
    bool HasLogic = false;
    /// Whether the example has a Vin or Iin source to drive the analysis.
    bool HasInput = false;
};

bool IsInputName(const std::string_view name) {
    return name.starts_with("Vin") || name.starts_with("Iin");
}

/**
 * @brief   Loads an example, makes its Vin source the only AC one and picks the output the editor would.
 * @details The oracle sweeps with every source that has an AC magnitude, so the input is the only one that keeps it.
 */
LoadedExample LoadExample(const std::string_view file_name, const std::optional<double> ideal_bandwidth) {
    const auto examples = GUI::GetExamples();
    const auto found = std::ranges::find(examples, file_name, &GUI::Example::FileName);
    REQUIRE(found != examples.end());
    auto loaded = GUI::LoadExample(*found);
    REQUIRE(loaded.has_value());
    LoadedExample result;
    for (auto &element : loaded->Elements) {
        Core::Component &part = element->GetComponent();
        result.HasLogic = result.HasLogic || Core::IsLogicPart(part.GetType());
        if (ideal_bandwidth && part.GetType() == Core::ComponentType::OP_AMP) {
            static_cast<Core::OpAmp &>(part).SetIdealBandwidth(*ideal_bandwidth);
        }
        if (auto *source = dynamic_cast<Core::Source *>(&part)) {
            Core::ACParameters ac = source->GetAC();
            ac.Magnitude = IsInputName(source->GetName()) ? 1.0 : 0.0;
            if (IsInputName(source->GetName())) {
                source->SetSourceType(Core::Source::SourceType::AC);
            }
            source->SetAC(ac);
        }
    }
    result.Schematic = std::make_unique<GUI::Schematic>();
    result.Schematic->Replace(std::move(loaded->Elements),
                              std::move(loaded->Wires),
                              std::move(loaded->Settings),
                              std::move(loaded->Subcircuits),
                              std::move(loaded->Models));
    result.Schematic->RestoreMeasurements(loaded->Measurements);
    result.Circuit = result.Schematic->GetCircuit();
    const auto output = GUI::ResolveOutputNode(std::nullopt, result.Circuit, *result.Schematic);
    if (!output) {
        return result;
    }
    const std::vector<std::string> sources = Core::GetTransferFunctionInputSources(result.Circuit);
    const auto input = std::ranges::find_if(sources, IsInputName);
    result.HasInput = input != sources.end();
    if (result.HasInput) {
        result.Config = {.OutputNode = *output, .InputSource = *input};
    }
    return result;
}

Testing::PoleZeroReport CheckExample(const LoadedExample &example, const Testing::OracleBand &band) {
    const auto compiled = Core::CompileCircuit(example.Circuit);
    REQUIRE(compiled.has_value());
    return Testing::CheckPoleZero(*compiled, example.Config, band);
}

/// The roots of a polynomial given from the highest power down, by Durand-Kerner, which shares nothing with QZ.
std::vector<std::complex<double>> PolynomialRoots(const std::vector<double> &coefficients) {
    const std::size_t degree = coefficients.size() - 1;
    // Working in x = s / scale, with scale the geometric mean of the roots, keeps the iteration near 1
    const double scale =
        std::pow(std::abs(coefficients.back() / coefficients.front()), 1.0 / static_cast<double>(degree));
    std::vector<double> monic(coefficients.size());
    for (std::size_t term = 0; term <= degree; ++term) {
        monic[term] = coefficients[term] / coefficients.front() / std::pow(scale, static_cast<double>(term));
    }
    std::vector<std::complex<double>> roots(degree);
    for (std::size_t index = 0; index < degree; ++index) {
        roots[index] = std::polar(1.0, 0.4 + TWO_PI * static_cast<double>(index) / static_cast<double>(degree));
    }
    for (int iteration = 0; iteration < 2000; ++iteration) {
        for (std::size_t index = 0; index < degree; ++index) {
            std::complex<double> value{0.0, 0.0};
            for (const double coefficient : monic) {
                value = value * roots[index] + coefficient;
            }
            std::complex<double> denominator{monic.front(), 0.0};
            for (std::size_t other = 0; other < degree; ++other) {
                if (other != index) {
                    denominator *= roots[index] - roots[other];
                }
            }
            roots[index] -= value / denominator;
        }
    }
    for (std::complex<double> &root : roots) {
        root *= scale;
    }
    return roots;
}

} // namespace

TEST_CASE("The Sallen-Key filter keeps all three poles at every gain-bandwidth product ngspice fails or passes at",
          "[pz][robust]") {
    // The unity-gain Sallen-Key of the example: R1 = R2 = 10k, C1 = 22 nF on the feedback, C2 = 10 nF to ground. With
    // the op-amp as a follower of gain a(s) = g / (s + p1), where g = 2 pi GBW and p1 = g (1 + 1 / A0) / 1, nodal
    // analysis gives the denominator Q(s) (s + p1) - s C1 g G2, with
    // Q(s) = G1 G2 + s ((G1 + G2) C2 + C1 G2) + s^2 C1 C2
    constexpr double RESISTANCE = 10e3;
    constexpr double FEEDBACK_CAPACITANCE = 22e-9;
    constexpr double GROUND_CAPACITANCE = 10e-9;
    constexpr double CONDUCTANCE = 1.0 / RESISTANCE;
    // ngspice returns nothing at 1e4, 5e5 and 1e6 Hz (its default) and works at 3e5 and 2e6
    for (const double gbw : {1e4, 3e5, 5e5, 1e6, 2e6}) {
        INFO("gain-bandwidth " << gbw << " Hz");
        const LoadedExample example = LoadExample("sallen_key_low_pass.imcsim", gbw);
        const Testing::PoleZeroReport report =
            CheckExample(example, {.StartFrequencyHertz = 1.0, .StopFrequencyHertz = 1e9});
        INFO(Testing::Describe(report));
        REQUIRE(report.Poles.size() == 3);
        CHECK(report.Zeros.empty());

        const double g = TWO_PI * gbw;
        const double p1 = g * (1.0 + 1.0 / IDEAL_OP_AMP_GAIN);
        const double q2 = FEEDBACK_CAPACITANCE * GROUND_CAPACITANCE;
        const double q1 = 2.0 * CONDUCTANCE * GROUND_CAPACITANCE + FEEDBACK_CAPACITANCE * CONDUCTANCE;
        const double q0 = CONDUCTANCE * CONDUCTANCE;
        const std::vector<double> denominator{q2,
                                              q1 + q2 * p1,
                                              q0 + q1 * p1 - FEEDBACK_CAPACITANCE * g * CONDUCTANCE,
                                              q0 * p1};
        for (const std::complex<double> &expected : PolynomialRoots(denominator)) {
            CHECK(Testing::HasRootNear(report.Poles, expected, 1e-4));
        }
        CHECK_FALSE(Testing::HasRightHalfPlaneRoot(report.Poles, 0.0));
        CHECK(Testing::IsMatch(report));
    }
}

TEST_CASE("The transformer power supply has no right-half-plane root and all seven of its poles", "[pz][robust]") {
    // ngspice returns right-half-plane roots here, with no message. The circuit is passive and linearized with its
    // diodes off, so none can exist: the roots are the two coupled windings, the filter capacitor and the junctions
    const LoadedExample example = LoadExample("transformer_power_supply.imcsim", std::nullopt);
    const Testing::PoleZeroReport report =
        CheckExample(example, {.StartFrequencyHertz = 0.1, .StopFrequencyHertz = 1e9});
    INFO(Testing::Describe(report));
    CHECK(report.Poles.size() == 7);
    CHECK_FALSE(Testing::HasRightHalfPlaneRoot(report.Poles, 0.0));
    CHECK_FALSE(Testing::HasRightHalfPlaneRoot(report.Zeros, 0.0));
    // The roots near 1 to 1e4 rad/s sit a million times closer to the origin than the 3e12 rad/s junction poles. QZ
    // alone put the zero beside the pole at -15005.77 anywhere from -14868 to -15114 depending on the rounding of the
    // platform (a macOS arm64 build did); refined to its own size it is at -15005.746 to 1e-6 on every one
    CHECK(Testing::HasRootNear(report.Zeros, {-15005.7457, 0.0}, 1e-5));
    CHECK(Testing::HasRootNear(report.Poles, {-15005.7709, 0.0}, 1e-5));
    CHECK(Testing::HasRootNear(report.Poles, {-9.67149943, 0.0}, 1e-6));
    CHECK(Testing::HasRootNear(report.Poles, {-1.0, 0.0}, 1e-6));
    CHECK(std::ranges::any_of(report.Zeros, [](const auto &zero) { return zero == std::complex<double>(0.0, 0.0); }));
    CHECK(Testing::IsMatch(report));
}

TEST_CASE("Every example whose input reaches its output has roots that rebuild its AC response", "[pz][robust]") {
    // The examples whose input is a Vin source; the rest have a supply for input, which no AC sweep can drive, or
    // distributed parts with no rational pole-zero form
    const Core::NodalysBackend nodalys;
    for (const GUI::Example &entry : GUI::GetExamples()) {
        const LoadedExample example = LoadExample(entry.FileName, std::nullopt);
        if (!example.HasInput || example.HasLogic
            || Core::FindUnsupported(nodalys, example.Circuit, Core::AnalysisKind::POLE_ZERO)) {
            continue;
        }
        const auto compiled = Core::CompileCircuit(example.Circuit);
        if (!compiled) {
            continue;
        }
        INFO(entry.FileName);
        const Testing::PoleZeroReport report =
            Testing::CheckPoleZero(*compiled, example.Config, {.StartFrequencyHertz = 0.1, .StopFrequencyHertz = 1e9});
        INFO(Testing::Describe(report));
        const bool is_undriven = report.Error.contains("does not respond");
        if (is_undriven) {
            // Only where every diode or switch of the path is off at the operating point
            CHECK((entry.FileName == "full_bridge_rectifier.imcsim" || entry.FileName == "buck_converter.imcsim"));
            continue;
        }
        CHECK(report.Error.empty());
        CHECK(report.Warnings.empty());
        CHECK(report.MaxMagnitudeErrorDecibels <= 1e-3);
        CHECK(report.MaxPhaseErrorDegrees <= 1e-2);
    }
}

TEST_CASE("The op-amp integrator has its pole near the origin and the op-amp pole, and a block instance keeps its "
          "roots",
          "[pz][robust]") {
    SECTION("Op-amp integrator") {
        const LoadedExample example = LoadExample("op_amp_integrator.imcsim", std::nullopt);
        const Testing::PoleZeroReport report =
            CheckExample(example, {.StartFrequencyHertz = 0.1, .StopFrequencyHertz = 1e9});
        INFO(Testing::Describe(report));
        REQUIRE(report.Poles.size() == 2);
        // Finite open-loop gain moves the integrator's pole from the origin to about -100 rad/s
        const auto lowest = std::ranges::min_element(report.Poles, {}, [](const auto &pole) { return std::abs(pole); });
        CHECK(std::abs(lowest->real()) < 200.0);
        CHECK(std::abs(lowest->real()) > 10.0);
        CHECK(Testing::IsMatch(report));
    }
    SECTION("Blocks of the example rc_blocks") {
        const LoadedExample example = LoadExample("rc_blocks.imcsim", std::nullopt);
        const Testing::PoleZeroReport report =
            CheckExample(example, {.StartFrequencyHertz = 1.0, .StopFrequencyHertz = 1e7});
        INFO(Testing::Describe(report));
        CHECK(Testing::HasRootNear(report.Poles, {-10000.0, 0.0}, 1e-4));
        CHECK(Testing::IsMatch(report));
    }
}

TEST_CASE("The Nodalys backend says in plain words why a circuit has no poles or zeros", "[pz][messages]") {
    Core::NodalysBackend backend;
    SECTION("A circuit with no capacitor or inductor") {
        Core::VoltageSource source;
        source.SetName("Vin1");
        Core::Resistor top;
        top.SetName("R1");
        top.SetValue(1e3);
        Core::Resistor bottom;
        bottom.SetName("R2");
        bottom.SetValue(3e3);
        Core::Ground ground;
        Core::Circuit circuit;
        circuit.Add(source, {1, 0});
        circuit.Add(top, {1, 2});
        circuit.Add(bottom, {2, 0});
        circuit.Add(ground, {0});
        const Core::PoleZeroRun run = backend.RunPoleZero(circuit, {.OutputNode = 2, .InputSource = "Vin1"});
        REQUIRE_FALSE(run.Result.has_value());
        CHECK(run.Result.error().contains("no poles or zeros"));
        CHECK(run.Result.error().contains("no capacitor or inductor"));
        CHECK_FALSE(run.Result.error().contains("ngspice"));
    }
    SECTION("An output the source does not reach") {
        const LoadedExample example = LoadExample("full_bridge_rectifier.imcsim", std::nullopt);
        const Core::PoleZeroRun run =
            backend.RunPoleZero(example.Circuit,
                                {.OutputNode = example.Config.OutputNode, .InputSource = example.Config.InputSource});
        REQUIRE_FALSE(run.Result.has_value());
        CHECK(run.Result.error().contains("does not respond"));
        CHECK_FALSE(run.Result.error().contains("ngspice"));
    }
    SECTION("An output node the source holds") {
        const LoadedExample example = LoadExample("rc_low_pass_filter.imcsim", std::nullopt);
        const Core::PoleZeroRun run =
            backend.RunPoleZero(example.Circuit, {.OutputNode = 1, .InputSource = example.Config.InputSource});
        REQUIRE_FALSE(run.Result.has_value());
        CHECK(run.Result.error().contains("constant gain"));
        CHECK_FALSE(run.Result.error().contains("ngspice"));
    }
}
