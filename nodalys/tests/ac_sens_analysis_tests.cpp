/**
 * @file    ac_sens_analysis_tests.cpp
 * @brief   Tests of the Nodalys AC sensitivity analysis: closed forms of linear circuits, the match with the DC
 *          sensitivity at low frequency, and finite differences of the AC sweep on circuits with nonlinear devices and
 *          controlled sources, where the operating point moves with every value.
 */

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <complex>
#include <format>
#include <functional>
#include <map>
#include <nodalys/nodalys.h>
#include <numbers>
#include <string>

namespace {

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using Complex = std::complex<double>;

constexpr double TWO_PI = 2.0 * std::numbers::pi;
constexpr double DECIBELS_PER_NEPER = 20.0 / std::numbers::ln10;
constexpr double DEGREES_PER_RADIAN = 180.0 / std::numbers::pi;

/// Element values by name, so a circuit can be built again with one of them changed.
using Values = std::map<std::string, double>;
using CircuitMaker = std::function<Nodalys::Circuit(const Values &)>;

/// Options that leave the operating point converged far below the size of the finite-difference steps.
Nodalys::ConvergenceOptions TightOptions() {
    return {.RelTol = 1e-12, .VoltageAbsTol = 1e-13, .CurrentAbsTol = 1e-18, .MaxIterations = 500};
}

Nodalys::Circuit Finish(Nodalys::CircuitBuilder &builder) {
    auto circuit = builder.Build();
    REQUIRE(circuit.has_value());
    return std::move(*circuit);
}

/// A single frequency, which is how a sweep of one point is asked.
Nodalys::AcSensitivityConfig SingleFrequency(const int output, const std::string &source, const double frequency) {
    return {.OutputPositiveNode = output,
            .OutputNegativeNode = 0,
            .InputSource = source,
            .StartFrequency = frequency,
            .StopFrequency = frequency,
            .PointsPerDecade = 1};
}

const Nodalys::AcElementSensitivity &FindElement(const Nodalys::AcSensitivityResult &result, const std::string &name) {
    const auto found =
        std::ranges::find_if(result.Elements, [&name](const auto &element) { return element.Name == name; });
    REQUIRE(found != result.Elements.end());
    return *found;
}

/// The response of a node to the circuit's only AC source: magnitude in decibels and phase in degrees.
Complex ReadResponse(const Nodalys::Circuit &circuit, const int node, const double frequency) {
    const Nodalys::AcSweepConfig config{.StartFrequency = frequency, .StopFrequency = frequency, .PointsPerDecade = 1};
    const auto sweep = Nodalys::ComputeACSweep(circuit, config, TightOptions());
    REQUIRE(sweep.has_value());
    const auto index = static_cast<std::size_t>(node);
    return std::polar(std::pow(10.0, sweep->NodeMagnitudesDecibels[index][0] / 20.0),
                      sweep->NodePhasesDegrees[index][0] / DEGREES_PER_RADIAN);
}

/**
 * @brief   Checks the two figures of every element against central differences of the AC sweep.
 * @details An element the maker does not know by name, such as the parts inside an op-amp, is left out; the count of
 *          the ones checked must be at least the minimum, so a renamed element cannot make the test pass for nothing.
 */
void CheckAgainstFiniteDifferences(const CircuitMaker &make,
                                   const Values &values,
                                   const int output,
                                   const std::string &source,
                                   const double frequency,
                                   const std::size_t minimum_checked) {
    constexpr double RELATIVE_STEP = 1e-4;
    constexpr double PERCENT = 0.01;
    const auto result =
        Nodalys::ComputeAcSensitivity(make(values), SingleFrequency(output, source, frequency), TightOptions());
    REQUIRE(result.has_value());
    std::size_t checked = 0;
    for (const auto &element : result->Elements) {
        if (!values.contains(element.Name)) {
            continue;
        }
        ++checked;
        Values above = values;
        Values below = values;
        above[element.Name] *= 1.0 + RELATIVE_STEP;
        below[element.Name] *= 1.0 - RELATIVE_STEP;
        const Complex change = std::log(ReadResponse(make(above), output, frequency)) -
                               std::log(ReadResponse(make(below), output, frequency));
        // log of the response is ln|H| + j arg H, so its change per relative step is both figures at once
        const double expected_decibels = DECIBELS_PER_NEPER * change.real() / (2.0 * RELATIVE_STEP) * PERCENT;
        const double expected_degrees = DEGREES_PER_RADIAN * change.imag() / (2.0 * RELATIVE_STEP) * PERCENT;
        INFO(std::format("element {}", element.Name));
        REQUIRE(element.MagnitudeDecibelsPerPercent[0].has_value());
        CHECK_THAT(*element.MagnitudeDecibelsPerPercent[0],
                   WithinAbs(expected_decibels, 2e-6 + 2e-4 * std::abs(expected_decibels)));
        CHECK_THAT(*element.PhaseDegreesPerPercent[0],
                   WithinAbs(expected_degrees, 2e-5 + 2e-4 * std::abs(expected_degrees)));
    }
    CHECK(checked >= minimum_checked);
}

} // namespace

TEST_CASE("AC sensitivity of an RC low-pass at its corner is about a twentieth of a decibel per percent",
          "[nodalys][ac_sensitivity]") {
    constexpr double RESISTANCE = 1000.0;
    constexpr double CORNER = 1000.0;
    Nodalys::CircuitBuilder builder;
    builder.AddVdc("V1", 1, 0, 0.0, 1.0);
    builder.AddResistor("R1", 1, 2, RESISTANCE);
    builder.AddCapacitor("C1", 2, 0, 1.0 / (TWO_PI * RESISTANCE * CORNER));

    const auto result = Nodalys::ComputeAcSensitivity(Finish(builder), SingleFrequency(2, "V1", CORNER));

    REQUIRE(result.has_value());
    // H = 1 / (1 + jx) at x = 1, so dlnH/dlnR = dlnH/dlnC = -jx / (1 + jx) = -0.5 - 0.5j
    for (const std::string name : {"r1", "c1"}) {
        INFO(name);
        const auto &element = FindElement(*result, name);
        REQUIRE(element.MagnitudeDecibelsPerPercent[0].has_value());
        CHECK_THAT(*element.MagnitudeDecibelsPerPercent[0], WithinAbs(-0.043429, 1e-6));
        CHECK_THAT(*element.PhaseDegreesPerPercent[0], WithinAbs(-0.28648, 1e-5));
    }
    CHECK_THAT(std::abs(result->Output[0]), WithinRel(1.0 / std::numbers::sqrt2, 1e-9));
    // The source value is no part of a linear circuit's response
    CHECK_THAT(std::abs(FindElement(*result, "v1").Derivatives[0]), WithinAbs(0.0, 1e-12));
    CHECK(result->Warnings.empty());
}

TEST_CASE("AC sensitivity of a divider at low frequency equals its DC sensitivity", "[nodalys][ac_sensitivity]") {
    constexpr double SOURCE_VOLTS = 10.0;
    Nodalys::CircuitBuilder builder;
    builder.AddVdc("V1", 1, 0, SOURCE_VOLTS, 1.0);
    builder.AddResistor("R1", 1, 2, 1000.0);
    builder.AddResistor("R2", 2, 0, 2000.0);
    builder.AddCapacitor("C1", 2, 0, 1e-9);
    const Nodalys::Circuit circuit = Finish(builder);

    const auto dc = Nodalys::ComputeSensitivity(circuit, {.OutputPositiveNode = 2, .OutputNegativeNode = 0});
    const auto ac = Nodalys::ComputeAcSensitivity(circuit, SingleFrequency(2, "V1", 1.0));

    REQUIRE(dc.has_value());
    REQUIRE(ac.has_value());
    // The DC analysis is for the DC value of the source and the AC one for 1 V of AC, so the DC figures scale by 1 / 10
    for (const std::string name : {"r1", "r2"}) {
        INFO(name);
        CHECK_THAT(FindElement(*ac, name).Derivatives[0].real(), WithinRel(dc->Vectors.at(name) / SOURCE_VOLTS, 1e-6));
        CHECK_THAT(FindElement(*ac, name).Derivatives[0].imag(), WithinAbs(0.0, 1e-6 * std::abs(dc->Vectors.at(name))));
    }
}

TEST_CASE("AC sensitivity of a series RLC matches the derivatives of its transfer function",
          "[nodalys][ac_sensitivity]") {
    constexpr double RESISTANCE = 50.0;
    constexpr double INDUCTANCE = 1e-3;
    constexpr double CAPACITANCE = 1e-6;
    constexpr double FREQUENCY = 4000.0;
    Nodalys::CircuitBuilder builder;
    builder.AddVdc("V1", 1, 0, 0.0, 1.0);
    builder.AddResistor("R1", 1, 2, RESISTANCE);
    builder.AddInductor("L1", 2, 3, INDUCTANCE);
    builder.AddCapacitor("C1", 3, 0, CAPACITANCE);

    const auto result = Nodalys::ComputeAcSensitivity(Finish(builder), SingleFrequency(3, "V1", FREQUENCY));

    REQUIRE(result.has_value());
    const double omega = TWO_PI * FREQUENCY;
    const Complex denominator(1.0 - omega * omega * INDUCTANCE * CAPACITANCE, omega * RESISTANCE * CAPACITANCE);
    const Complex transfer = 1.0 / denominator;
    CHECK_THAT(std::abs(result->Output[0] - transfer), WithinAbs(0.0, 1e-9));
    // dH/dp = -H^2 d(denominator)/dp
    const Complex by_resistance = -transfer * transfer * Complex(0.0, omega * CAPACITANCE);
    const Complex by_inductance = -transfer * transfer * (-omega * omega * CAPACITANCE);
    const Complex by_capacitance = -transfer * transfer * Complex(-omega * omega * INDUCTANCE, omega * RESISTANCE);
    CHECK_THAT(std::abs(FindElement(*result, "r1").Derivatives[0] - by_resistance), WithinAbs(0.0, 1e-9));
    CHECK_THAT(std::abs(FindElement(*result, "l1").Derivatives[0] - by_inductance), WithinAbs(0.0, 1e-6));
    CHECK_THAT(std::abs(FindElement(*result, "c1").Derivatives[0] - by_capacitance), WithinAbs(0.0, 1e-3));
}

TEST_CASE("AC sensitivity of a biased transistor stage follows the operating point as the AC sweep does",
          "[nodalys][ac_sensitivity]") {
    // Junction capacitances and the transit time make the small-signal matrix depend on the bias, and the supply and
    // the bias resistors move it
    const CircuitMaker make = [](const Values &values) {
        Nodalys::CircuitBuilder builder;
        builder.AddVdc("vcc", 1, 0, values.at("vcc"));
        builder.AddVdc("vin", 5, 0, 0.0, 1.0);
        builder.AddCapacitor("cin", 5, 2, values.at("cin"));
        builder.AddResistor("rb1", 1, 2, values.at("rb1"));
        builder.AddResistor("rb2", 2, 0, values.at("rb2"));
        builder.AddResistor("rc", 1, 3, values.at("rc"));
        builder.AddResistor("re", 4, 0, values.at("re"));
        builder.AddCapacitor("ce", 4, 0, values.at("ce"));
        builder.AddCapacitor("cl", 3, 0, values.at("cl"));
        Nodalys::BjtParameters model;
        model.EarlyVoltage = 100.0;
        model.EmitterCapacitance = 20e-12;
        model.CollectorCapacitance = 5e-12;
        model.TransitTime = 0.4e-9;
        model.BaseResistance = 60.0;
        builder.AddBjt("q1", 3, 2, 4, true, model);
        return Finish(builder);
    };
    const Values values{{"vcc", 12.0},
                        {"cin", 1e-6},
                        {"rb1", 47e3},
                        {"rb2", 10e3},
                        {"rc", 2200.0},
                        {"re", 470.0},
                        {"ce", 10e-6},
                        {"cl", 2e-9}};

    for (const double frequency : {1e3, 1e5, 3e6}) {
        INFO(std::format("frequency {}", frequency));
        CheckAgainstFiniteDifferences(make, values, 3, "vin", frequency, 8);
    }
}

TEST_CASE("AC sensitivity of an ideal op-amp stage with controlled sources follows the AC sweep",
          "[nodalys][ac_sensitivity]") {
    const CircuitMaker make = [](const Values &values) {
        Nodalys::CircuitBuilder builder;
        builder.AddVdc("vin", 1, 0, 0.0, 1.0);
        builder.AddVdc("vcc", 4, 0, values.at("vcc"));
        builder.AddVdc("vee", 5, 0, -values.at("vcc"));
        builder.AddIdealOpAmp("u1", 2, 1, 3, 4, 5, 1e6);
        builder.AddResistor("rf", 2, 3, values.at("rf"));
        builder.AddResistor("rg", 3, 0, values.at("rg"));
        builder.AddVcvs("e1", 6, 0, 2, 0, values.at("e1"));
        builder.AddResistor("rl", 6, 0, 2000.0);
        builder.AddVccs("g1", 7, 0, 6, 0, values.at("g1"));
        builder.AddResistor("r7", 7, 0, values.at("r7"));
        builder.AddCapacitor("c7", 7, 0, values.at("c7"));
        builder.AddResistor("rs", 7, 9, 1000.0);
        builder.AddCurrentProbe("vsense", 9, 0);
        builder.AddCccs("f1", 8, 0, "vsense", values.at("f1"));
        builder.AddResistor("r8", 8, 0, 3000.0);
        builder.AddCcvs("h1", 10, 0, "vsense", values.at("h1"));
        builder.AddResistor("rh", 10, 11, 1000.0);
        builder.AddCapacitor("ch", 11, 0, values.at("ch"));
        builder.AddInductor("lh", 11, 12, values.at("lh"));
        builder.AddResistor("rz", 12, 0, 500.0);
        builder.AddResistor("rs8", 8, 11, 2000.0);
        return Finish(builder);
    };
    const Values values{{"vcc", 15.0},
                        {"rf", 10e3},
                        {"rg", 1e3},
                        {"e1", 0.5},
                        {"g1", 1e-3},
                        {"r7", 5e3},
                        {"c7", 1e-9},
                        {"f1", 2.0},
                        {"h1", 500.0},
                        {"ch", 1e-9},
                        {"lh", 1e-3}};

    for (const double frequency : {1e3, 3e4, 2e5}) {
        INFO(std::format("frequency {}", frequency));
        CheckAgainstFiniteDifferences(make, values, 11, "vin", frequency, 11);
    }
}

TEST_CASE("AC sensitivity gives no value where the output is zero and says so once", "[nodalys][ac_sensitivity]") {
    // A Wien arm against a 2:1 divider is balanced at 1 / (2 pi R C), where the difference of the two nodes is zero
    constexpr double RESISTANCE = 1000.0;
    constexpr double CAPACITANCE = 1e-7;
    const double null_frequency = 1.0 / (TWO_PI * RESISTANCE * CAPACITANCE);
    Nodalys::CircuitBuilder builder;
    builder.AddVdc("V1", 1, 0, 0.0, 1.0);
    builder.AddResistor("R1", 1, 2, RESISTANCE);
    builder.AddCapacitor("C1", 2, 3, CAPACITANCE);
    builder.AddResistor("R2", 3, 0, RESISTANCE);
    builder.AddCapacitor("C2", 3, 0, CAPACITANCE);
    builder.AddResistor("R3", 1, 4, 2.0 * RESISTANCE);
    builder.AddResistor("R4", 4, 0, RESISTANCE);
    // Node 3 is the middle of the first arm and node 4 the one of the divider
    const Nodalys::AcSensitivityConfig bridge{.OutputPositiveNode = 3,
                                              .OutputNegativeNode = 4,
                                              .InputSource = "V1",
                                              .StartFrequency = null_frequency / 10.0,
                                              .StopFrequency = null_frequency,
                                              .PointsPerDecade = 1};

    const auto result = Nodalys::ComputeAcSensitivity(Finish(builder), bridge);

    REQUIRE(result.has_value());
    REQUIRE(result->Frequencies.size() == 2);
    const auto &element = FindElement(*result, "r1");
    CHECK(element.MagnitudeDecibelsPerPercent[0].has_value());
    CHECK_FALSE(element.MagnitudeDecibelsPerPercent[1].has_value());
    CHECK_FALSE(element.PhaseDegreesPerPercent[1].has_value());
    CHECK(result->Warnings.size() == 1);
}

TEST_CASE("AC sensitivity refuses a source that does not drive the output and a source with no AC magnitude",
          "[nodalys][ac_sensitivity]") {
    Nodalys::CircuitBuilder builder;
    builder.AddVdc("V1", 1, 0, 0.0, 1.0);
    builder.AddResistor("R1", 1, 2, 1000.0);
    builder.AddResistor("R2", 2, 0, 1000.0);
    builder.AddResistor("R3", 3, 0, 1000.0);
    builder.AddVdc("V2", 4, 0, 1.0);
    builder.AddResistor("R4", 4, 3, 1000.0);
    const Nodalys::Circuit circuit = Finish(builder);

    // Node 3 only sees V2, whose DC value is no AC input
    const auto undriven = Nodalys::ComputeAcSensitivity(circuit, SingleFrequency(3, "V1", 1e3));
    const auto silent = Nodalys::ComputeAcSensitivity(circuit, SingleFrequency(2, "V2", 1e3));
    const auto missing = Nodalys::ComputeAcSensitivity(circuit, SingleFrequency(2, "V9", 1e3));
    const auto bad_range =
        Nodalys::ComputeAcSensitivity(circuit, {.OutputPositiveNode = 2, .InputSource = "V1", .StartFrequency = 0.0});

    REQUIRE_FALSE(undriven.has_value());
    CHECK(undriven.error().contains("does not drive it"));
    REQUIRE_FALSE(silent.has_value());
    CHECK(silent.error().contains("no AC magnitude"));
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().contains("no source named"));
    REQUIRE_FALSE(bad_range.has_value());
    CHECK(bad_range.error().contains("frequency settings"));
}
