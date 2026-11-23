/**
 * @file    thermal_tests.cpp
 * @brief   Tests for SPICE temperature scaling across Nodalys devices, thermal noise, expressions, and parser.
 */

#include "devices/thermal.h"
#include "nodalys/nodalys.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <string>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

TEST_CASE("Thermal laws return exact nominal values at T = TNOM and follow SPICE away from it", "[thermal]") {
    const Nodalys::ThermalConditions nominal = Nodalys::MakeThermalConditions(27.0, 27.0);
    REQUIRE(Nodalys::IsNominal(nominal));
    CHECK(Nodalys::GetSaturationCurrentLogRatio(nominal, 1.11, 3.0) == 0.0);
    CHECK(Nodalys::GetJunctionPotential(nominal, 0.8) == 0.8);
    CHECK(Nodalys::GetJunctionCapacitance(nominal, 12e-12, 0.8, 0.5) == 12e-12);

    const Nodalys::ThermalConditions hot = Nodalys::MakeThermalConditions(100.0, 27.0);
    REQUIRE_FALSE(Nodalys::IsNominal(hot));
    CHECK_THAT(Nodalys::GetThermalVoltage(hot), WithinRel(8.617087399994e-5 * 373.15, 1e-12));
    CHECK_THAT(Nodalys::GetFourKt(hot), WithinRel(4.0 * 1.380649e-23 * 373.15, 1e-12));
    CHECK(Nodalys::GetBandGap(373.15) < Nodalys::GetBandGap(300.15));
    CHECK(Nodalys::GetSaturationCurrentLogRatio(hot, 1.11, 3.0) > 0.0);
    // Junction potential decreases as temperature rises
    CHECK(Nodalys::GetJunctionPotential(hot, 0.6) < 0.6);
    CHECK(Nodalys::GetJunctionCapacitance(hot, 10e-12, 0.6, 0.5) > 10e-12);
}

TEST_CASE("Diode forward drop decreases with temperature and honors EG and XTI", "[thermal]") {
    const std::string netlist = R"(Diode temperature test
V1 1 0 DC 5
R1 1 2 1k
D1 2 0 DTEST
.model DTEST D(IS=1e-14 N=1 EG=1.11 XTI=3)
.temp 100
.op
.end
)";
    auto parsed = Nodalys::ParseNetlist(netlist);
    REQUIRE(parsed.has_value());
    auto hot = Nodalys::ComputeOperatingPoint(parsed->CircuitInstance);
    REQUIRE(hot.has_value());

    parsed->CircuitInstance.SetTemperature(27.0);
    auto nominal = Nodalys::ComputeOperatingPoint(parsed->CircuitInstance);
    REQUIRE(nominal.has_value());

    // Silicon diode drops roughly 2 mV/K, so 73 K hotter drops more than 0.1 V
    CHECK(hot->NodeVoltages[2] < nominal->NodeVoltages[2] - 0.1);
}

TEST_CASE("BJT collector current grows with temperature and scales beta with XTB", "[thermal]") {
    Nodalys::CircuitBuilder builder(3);
    builder.AddVdc("VCC", 1, 0, 5.0);
    builder.AddVdc("VBE", 2, 0, 0.65);
    Nodalys::BjtParameters params;
    params.SaturationCurrent = 1e-15;
    params.ForwardBeta = 100.0;
    params.BetaExponent = 1.5;
    builder.AddBjt("Q1", 1, 2, 0, true, params);

    auto circuit = builder.Build();
    REQUIRE(circuit.has_value());

    auto at_27 = Nodalys::ComputeOperatingPoint(*circuit);
    REQUIRE(at_27.has_value());

    circuit->SetTemperature(100.0);
    auto at_100 = Nodalys::ComputeOperatingPoint(*circuit);
    REQUIRE(at_100.has_value());

    // At fixed Vbe = 0.65 V, Ic increases strongly with temperature and beta grows as (373.15/300.15)^1.5
    const double ic_27 = -at_27->Currents[0].Current;
    const double ic_100 = -at_100->Currents[0].Current;
    const double ib_27 = -at_27->Currents[1].Current;
    const double ib_100 = -at_100->Currents[1].Current;
    CHECK(ic_100 > ic_27 * 10.0);
    const double expected_beta_100 = 100.0 * std::pow(373.15 / 300.15, 1.5);
    CHECK_THAT(ic_100 / ib_100, WithinRel(expected_beta_100, 1e-3));
    CHECK_THAT(ic_27 / ib_27, WithinRel(100.0, 1e-3));
}

TEST_CASE("MOSFET level-1 drain current matches ngspice at 0 C, 27 C, and 100 C", "[thermal]") {
    const std::string netlist = R"(MOSFET temperature test
.model nmos1 NMOS (LEVEL=1 VTO=2.0 KP=1e-4 PHI=0.6)
.model pmos1 PMOS (LEVEL=1 VTO=-2.0 KP=5e-5 PHI=0.6)
Vd1 d1 0 5
Vg1 g1 0 4
M1 d1 g1 0 0 nmos1 W=100u L=100u
Vd2 d2 0 -5
Vg2 g2 0 -4
M2 d2 g2 0 0 pmos1 W=100u L=100u
.op
.end
)";
    auto parsed = Nodalys::ParseNetlist(netlist);
    REQUIRE(parsed.has_value());

    auto find_current = [](const Nodalys::OperatingPointResult &op, const std::string_view name) {
        for (const auto &entry : op.Currents) {
            if (entry.Name == name) {
                return entry.Current;
            }
        }
        FAIL("Missing current: " << name);
        return 0.0;
    };

    auto op_27 = Nodalys::ComputeOperatingPoint(parsed->CircuitInstance);
    REQUIRE(op_27.has_value());
    CHECK_THAT(find_current(*op_27, "Vd1"), WithinRel(-2.0e-4, 1e-5));
    CHECK_THAT(find_current(*op_27, "Vd2"), WithinRel(1.0e-4, 1e-5));

    parsed->CircuitInstance.SetTemperature(100.0);
    auto op_100 = Nodalys::ComputeOperatingPoint(parsed->CircuitInstance);
    REQUIRE(op_100.has_value());
    CHECK_THAT(find_current(*op_100, "Vd1"), WithinRel(-1.55028e-4, 1e-4));
    CHECK_THAT(find_current(*op_100, "Vd2"), WithinRel(7.909835e-5, 1e-4));

    parsed->CircuitInstance.SetTemperature(0.0);
    auto op_0 = Nodalys::ComputeOperatingPoint(parsed->CircuitInstance);
    REQUIRE(op_0.has_value());
    CHECK_THAT(find_current(*op_0, "Vd1"), WithinRel(-2.24312e-4, 1e-4));
    CHECK_THAT(find_current(*op_0, "Vd2"), WithinRel(1.113628e-4, 1e-4));
}

TEST_CASE("Resistor thermal noise scales linearly with absolute temperature", "[thermal]") {
    Nodalys::CircuitBuilder builder(2);
    builder.AddVdc("VIN", 1, 0, 0.0, 1.0);
    builder.AddResistor("R1", 1, 2, 1000.0);
    auto circuit = builder.Build();
    REQUIRE(circuit.has_value());

    const Nodalys::NoiseConfig config{.OutputPositiveNode = 2,
                                      .OutputNegativeNode = 0,
                                      .InputSource = "VIN",
                                      .StartFrequency = 1e3,
                                      .StopFrequency = 1e3,
                                      .PointsPerDecade = 1};
    auto noise_27 = Nodalys::ComputeNoise(*circuit, config);
    REQUIRE(noise_27.has_value());

    circuit->SetTemperature(100.0);
    auto noise_100 = Nodalys::ComputeNoise(*circuit, config);
    REQUIRE(noise_100.has_value());

    const double ratio = noise_100->OutputDensities.front() / noise_27->OutputDensities.front();
    CHECK_THAT(ratio, WithinRel(std::sqrt(373.15 / 300.15), 1e-6));
}

TEST_CASE("Behavioral source temper variable reads the simulation temperature", "[thermal]") {
    Nodalys::CircuitBuilder builder(2);
    builder.AddBehavioralVoltage("B1", 1, 0, "temper");
    builder.SetTemperature(85.0);
    auto circuit = builder.Build();
    REQUIRE(circuit.has_value());

    auto op = Nodalys::ComputeOperatingPoint(*circuit);
    REQUIRE(op.has_value());
    CHECK_THAT(op->NodeVoltages[1], WithinAbs(85.0, 1e-12));

    circuit->SetTemperature(-10.0);
    auto cold = Nodalys::ComputeOperatingPoint(*circuit);
    REQUIRE(cold.has_value());
    CHECK_THAT(cold->NodeVoltages[1], WithinAbs(-10.0, 1e-12));
}
