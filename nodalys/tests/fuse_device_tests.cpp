/**
 * @file    fuse_device_tests.cpp
 * @brief   Tests of the fuse of Nodalys: the heat equation, the blow, the latch and the whole fuse of the DC and
 *          small-signal analyses.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <nodalys/nodalys.h>
#include <optional>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

/// A 1 A fuse with a time constant of 150 ms, so the runs that blow it stay short.
constexpr Nodalys::FuseParameters QUICK_FUSE = {
    .RatedCurrent = 1.0, .MeltingI2t = 0.15, .ColdResistance = 0.15, .BlownResistance = 1e9};
constexpr double QUICK_TIME_CONSTANT = 0.15;
/// Heat at which the fuse opens, the same as the latch of the ngspice model.
constexpr double BLOW_HEAT = 1.015;
/// Node of the supply, of the load and of the middle of a load with an inductor.
constexpr int SUPPLY = 1;
constexpr int LOAD = 2;
constexpr int INDUCTOR_END = 3;

/// A supply on node 1 feeding the quick fuse to node 2, where a load goes to ground.
Nodalys::Circuit BuildFuseLoop(const double supply_volts, const double load_ohms) {
    Nodalys::CircuitBuilder builder;
    builder.AddVdc("V1", SUPPLY, 0, supply_volts, 1.0);
    builder.AddFuse("FU1", SUPPLY, LOAD, QUICK_FUSE);
    builder.AddResistor("R1", LOAD, 0, load_ohms);
    auto circuit = builder.Build();
    REQUIRE(circuit.has_value());
    return std::move(*circuit);
}

Nodalys::TransientResult RunTransient(const Nodalys::Circuit &circuit, const double stop_time, const double time_step) {
    const Nodalys::TransientConfig config{.StopTime = stop_time,
                                          .TimeStep = time_step,
                                          .MaxStep = time_step,
                                          .Method = Nodalys::IntegrationMethod::GEAR2};
    auto result = Nodalys::ComputeTransient(circuit, config);
    REQUIRE(result.has_value());
    return std::move(*result);
}

/// Time of the first point where the load has lost all but a thousandth of its voltage, or none when it never does.
std::optional<double> FindOpenTime(const Nodalys::TransientResult &result) {
    const std::vector<double> &load = result.NodeVoltages[LOAD];
    const double open_level = 1e-3 * load.front();
    const auto open = std::ranges::find_if(load, [open_level](const double volts) { return volts < open_level; });
    if (open == load.end()) {
        return std::nullopt;
    }
    return result.Times[static_cast<std::size_t>(open - load.begin())];
}

/// Time a fuse takes to blow at a constant current, from the closed form of the heat equation that starts at zero.
double ExpectedBlowTime(const double current) {
    const double settled_heat = current * current / (QUICK_FUSE.RatedCurrent * QUICK_FUSE.RatedCurrent);
    return QUICK_TIME_CONSTANT * std::log(settled_heat / (settled_heat - BLOW_HEAT));
}

} // namespace

TEST_CASE("A fuse blows at the time the heat equation gives for a constant overcurrent", "[nodalys][fuse]") {
    // 5 V over 2.5 Ohm and the 0.15 Ohm of the fuse: 1.887 A, a heat that settles at 3.56
    const double current = 5.0 / (2.5 + QUICK_FUSE.ColdResistance);
    const Nodalys::TransientResult result = RunTransient(BuildFuseLoop(5.0, 2.5), 0.5, 5e-3);
    const std::optional<double> open_time = FindOpenTime(result);
    REQUIRE(open_time.has_value());
    INFO("open at " << *open_time << " s, closed form " << ExpectedBlowTime(current) << " s");
    // The first open point is the end of the opening, a hundredth of a thousandth of the time constant after the blow
    CHECK(*open_time >= ExpectedBlowTime(current));
    CHECK_THAT(*open_time, WithinAbs(ExpectedBlowTime(current), 1e-4));
    CHECK_THAT(result.NodeVoltages[LOAD].front(), WithinAbs(5.0 * 2.5 / 2.65, 1e-9));
}

TEST_CASE("The blow time follows the current squared: a quarter of the current takes about 16 times the heat",
          "[nodalys][fuse]") {
    for (const double load_ohms : {1.0, 2.5, 4.0}) {
        const double current = 5.0 / (load_ohms + QUICK_FUSE.ColdResistance);
        const std::optional<double> open_time = FindOpenTime(RunTransient(BuildFuseLoop(5.0, load_ohms), 2.0, 5e-3));
        INFO("load " << load_ohms << " Ohm");
        REQUIRE(open_time.has_value());
        CHECK_THAT(*open_time, WithinAbs(ExpectedBlowTime(current), 1e-4));
    }
}

TEST_CASE("A fuse never blows at or below its rated current, however long it carries it", "[nodalys][fuse]") {
    // 0.97 A, then 1 A exactly where the heat settles at 1, and 1.001 A where it settles at 1.002, below the 1.015
    for (const double load_ohms : {5.0 / 0.97 - 0.15, 4.85, 5.0 / 1.001 - 0.15}) {
        const Nodalys::TransientResult result =
            RunTransient(BuildFuseLoop(5.0, load_ohms), 20.0 * QUICK_TIME_CONSTANT, 10e-3);
        INFO("load " << load_ohms << " Ohm");
        CHECK_FALSE(FindOpenTime(result).has_value());
        CHECK_THAT(result.NodeVoltages[LOAD].back(), WithinRel(result.NodeVoltages[LOAD].front(), 1e-6));
    }
}

TEST_CASE("A fuse stays blown, with the blown resistance between its terminals", "[nodalys][fuse]") {
    const Nodalys::TransientResult result = RunTransient(BuildFuseLoop(5.0, 2.5), 0.5, 5e-3);
    const std::optional<double> open_time = FindOpenTime(result);
    REQUIRE(open_time.has_value());
    const std::vector<double> &load = result.NodeVoltages[LOAD];
    for (std::size_t index = 0; index < result.Times.size(); ++index) {
        if (result.Times[index] >= *open_time) {
            CHECK_THAT(load[index], WithinAbs(0.0, 1e-3));
        }
    }
    // 5 V over the blown 1 GOhm
    const auto current = std::ranges::find(result.Currents, "FU1", &Nodalys::ComponentTrace::Name);
    REQUIRE(current != result.Currents.end());
    CHECK_THAT(current->Values.back(), WithinAbs(5e-9, 1e-9));
}

TEST_CASE("A fuse is whole in the operating point, the DC sweep and the AC sweep, far past its rating",
          "[nodalys][fuse]") {
    // 5 V over 0.5 Ohm is 7.7 A, several times what the fuse carries for long
    const Nodalys::Circuit circuit = BuildFuseLoop(5.0, 0.5);
    const auto point = Nodalys::ComputeOperatingPoint(circuit);
    REQUIRE(point.has_value());
    CHECK_THAT(point->NodeVoltages[LOAD], WithinAbs(5.0 * 0.5 / 0.65, 1e-6));

    const auto sweep =
        Nodalys::ComputeDCSweep(circuit, {.SweptSource = "V1", .SweptStart = 0.0, .SweptStop = 20.0, .SweptStep = 5.0});
    REQUIRE(sweep.has_value());
    CHECK_THAT(sweep->Curves[0].NodeVoltages[LOAD].back(), WithinAbs(20.0 * 0.5 / 0.65, 1e-6));

    const auto response =
        Nodalys::ComputeACSweep(circuit, {.StartFrequency = 10.0, .StopFrequency = 1e3, .PointsPerDecade = 2});
    REQUIRE(response.has_value());
    for (const double magnitude : response->NodeMagnitudesDecibels[LOAD]) {
        CHECK_THAT(magnitude, WithinAbs(20.0 * std::log10(0.5 / 0.65), 1e-6));
    }
}

TEST_CASE("A fuse that cuts an inductive load settles at once with Gear-2, without ringing", "[nodalys][fuse]") {
    // 5 V over 2.5 Ohm in series with 10 mH: the fuse opens with the inductor carrying 2 A
    Nodalys::CircuitBuilder builder;
    builder.AddVdc("V1", SUPPLY, 0, 5.0);
    builder.AddFuse("FU1", SUPPLY, LOAD, QUICK_FUSE);
    builder.AddInductor("L1", LOAD, INDUCTOR_END, 10e-3);
    builder.AddResistor("R1", INDUCTOR_END, 0, 2.5);
    auto circuit = builder.Build();
    REQUIRE(circuit.has_value());
    const Nodalys::TransientResult result = RunTransient(*circuit, 0.5, 1e-3);
    const std::vector<double> &node = result.NodeVoltages[LOAD];
    // Within 10 uV of rest, where the trapezoidal rule stays 1.1 mV off, alternating in sign at every point
    CHECK_THAT(node.back(), WithinAbs(0.0, 1e-5));
    // The last quarter of the run has no swing left
    double swing = 0.0;
    for (std::size_t index = result.Times.size() * 3 / 4; index < result.Times.size(); ++index) {
        swing = std::max(swing, std::fabs(node[index]));
    }
    CHECK_THAT(swing, WithinAbs(0.0, 1e-5));
}

TEST_CASE("Overloads far apart do not add up, and overloads close together do", "[nodalys][fuse]") {
    // Each 5 A pulse of 1 ms adds a sixth of the melting heat, which the fuse loses again in a second
    const auto build = [](const double period) {
        Nodalys::CircuitBuilder builder;
        builder.AddVpulse("V1", SUPPLY, 0,
                          {.Low = 0.0,
                           .High = 12.5,
                           .Delay = 1e-3,
                           .RiseTime = 10e-6,
                           .FallTime = 10e-6,
                           .Width = 1e-3,
                           .Period = period});
        builder.AddFuse("FU1", SUPPLY, LOAD, QUICK_FUSE);
        builder.AddResistor("R1", LOAD, 0, 2.5);
        auto circuit = builder.Build();
        REQUIRE(circuit.has_value());
        return std::move(*circuit);
    };
    const Nodalys::TransientResult spaced = RunTransient(build(1.0), 5.0, 200e-6);
    // The load still sees the pulse that starts at 4.001 s
    const auto last_pulse = std::ranges::find_if(spaced.Times, [](const double time) { return time >= 4.0015; });
    REQUIRE(last_pulse != spaced.Times.end());
    CHECK(spaced.NodeVoltages[LOAD][static_cast<std::size_t>(last_pulse - spaced.Times.begin())] > 10.0);

    const Nodalys::TransientResult close = RunTransient(build(10e-3), 0.5, 200e-6);
    CHECK(*std::ranges::max_element(close.NodeVoltages[LOAD]) > 10.0);
    CHECK(close.NodeVoltages[LOAD].back() < 1e-3);
}

TEST_CASE("A fuse blows at the right time with an output step larger than the time it takes", "[nodalys][fuse]") {
    // The output step is five times the blow time, so only the landing on the predicted instant finds it
    const double current = 5.0 / (2.5 + QUICK_FUSE.ColdResistance);
    const Nodalys::TransientResult result = RunTransient(BuildFuseLoop(5.0, 2.5), 0.5, 0.2);
    const std::optional<double> open_time = FindOpenTime(result);
    REQUIRE(open_time.has_value());
    CHECK_THAT(*open_time, WithinAbs(ExpectedBlowTime(current), 1e-4));
}
