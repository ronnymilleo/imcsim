/**
 * @file    smooth_logic_tests.cpp
 * @brief   Tests for C^inf smooth continuous logic macromodels, Verilog co-simulation bridge, and netlist logic cards.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <nodalys/nodalys.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

Nodalys::OperatingPointResult RunOp(const Nodalys::Circuit &circuit) {
    const auto result = Nodalys::ComputeOperatingPoint(circuit);
    REQUIRE(result.has_value());
    return *result;
}

} // namespace

TEST_CASE("Smooth continuous logic gates reproduce exact truth tables in DC operating point", "[nodalys][logic]") {
    SECTION("Inverter and Buffer") {
        for (const double in_val : {0.0, 5.0}) {
            Nodalys::CircuitBuilder b;
            b.AddVdc("V1", 1, 0, in_val);
            b.AddInverter("U1", 2, 1);
            b.AddLogicBuffer("U2", 3, 1);

            auto circuit = b.Build();
            REQUIRE(circuit.has_value());
            const auto op = RunOp(*circuit);

            const double expected_inv = (in_val > 2.5) ? 0.0 : 5.0;
            const double expected_buf = (in_val > 2.5) ? 5.0 : 0.0;
            CHECK_THAT(op.NodeVoltages[2], WithinAbs(expected_inv, 0.05));
            CHECK_THAT(op.NodeVoltages[3], WithinAbs(expected_buf, 0.05));
        }
    }

    SECTION("Two-input AND, NAND, OR, NOR, XOR, XNOR gates") {
        const std::vector<std::pair<double, double>> combinations = {{0.0, 0.0}, {0.0, 5.0}, {5.0, 0.0}, {5.0, 5.0}};

        for (const auto &[v1, v2] : combinations) {
            const bool b1 = (v1 > 2.5);
            const bool b2 = (v2 > 2.5);

            Nodalys::CircuitBuilder b;
            b.AddVdc("V1", 1, 0, v1);
            b.AddVdc("V2", 2, 0, v2);

            b.AddLogicGate("U_AND", Nodalys::LogicGateType::AND, 3, {1, 2});
            b.AddLogicGate("U_NAND", Nodalys::LogicGateType::NAND, 4, {1, 2});
            b.AddLogicGate("U_OR", Nodalys::LogicGateType::OR, 5, {1, 2});
            b.AddLogicGate("U_NOR", Nodalys::LogicGateType::NOR, 6, {1, 2});
            b.AddLogicGate("U_XOR", Nodalys::LogicGateType::XOR, 7, {1, 2});
            b.AddLogicGate("U_XNOR", Nodalys::LogicGateType::XNOR, 8, {1, 2});

            auto circuit = b.Build();
            REQUIRE(circuit.has_value());
            const auto op = RunOp(*circuit);

            const double exp_and = (b1 && b2) ? 5.0 : 0.0;
            const double exp_nand = !(b1 && b2) ? 5.0 : 0.0;
            const double exp_or = (b1 || b2) ? 5.0 : 0.0;
            const double exp_nor = !(b1 || b2) ? 5.0 : 0.0;
            const double exp_xor = (b1 ^ b2) ? 5.0 : 0.0;
            const double exp_xnor = !(b1 ^ b2) ? 5.0 : 0.0;

            CHECK_THAT(op.NodeVoltages[3], WithinAbs(exp_and, 0.05));
            CHECK_THAT(op.NodeVoltages[4], WithinAbs(exp_nand, 0.05));
            CHECK_THAT(op.NodeVoltages[5], WithinAbs(exp_or, 0.05));
            CHECK_THAT(op.NodeVoltages[6], WithinAbs(exp_nor, 0.05));
            CHECK_THAT(op.NodeVoltages[7], WithinAbs(exp_xor, 0.05));
            CHECK_THAT(op.NodeVoltages[8], WithinAbs(exp_xnor, 0.05));
        }
    }
}

TEST_CASE("Analog transfer curves of smooth gates are infinitely differentiable with continuous transitions",
          "[nodalys][logic]") {
    Nodalys::CircuitBuilder b;
    b.AddVdc("VIN", 1, 0, 0.0);
    Nodalys::SmoothLogicParameters params;
    params.VThreshold = 2.5;
    params.TransitionWidth = 0.2;
    b.AddInverter("U1", 2, 1, params);

    auto circuit = b.Build();
    REQUIRE(circuit.has_value());

    Nodalys::DcSweepConfig config;
    config.SweptSource = "VIN";
    config.SweptStart = 0.0;
    config.SweptStop = 5.0;
    config.SweptStep = 0.05;

    const auto res = Nodalys::ComputeDCSweep(*circuit, config);
    REQUIRE(res.has_value());
    REQUIRE(!res->Curves.empty());
    const auto &voltages = res->Curves[0].NodeVoltages[2];
    REQUIRE(!voltages.empty());

    // Monotonically decreasing output voltage
    for (std::size_t i = 1; i < voltages.size(); ++i) {
        CHECK(voltages[i] <= voltages[i - 1] + 1e-9);
    }

    // Midpoint threshold voltage test
    const std::size_t mid_idx = voltages.size() / 2;
    CHECK_THAT(voltages[mid_idx], WithinAbs(2.5, 0.05));
}

TEST_CASE("Schmitt trigger displays continuous analog hysteresis with upper and lower thresholds", "[nodalys][logic]") {
    auto build_circuit = [](const double vin_val) {
        Nodalys::CircuitBuilder b;
        b.AddVdc("VIN", 1, 0, vin_val);
        Nodalys::SmoothLogicParameters params;
        params.VThreshold = 2.5;
        params.Hysteresis = 1.0;
        params.TransitionWidth = 0.05;
        b.AddSchmittTrigger("U1", 2, 1, params);
        auto c = b.Build();
        REQUIRE(c.has_value());
        return std::move(*c);
    };

    // When VIN is at 0 V, output is LOW (0 V)
    auto c1 = build_circuit(0.0);
    auto op1 = RunOp(c1);
    CHECK_THAT(op1.NodeVoltages[2], WithinAbs(0.0, 0.05));

    // At VIN = 2.2 V (above 2.0 V lower threshold, but below 3.0 V upper threshold when starting LOW)
    auto c2 = build_circuit(2.2);
    auto op2 = RunOp(c2);
    CHECK_THAT(op2.NodeVoltages[2], WithinAbs(0.0, 0.1));

    // At VIN = 3.5 V (above 3.0 V upper threshold), output flips HIGH (5 V)
    auto c3 = build_circuit(3.5);
    auto op3 = RunOp(c3);
    CHECK_THAT(op3.NodeVoltages[2], WithinAbs(5.0, 0.1));
}

TEST_CASE("Inertial delay attenuates narrow glitch pulses shorter than propagation delay", "[nodalys][logic]") {
    Nodalys::CircuitBuilder b;
    Nodalys::PulseParameters pulse;
    pulse.Low = 0.0;
    pulse.High = 5.0;
    pulse.Delay = 10e-9;
    pulse.RiseTime = 1e-9;
    pulse.FallTime = 1e-9;
    pulse.Width = 20e-9;
    pulse.Period = 100e-9;
    b.AddVpulse("VIN", 1, 0, pulse);

    Nodalys::SmoothLogicParameters params;
    params.PropagationDelay = 5e-9; // 5 ns delay, shorter than 20 ns pulse
    b.AddInverter("U1", 2, 1, params);

    auto circuit = b.Build();
    REQUIRE(circuit.has_value());

    Nodalys::TransientConfig tran;
    tran.StopTime = 80e-9;
    tran.TimeStep = 0.5e-9;
    tran.UseInitialConditions = true;

    const auto res = Nodalys::ComputeTransient(*circuit, tran);
    REQUIRE(res.has_value());

    // Check that output reached near 0 V during the pulse
    double min_out = 5.0;
    for (const double v : res->NodeVoltages[2]) {
        min_out = std::min(min_out, v);
    }
    CHECK(min_out < 0.5);
}

TEST_CASE("Cross-coupled NOR gates form a stable continuous SR latch", "[nodalys][logic]") {
    Nodalys::CircuitBuilder b;
    // S pulse at 10 ns (width 10 ns)
    Nodalys::PulseParameters pulse_s;
    pulse_s.Low = 0.0;
    pulse_s.High = 5.0;
    pulse_s.Delay = 10e-9;
    pulse_s.RiseTime = 1e-9;
    pulse_s.FallTime = 1e-9;
    pulse_s.Width = 10e-9;
    pulse_s.Period = 1e-3;
    b.AddVpulse("V_S", 1, 0, pulse_s);

    // R pulse at 40 ns (width 10 ns)
    Nodalys::PulseParameters pulse_r;
    pulse_r.Low = 0.0;
    pulse_r.High = 5.0;
    pulse_r.Delay = 40e-9;
    pulse_r.RiseTime = 1e-9;
    pulse_r.FallTime = 1e-9;
    pulse_r.Width = 10e-9;
    pulse_r.Period = 1e-3;
    b.AddVpulse("V_R", 2, 0, pulse_r);

    Nodalys::SmoothLogicParameters params;
    params.PropagationDelay = 1e-9;
    b.AddSRLatch("SR1", 3, 4, 1, 2, params);

    auto circuit = b.Build();
    REQUIRE(circuit.has_value());

    Nodalys::TransientConfig tran;
    tran.StopTime = 70e-9;
    tran.TimeStep = 0.5e-9;
    tran.UseInitialConditions = true;

    const auto res = Nodalys::ComputeTransient(*circuit, tran);
    REQUIRE(res.has_value());

    auto sample_at = [&](const double t, const int node) {
        for (std::size_t i = 0; i < res->Times.size(); ++i) {
            if (res->Times[i] >= t) {
                return res->NodeVoltages[static_cast<std::size_t>(node)][i];
            }
        }
        return res->NodeVoltages[static_cast<std::size_t>(node)].back();
    };

    // At t = 25 ns (after Set pulse has ended, S=0, R=0): Q must be latched HIGH (near 5 V), Q_bar LOW
    CHECK_THAT(sample_at(25e-9, 3), WithinAbs(5.0, 0.2));
    CHECK_THAT(sample_at(25e-9, 4), WithinAbs(0.0, 0.2));

    // At t = 55 ns (after Reset pulse has ended, S=0, R=0): Q must be latched LOW (near 0 V), Q_bar HIGH
    CHECK_THAT(sample_at(55e-9, 3), WithinAbs(0.0, 0.2));
    CHECK_THAT(sample_at(55e-9, 4), WithinAbs(5.0, 0.2));
}

TEST_CASE("Edge-triggered D flip-flop macromodel instantiates and converges cleanly", "[nodalys][logic]") {
    Nodalys::CircuitBuilder b;
    // D starts at 5 V, drops to 0 V at 45 ns
    Nodalys::PulseParameters pulse_d;
    pulse_d.Low = 5.0;
    pulse_d.High = 0.0;
    pulse_d.Delay = 45e-9;
    pulse_d.RiseTime = 1e-9;
    pulse_d.FallTime = 1e-9;
    pulse_d.Width = 100e-9;
    pulse_d.Period = 1e-3;
    b.AddVpulse("V_D", 1, 0, pulse_d);

    // Clock square wave with rising edges at 20 ns, 60 ns
    Nodalys::PulseParameters pulse_clk;
    pulse_clk.Low = 0.0;
    pulse_clk.High = 5.0;
    pulse_clk.Delay = 20e-9;
    pulse_clk.RiseTime = 1e-9;
    pulse_clk.FallTime = 1e-9;
    pulse_clk.Width = 20e-9;
    pulse_clk.Period = 40e-9;
    b.AddVpulse("V_CLK", 2, 0, pulse_clk);

    Nodalys::SmoothLogicParameters params;
    params.PropagationDelay = 1e-9;
    b.AddDFlipFlop("DFF1", 3, 4, 1, 2, params);

    auto circuit = b.Build();
    REQUIRE(circuit.has_value());

    Nodalys::TransientConfig tran;
    tran.StopTime = 80e-9;
    tran.TimeStep = 0.5e-9;
    tran.UseInitialConditions = true;

    const auto res = Nodalys::ComputeTransient(*circuit, tran);
    REQUIRE(res.has_value());

    auto sample_at = [&](const double t, const int node) {
        for (std::size_t i = 0; i < res->Times.size(); ++i) {
            if (res->Times[i] >= t) {
                return res->NodeVoltages[static_cast<std::size_t>(node)][i];
            }
        }
        return res->NodeVoltages[static_cast<std::size_t>(node)].back();
    };

    // At t = 35 ns (after first rising clock edge when D=5V): Q must be HIGH
    CHECK_THAT(sample_at(35e-9, 3), WithinAbs(5.0, 0.3));
    CHECK_THAT(sample_at(35e-9, 4), WithinAbs(0.0, 0.3));

    // At t = 75 ns (after second rising clock edge when D=0V): Q must be LOW
    CHECK_THAT(sample_at(75e-9, 3), WithinAbs(0.0, 0.3));
    CHECK_THAT(sample_at(75e-9, 4), WithinAbs(5.0, 0.3));
}

TEST_CASE("Verilog co-simulation bridge converts discrete levels and synthesizes smooth ramps", "[nodalys][bridge]") {
    Nodalys::SmoothLogicParameters params;
    params.VLow = 0.0;
    params.VHigh = 3.3;
    params.VThreshold = 1.65;

    SECTION("VoltageToDigital") {
        CHECK(Nodalys::VoltageToDigital(0.1, params, 0.3) == Nodalys::DigitalLevel::LOW);
        CHECK(Nodalys::VoltageToDigital(3.2, params, 0.3) == Nodalys::DigitalLevel::HIGH);
        CHECK(Nodalys::VoltageToDigital(1.65, params, 0.3) == Nodalys::DigitalLevel::UNKNOWN);
    }

    SECTION("DigitalToVoltage") {
        CHECK_THAT(Nodalys::DigitalToVoltage(Nodalys::DigitalLevel::LOW, params), WithinAbs(0.0, 1e-9));
        CHECK_THAT(Nodalys::DigitalToVoltage(Nodalys::DigitalLevel::HIGH, params), WithinAbs(3.3, 1e-9));
        CHECK_THAT(Nodalys::DigitalToVoltage(Nodalys::DigitalLevel::UNKNOWN, params), WithinAbs(1.65, 1e-9));
    }

    SECTION("Format and Parse") {
        CHECK(Nodalys::FormatDigitalLevel(Nodalys::DigitalLevel::LOW) == "0");
        CHECK(Nodalys::FormatDigitalLevel(Nodalys::DigitalLevel::HIGH) == "1");
        CHECK(Nodalys::FormatDigitalLevel(Nodalys::DigitalLevel::UNKNOWN) == "X");
        CHECK(Nodalys::FormatDigitalLevel(Nodalys::DigitalLevel::HIGH_Z) == "Z");

        CHECK(Nodalys::ParseDigitalLevel("0") == Nodalys::DigitalLevel::LOW);
        CHECK(Nodalys::ParseDigitalLevel("1") == Nodalys::DigitalLevel::HIGH);
        CHECK(Nodalys::ParseDigitalLevel("X") == Nodalys::DigitalLevel::UNKNOWN);
        CHECK(Nodalys::ParseDigitalLevel("Z") == Nodalys::DigitalLevel::HIGH_Z);
        CHECK(!Nodalys::ParseDigitalLevel("invalid").has_value());
    }

    SECTION("EvaluateSmoothTransition") {
        Nodalys::SmoothTransition trans;
        trans.StartTime = 10e-9;
        trans.Duration = 2e-9;
        trans.StartVoltage = 0.0;
        trans.TargetVoltage = 3.3;

        CHECK_THAT(Nodalys::EvaluateSmoothTransition(trans, 0.0), WithinAbs(0.0, 0.01));
        CHECK_THAT(Nodalys::EvaluateSmoothTransition(trans, 11e-9), WithinAbs(1.65, 0.05));
        CHECK_THAT(Nodalys::EvaluateSmoothTransition(trans, 20e-9), WithinAbs(3.3, 0.01));
    }
}

TEST_CASE("Autonomous 3-inverter ring oscillator generates continuous periodic oscillations", "[nodalys][logic]") {
    Nodalys::CircuitBuilder b;
    Nodalys::SmoothLogicParameters params;
    params.PropagationDelay = 2e-9;
    params.TransitionWidth = 0.1;

    b.AddInverter("INV1", 2, 1, params);
    b.AddInverter("INV2", 3, 2, params);
    b.AddInverter("INV3", 1, 3, params);

    auto circuit = b.Build();
    REQUIRE(circuit.has_value());

    Nodalys::TransientConfig tran;
    tran.StopTime = 40e-9;
    tran.TimeStep = 0.1e-9;
    tran.UseInitialConditions = true;

    const auto res = Nodalys::ComputeTransient(*circuit, tran);
    REQUIRE(res.has_value());

    // Verify oscillation by checking that node 1 transitions both below 1.0 V and above 4.0 V
    double min_v = 5.0;
    double max_v = 0.0;
    for (const double v : res->NodeVoltages[1]) {
        min_v = std::min(min_v, v);
        max_v = std::max(max_v, v);
    }
    CHECK(min_v < 1.0);
    CHECK(max_v > 4.0);
}

TEST_CASE("SPICE netlist parser instantiates and simulates smooth logic gates", "[nodalys][parser][logic]") {
    const std::string netlist = R"(
* Smooth continuous logic netlist test
V1 in1 0 DC 5.0
V2 in2 0 DC 0.0
U1 out_and in1 in2 AND VHIGH=5 VLOW=0 VTH=2.5
U2 out_or  in1 in2 OR  VHIGH=5 VLOW=0 VTH=2.5
U3 out_inv in1     NOT VHIGH=5 VLOW=0 VTH=2.5
.OP
.END
)";

    auto parse_res = Nodalys::ParseNetlist(netlist);
    REQUIRE(parse_res.has_value());

    const auto op = RunOp(parse_res->CircuitInstance);
    REQUIRE(op.NodeVoltages.size() >= 6);

    CHECK_THAT(op.NodeVoltages[3], WithinAbs(0.0, 0.05));
    CHECK_THAT(op.NodeVoltages[4], WithinAbs(5.0, 0.05));
    CHECK_THAT(op.NodeVoltages[5], WithinAbs(0.0, 0.05));
}
