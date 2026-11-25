/**
 * @file    transmission_line_tests.cpp
 * @brief   Tests for differential S-parameter ports and lossless and lossy dispersive transmission lines across DC,
 *          AC, S-parameters, noise, AC sensitivity, harmonic balance, pole-zero refusal and transient.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <nodalys/nodalys.h>

#include <cmath>
#include <complex>
#include <cstddef>
#include <format>
#include <numbers>
#include <string>
#include <vector>

using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

using Complex = std::complex<double>;

constexpr double TWO_PI = 2.0 * std::numbers::pi;
constexpr double BOLTZMANN_CONSTANT = 1.380649e-23;

struct TwoPortScattering {
    Complex S11;
    Complex S21;
};

void RequireClose(const Complex actual, const Complex expected, const double tolerance) {
    CHECK_THAT(actual.real(), WithinAbs(expected.real(), tolerance));
    CHECK_THAT(actual.imag(), WithinAbs(expected.imag(), tolerance));
}

/// Closed-form telegrapher scattering parameters of a uniform transmission line between port1_ohms and port2_ohms.
TwoPortScattering AnalyticalScattering(const Nodalys::TransmissionLineParameters &parameters,
                                       const double frequency,
                                       const double port1_ohms,
                                       const double port2_ohms) {
    const double omega = TWO_PI * frequency;
    const double total_inductance = parameters.ImpedanceOhms * parameters.DelaySeconds;
    const double total_capacitance = parameters.DelaySeconds / parameters.ImpedanceOhms;
    const double resistance =
        parameters.DcResistanceOhms
        + (frequency > 0.0 ? parameters.SkinResistanceOhms * std::sqrt(frequency / parameters.ReferenceFrequencyHz)
                           : 0.0);
    const double conductance = parameters.DcConductanceSiemens + omega * total_capacitance * parameters.LossTangent;

    const Complex z_total(resistance, omega * total_inductance);
    const Complex y_total(conductance, omega * total_capacitance);
    const Complex theta = std::sqrt(z_total * y_total);
    const Complex z_char = std::sqrt(z_total / y_total);

    const Complex cosh_theta = std::cosh(theta);
    const Complex sinh_theta = std::sinh(theta);
    const Complex a_term = cosh_theta;
    const Complex b_term = z_char * sinh_theta;
    const Complex c_term = sinh_theta / z_char;
    const Complex d_term = cosh_theta;

    const Complex denominator = a_term * port2_ohms + b_term + c_term * (port1_ohms * port2_ohms) + d_term * port1_ohms;
    const Complex s11 =
        (a_term * port2_ohms + b_term - c_term * (port1_ohms * port2_ohms) - d_term * port1_ohms) / denominator;
    const Complex s21 = 2.0 * std::sqrt(port1_ohms * port2_ohms) / denominator;
    return {.S11 = s11, .S21 = s21};
}

Complex AcPhasor(const Nodalys::AcSweepResult &result, const std::size_t node, const std::size_t index) {
    const double magnitude = std::pow(10.0, result.NodeMagnitudesDecibels[node][index] / 20.0);
    const double phase_rad = result.NodePhasesDegrees[node][index] * (std::numbers::pi / 180.0);
    return std::polar(magnitude, phase_rad);
}

double SampleAtOrAfter(const Nodalys::TransientResult &result, const std::size_t node, const double time) {
    std::size_t index = 0;
    while (index + 1 < result.Times.size() && result.Times[index] < time) {
        ++index;
    }
    return result.NodeVoltages[node][index];
}

double CurrentOf(const Nodalys::OperatingPointResult &point, const std::string &name) {
    for (const Nodalys::ComponentCurrent &entry : point.Currents) {
        if (entry.Name == name) {
            return entry.Current;
        }
    }
    FAIL("Missing operating-point current " << name);
    return 0.0;
}

} // namespace

TEST_CASE("A differential S-parameter port measures a floating network across two non-ground nodes",
          "[nodalys][sp][port]") {
    // Nodes 1 and 2 each sit on 200 ohm to ground with 100 ohm directly between them:
    // differential impedance is 100 || (200 + 200) = 80 ohm, so S11 against 50 ohm is (80 - 50) / (80 + 50) = 3/13
    constexpr double EXPECTED_S11 = 30.0 / 130.0;

    Nodalys::CircuitBuilder builder(3);
    builder.AddPort(1, 1, 50.0, 2);
    builder.AddResistor("RDIFF", 1, 2, 100.0);
    builder.AddResistor("RLEG1", 1, 0, 200.0);
    builder.AddResistor("RLEG2", 2, 0, 200.0);
    const auto built = builder.Build();
    REQUIRE(built.has_value());

    const auto api_sp = Nodalys::ComputeSParameters(
        *built,
        {.SweepType = Nodalys::FrequencySweepType::LINEAR, .StartFrequency = 1e6, .StopFrequency = 2e6, .Points = 2});
    REQUIRE(api_sp.has_value());
    RequireClose(api_sp->Get(0, 1, 1), EXPECTED_S11, 1e-9);
    RequireClose(*api_sp->Impedance(0, 1, 1), 80.0, 1e-6);

    // Both the dedicated P card and the V source port syntax read differential nodes too
    for (const std::string port_line : {"P1 1 2 z0=50\n", "Vsport1 1 2 dc 0 portnum 1 z0 50\n"}) {
        const auto parsed = Nodalys::ParseNetlist(
            std::format("* diff port\n{}RDIFF 1 2 100\nRLEG1 1 0 200\nRLEG2 2 0 200\n.sp lin 2 1Meg 2Meg\n.end\n",
                        port_line));
        REQUIRE(parsed.has_value());
        const auto sp = Nodalys::ComputeSParameters(parsed->CircuitInstance,
                                                    std::get<Nodalys::SParameterConfig>(parsed->Analyses[0].Config));
        REQUIRE(sp.has_value());
        RequireClose(sp->Get(1, 1, 1), EXPECTED_S11, 1e-9);
    }

    // Degenerate differential ports (both terminals on the same node) are refused clearly
    Nodalys::CircuitBuilder shorted_port(2);
    shorted_port.AddPort(1, 1, 50.0, 1);
    shorted_port.AddResistor("R1", 1, 0, 50.0);
    const auto shorted_circuit = shorted_port.Build();
    REQUIRE(shorted_circuit.has_value());
    const auto refused = Nodalys::ComputeSParameters(
        *shorted_circuit,
        {.SweepType = Nodalys::FrequencySweepType::LINEAR, .StartFrequency = 1e6, .StopFrequency = 1e6, .Points = 1});
    REQUIRE_FALSE(refused.has_value());
    CHECK_THAT(refused.error(), ContainsSubstring("both terminals on node 1"));
}

TEST_CASE("A lossless transmission line passes DC and matches telegrapher theory through quarter, half and full wave",
          "[nodalys][transmission_line][sp][ac]") {
    // Z0 = 50 ohm, Td = 1 ns -> quarter-wave 250 MHz, half-wave 500 MHz (beta*l = pi), full-wave 1 GHz (beta*l = 2*pi)
    Nodalys::CircuitBuilder dc_builder(4);
    dc_builder.AddVdc("V1", 1, 0, 10.0);
    dc_builder.AddResistor("RS", 1, 2, 50.0);
    dc_builder.AddTransmissionLine("TL1", 2, 0, 3, 0, {.ImpedanceOhms = 50.0, .DelaySeconds = 1e-9});
    dc_builder.AddResistor("RL", 3, 0, 150.0);
    const auto dc_circuit = dc_builder.Build();
    REQUIRE(dc_circuit.has_value());

    const auto op = Nodalys::ComputeOperatingPoint(*dc_circuit);
    REQUIRE(op.has_value());
    // At DC a lossless line is a direct connection: 10 V * 150 / (50 + 150) = 7.5 V, loop current = 50 mA
    CHECK_THAT(op->NodeVoltages[2], WithinAbs(7.5, 1e-9));
    CHECK_THAT(op->NodeVoltages[3], WithinAbs(7.5, 1e-9));
    CHECK_THAT(CurrentOf(*op, "TL1.P1"), WithinAbs(0.05, 1e-9));
    CHECK_THAT(CurrentOf(*op, "TL1.P2"), WithinAbs(-0.05, 1e-9));

    // Matched 50 ohm ports across 250 MHz, 500 MHz, 750 MHz and 1 GHz: S11 = 0, S21 = exp(-j * omega * Td)
    Nodalys::CircuitBuilder matched_builder(3);
    matched_builder.AddPort(1, 1, 50.0);
    matched_builder.AddPort(2, 2, 50.0);
    matched_builder.AddTransmissionLine("TL1", 1, 0, 2, 0, {.ImpedanceOhms = 50.0, .DelaySeconds = 1e-9});
    const auto matched_circuit = matched_builder.Build();
    REQUIRE(matched_circuit.has_value());

    const auto matched_sp = Nodalys::ComputeSParameters(
        *matched_circuit,
        {.SweepType = Nodalys::FrequencySweepType::LINEAR, .StartFrequency = 250e6, .StopFrequency = 1e9, .Points = 4});
    REQUIRE(matched_sp.has_value());
    REQUIRE(matched_sp->Frequencies.size() == 4);
    for (std::size_t index = 0; index < matched_sp->Frequencies.size(); ++index) {
        const double phase = -TWO_PI * matched_sp->Frequencies[index] * 1e-9;
        RequireClose(matched_sp->Get(index, 1, 1), 0.0, 1e-9);
        RequireClose(matched_sp->Get(index, 2, 2), 0.0, 1e-9);
        RequireClose(matched_sp->Get(index, 2, 1), std::polar(1.0, phase), 1e-9);
        RequireClose(matched_sp->Get(index, 1, 2), std::polar(1.0, phase), 1e-9);
    }

    // Quarter-wave impedance transformer: Z0 = 100 ohm transforms RL = 200 ohm to Zin = 100^2 / 200 = 50 ohm at 250 MHz
    Nodalys::CircuitBuilder qwave_builder(3);
    qwave_builder.AddPort(1, 1, 50.0);
    qwave_builder.AddTransmissionLine("TLQ", 1, 0, 2, 0, {.ImpedanceOhms = 100.0, .DelaySeconds = 1e-9});
    qwave_builder.AddResistor("RL", 2, 0, 200.0);
    const auto qwave_circuit = qwave_builder.Build();
    REQUIRE(qwave_circuit.has_value());

    const auto qwave_sp = Nodalys::ComputeSParameters(*qwave_circuit,
                                                      {.SweepType = Nodalys::FrequencySweepType::LINEAR,
                                                       .StartFrequency = 250e6,
                                                       .StopFrequency = 500e6,
                                                       .Points = 2});
    REQUIRE(qwave_sp.has_value());
    // At 250 MHz (quarter wave): Zin = 50 ohm -> S11 = 0; at 500 MHz (half wave): Zin = 200 ohm -> S11 = 0.6
    RequireClose(qwave_sp->Get(0, 1, 1), 0.0, 1e-9);
    RequireClose(*qwave_sp->Impedance(0, 1, 1), 50.0, 1e-5);
    RequireClose(qwave_sp->Get(1, 1, 1), 0.6, 1e-9);
    RequireClose(*qwave_sp->Impedance(1, 1, 1), 200.0, 1e-4);

    // An open-ended quarter-wave shunt stub shorts the line at 250 MHz and becomes an open again at 500 MHz
    Nodalys::CircuitBuilder stub_builder(4);
    stub_builder.AddVdc("VIN", 1, 0, 0.0, 1.0, 0.0);
    stub_builder.AddResistor("RS", 1, 2, 50.0);
    stub_builder.AddResistor("RL", 2, 0, 50.0);
    stub_builder.AddTransmissionLine("TLSTUB", 2, 0, 3, 0, {.ImpedanceOhms = 50.0, .DelaySeconds = 1e-9});
    const auto stub_circuit = stub_builder.Build();
    REQUIRE(stub_circuit.has_value());

    const auto stub_ac = Nodalys::ComputeACSweep(*stub_circuit,
                                                 {.StartFrequency = 250e6,
                                                  .StopFrequency = 500e6,
                                                  .PointsPerDecade = 2,
                                                  .SweepType = Nodalys::FrequencySweepType::LINEAR});
    REQUIRE(stub_ac.has_value());
    CHECK_THAT(std::abs(AcPhasor(*stub_ac, 2, 0)), WithinAbs(0.0, 1e-6));
    CHECK_THAT(std::abs(AcPhasor(*stub_ac, 2, 1)), WithinAbs(0.5, 1e-6));
}

TEST_CASE("SPICE T lines parse both TD and F/NL forms at the top level and inside subcircuit text",
          "[nodalys][transmission_line][parser]") {
    const auto td_netlist = Nodalys::ParseNetlist(
        "* td form\nP1 1 0 z0=50\nP2 2 0 z0=50\nT1 1 0 2 0 Z0=75 TD=1n\n.sp lin 3 100Meg 500Meg\n.end\n");
    const auto nl_netlist = Nodalys::ParseNetlist(
        "* nl form\nP1 1 0 z0=50\nP2 2 0 z0=50\nT1 1 0 2 0 ZO=75 F=250Meg NL=0.25\n.sp lin 3 100Meg 500Meg\n.end\n");
    REQUIRE(td_netlist.has_value());
    REQUIRE(nl_netlist.has_value());

    const auto td_sp = Nodalys::ComputeSParameters(td_netlist->CircuitInstance,
                                                   std::get<Nodalys::SParameterConfig>(td_netlist->Analyses[0].Config));
    const auto nl_sp = Nodalys::ComputeSParameters(nl_netlist->CircuitInstance,
                                                   std::get<Nodalys::SParameterConfig>(nl_netlist->Analyses[0].Config));
    REQUIRE(td_sp.has_value());
    REQUIRE(nl_sp.has_value());
    for (std::size_t index = 0; index < td_sp->Frequencies.size(); ++index) {
        RequireClose(nl_sp->Get(index, 1, 1), td_sp->Get(index, 1, 1), 1e-12);
        RequireClose(nl_sp->Get(index, 2, 1), td_sp->Get(index, 2, 1), 1e-12);
    }

    // Subcircuit text expansion with parameterized Z0 and TD
    Nodalys::CircuitBuilder sub_builder(3);
    sub_builder.AddPort(1, 1, 50.0);
    sub_builder.AddPort(2, 2, 50.0);
    const Nodalys::SubcircuitText block{.Name = "LINE_BLOCK",
                                        .Ports = {"IN", "OUT"},
                                        .Body = "T1 IN 0 OUT 0 Z0={ZLINE} TD={DELAY}\n"};
    REQUIRE(Nodalys::AddSubcircuitText(
                sub_builder,
                block,
                {.Prefix = "X1", .PortNodes = {1, 2}, .Parameters = {{"ZLINE", 75.0}, {"DELAY", 1e-9}}})
                .has_value());
    const auto sub_circuit = sub_builder.Build();
    REQUIRE(sub_circuit.has_value());
    const auto sub_sp = Nodalys::ComputeSParameters(*sub_circuit,
                                                    {.SweepType = Nodalys::FrequencySweepType::LINEAR,
                                                     .StartFrequency = 100e6,
                                                     .StopFrequency = 500e6,
                                                     .Points = 3});
    REQUIRE(sub_sp.has_value());
    RequireClose(sub_sp->Get(0, 1, 1), td_sp->Get(0, 1, 1), 1e-12);
    RequireClose(sub_sp->Get(1, 2, 1), td_sp->Get(1, 2, 1), 1e-12);
}

TEST_CASE("A lossy dispersive line matches closed-form telegrapher theory in S-parameters, AC, noise and HB",
          "[nodalys][transmission_line][lossy][hb][noise]") {
    const Nodalys::TransmissionLineParameters lossy{
        .ImpedanceOhms = 50.0,
        .DelaySeconds = 1.5e-9,
        .DcResistanceOhms = 4.0,
        .SkinResistanceOhms = 12.0,
        .DcConductanceSiemens = 1e-4,
        .LossTangent = 0.02,
        .ReferenceFrequencyHz = 1e9,
    };

    // 1. S-parameters against exact complex ABCD -> S conversion across 4 decades
    Nodalys::CircuitBuilder sp_builder(3);
    sp_builder.AddPort(1, 1, 50.0);
    sp_builder.AddPort(2, 2, 75.0);
    sp_builder.AddTransmissionLine("TL1", 1, 0, 2, 0, lossy);
    const auto sp_circuit = sp_builder.Build();
    REQUIRE(sp_circuit.has_value());

    const auto sp = Nodalys::ComputeSParameters(
        *sp_circuit,
        {.SweepType = Nodalys::FrequencySweepType::DECADE, .StartFrequency = 1e6, .StopFrequency = 10e9, .Points = 4});
    REQUIRE(sp.has_value());
    for (std::size_t index = 0; index < sp->Frequencies.size(); ++index) {
        const TwoPortScattering expected = AnalyticalScattering(lossy, sp->Frequencies[index], 50.0, 75.0);
        RequireClose(sp->Get(index, 1, 1), expected.S11, 1e-9);
        RequireClose(sp->Get(index, 2, 1), expected.S21, 1e-9);
    }

    // 2. AC sweep, AC sensitivity and thermal noise
    Nodalys::CircuitBuilder ac_builder(4);
    ac_builder.AddVdc("VIN", 1, 0, 0.0, 2.0, 0.0);
    ac_builder.AddResistor("RS", 1, 2, 50.0);
    ac_builder.AddTransmissionLine("TL1", 2, 0, 3, 0, lossy);
    ac_builder.AddResistor("RL", 3, 0, 50.0);
    const auto ac_circuit = ac_builder.Build();
    REQUIRE(ac_circuit.has_value());

    const auto ac = Nodalys::ComputeACSweep(*ac_circuit,
                                            {.StartFrequency = 10e6,
                                             .StopFrequency = 1e9,
                                             .PointsPerDecade = 3,
                                             .SweepType = Nodalys::FrequencySweepType::DECADE});
    REQUIRE(ac.has_value());
    for (std::size_t index = 0; index < ac->Frequencies.size(); ++index) {
        // With 2 V behind 50 ohm and a 50 ohm load, V(3) equals S21 against 50 ohm ports
        const TwoPortScattering expected = AnalyticalScattering(lossy, ac->Frequencies[index], 50.0, 50.0);
        RequireClose(AcPhasor(*ac, 3, index), expected.S21, 1e-8);
    }

    const auto ac_sens = Nodalys::ComputeAcSensitivity(*ac_circuit,
                                                       {.OutputPositiveNode = 3,
                                                        .OutputNegativeNode = 0,
                                                        .InputSource = "VIN",
                                                        .StartFrequency = 100e6,
                                                        .StopFrequency = 500e6,
                                                        .PointsPerDecade = 2,
                                                        .SweepType = Nodalys::FrequencySweepType::LINEAR});
    REQUIRE(ac_sens.has_value());
    CHECK_FALSE(ac_sens->Elements.empty());

    // Bosma's theorem: when port 1 is shorted by an ideal AC source (no external resistor noise) and port 2 is open,
    // the output voltage noise spectral density is sqrt(4 * k * T * Re(Z_out))
    Nodalys::CircuitBuilder noise_builder(3);
    noise_builder.AddVdc("VIN", 1, 0, 0.0, 1.0, 0.0);
    noise_builder.AddTransmissionLine("TL1", 1, 0, 2, 0, lossy);
    const auto noise_circuit = noise_builder.Build();
    REQUIRE(noise_circuit.has_value());

    const auto noise = Nodalys::ComputeNoise(*noise_circuit,
                                             {.OutputPositiveNode = 2,
                                              .OutputNegativeNode = 0,
                                              .InputSource = "VIN",
                                              .StartFrequency = 100e6,
                                              .StopFrequency = 1e9,
                                              .PointsPerDecade = 2,
                                              .SweepType = Nodalys::FrequencySweepType::LINEAR});
    REQUIRE(noise.has_value());
    for (std::size_t index = 0; index < noise->Frequencies.size(); ++index) {
        // Port 1 is shorted for AC noise (ideal voltage source VIN), so Z_out at port 2 is B / D = Zc * tanh(theta)
        const double frequency = noise->Frequencies[index];
        const double omega = TWO_PI * frequency;
        const double r_eff =
            lossy.DcResistanceOhms + lossy.SkinResistanceOhms * std::sqrt(frequency / lossy.ReferenceFrequencyHz);
        const double g_eff =
            lossy.DcConductanceSiemens + omega * (lossy.DelaySeconds / lossy.ImpedanceOhms) * lossy.LossTangent;
        const Complex z_tot(r_eff, omega * lossy.ImpedanceOhms * lossy.DelaySeconds);
        const Complex y_tot(g_eff, omega * (lossy.DelaySeconds / lossy.ImpedanceOhms));
        const Complex z_out = std::sqrt(z_tot / y_tot) * std::tanh(std::sqrt(z_tot * y_tot));
        const double temperature_kelvin = 300.15;
        const double expected_density = std::sqrt(4.0 * BOLTZMANN_CONSTANT * temperature_kelvin * z_out.real());
        CHECK_THAT(noise->OutputDensities[index], WithinRel(expected_density, 1e-5));
    }

    // 3. Linear and nonlinear Harmonic Balance with a dispersive lossy line
    Nodalys::CircuitBuilder hb_linear_builder(4);
    hb_linear_builder.AddVsine("VIN", 1, 0, {.Amplitude = 2.0, .Frequency = 200e6});
    hb_linear_builder.AddResistor("RS", 1, 2, 50.0);
    hb_linear_builder.AddTransmissionLine("TL1", 2, 0, 3, 0, lossy);
    hb_linear_builder.AddResistor("RL", 3, 0, 50.0);
    const auto hb_linear_circuit = hb_linear_builder.Build();
    REQUIRE(hb_linear_circuit.has_value());

    const auto hb_linear =
        Nodalys::ComputeHarmonicBalance(*hb_linear_circuit, {.FundamentalFrequency = 200e6, .Harmonics = 4});
    REQUIRE(hb_linear.has_value());
    // Sine(omega*t) = Re(-j * exp(j*omega*t)), so peak phasor of 2*sin(omega*t) is -2j and V3_1 = -j * S21
    const TwoPortScattering fundamental = AnalyticalScattering(lossy, 200e6, 50.0, 50.0);
    RequireClose(hb_linear->NodePhasors[3][1], Complex(0.0, -1.0) * fundamental.S21, 1e-8);

    Nodalys::CircuitBuilder hb_nonlinear_builder(4);
    hb_nonlinear_builder.AddVsine("VIN", 1, 0, {.Amplitude = 2.5, .Frequency = 200e6});
    hb_nonlinear_builder.AddResistor("RS", 1, 2, 50.0);
    hb_nonlinear_builder.AddTransmissionLine("TL1", 2, 0, 3, 0, lossy);
    hb_nonlinear_builder.AddResistor("RL", 3, 0, 200.0);
    hb_nonlinear_builder.AddDiode("D1", 3, 0, {});
    const auto hb_nonlinear_circuit = hb_nonlinear_builder.Build();
    REQUIRE(hb_nonlinear_circuit.has_value());

    const auto hb_nonlinear =
        Nodalys::ComputeHarmonicBalance(*hb_nonlinear_circuit, {.FundamentalFrequency = 200e6, .Harmonics = 8});
    REQUIRE(hb_nonlinear.has_value());
    // Diode clamping positive excursions produces a negative DC bias and nonzero 2nd harmonic on node 3
    CHECK(hb_nonlinear->NodePhasors[3][0].real() < -0.05);
    CHECK(std::abs(hb_nonlinear->NodePhasors[3][2]) > 0.02);

    // 4. Pole-Zero refuses all transmission lines; Transient refuses lossy dispersive lines
    const auto pz = Nodalys::ComputePoleZero(*ac_circuit, {.OutputNode = 3, .InputSource = "VIN"});
    REQUIRE_FALSE(pz.has_value());
    CHECK_THAT(pz.error(), ContainsSubstring("transmission line"));

    const auto tran_refused = Nodalys::ComputeTransient(*ac_circuit, {.StopTime = 10e-9, .TimeStep = 0.1e-9});
    REQUIRE_FALSE(tran_refused.has_value());
    CHECK_THAT(tran_refused.error(), ContainsSubstring("lossless transmission lines only"));
}

TEST_CASE("A lossless transmission line propagates pulses and reflections by Bergeron's method in transient",
          "[nodalys][transmission_line][transient]") {
    constexpr double Z0 = 50.0;
    constexpr double DELAY = 2e-9;
    const Nodalys::PulseParameters step_pulse{
        .Low = 0.0,
        .High = 2.0,
        .Delay = 0.5e-9,
        .RiseTime = 0.1e-9,
        .FallTime = 0.1e-9,
        .Width = 20e-9,
        .Period = 40e-9,
    };

    // 1. Matched source (50 ohm) and matched load (50 ohm): 2 V step launches 1 V at 0.5 ns, arrives at 2.5 ns, no echo
    {
        Nodalys::CircuitBuilder builder(4);
        builder.AddVpulse("VIN", 1, 0, step_pulse);
        builder.AddResistor("RS", 1, 2, Z0);
        builder.AddTransmissionLine("TL1", 2, 0, 3, 0, {.ImpedanceOhms = Z0, .DelaySeconds = DELAY});
        builder.AddResistor("RL", 3, 0, Z0);
        const auto circuit = builder.Build();
        REQUIRE(circuit.has_value());

        const auto tran = Nodalys::ComputeTransient(*circuit, {.StopTime = 8e-9, .TimeStep = 0.1e-9});
        REQUIRE(tran.has_value());
        CHECK_THAT(SampleAtOrAfter(*tran, 2, 1.2e-9), WithinAbs(1.0, 1e-3));
        CHECK_THAT(SampleAtOrAfter(*tran, 3, 1.2e-9), WithinAbs(0.0, 1e-3));
        CHECK_THAT(SampleAtOrAfter(*tran, 3, 3.2e-9), WithinAbs(1.0, 1e-3));
        CHECK_THAT(SampleAtOrAfter(*tran, 2, 6.0e-9), WithinAbs(1.0, 1e-3));
    }

    // 2. Matched source (50 ohm) and open load: 1 V wave doubles to 2 V at Td = 2 ns, reaches source at 2*Td = 4 ns
    {
        Nodalys::CircuitBuilder builder(4);
        builder.AddVpulse("VIN", 1, 0, step_pulse);
        builder.AddResistor("RS", 1, 2, Z0);
        builder.AddTransmissionLine("TL1", 2, 0, 3, 0, {.ImpedanceOhms = Z0, .DelaySeconds = DELAY});
        const auto circuit = builder.Build();
        REQUIRE(circuit.has_value());

        const auto tran = Nodalys::ComputeTransient(*circuit, {.StopTime = 8e-9, .TimeStep = 0.1e-9});
        REQUIRE(tran.has_value());
        CHECK_THAT(SampleAtOrAfter(*tran, 2, 1.5e-9), WithinAbs(1.0, 1e-3));
        CHECK_THAT(SampleAtOrAfter(*tran, 3, 1.5e-9), WithinAbs(0.0, 1e-3));
        CHECK_THAT(SampleAtOrAfter(*tran, 3, 3.2e-9), WithinAbs(2.0, 1e-3));
        CHECK_THAT(SampleAtOrAfter(*tran, 2, 3.2e-9), WithinAbs(1.0, 1e-3));
        CHECK_THAT(SampleAtOrAfter(*tran, 2, 5.2e-9), WithinAbs(2.0, 1e-3));
    }

    // 3. Matched source (50 ohm) and 150 ohm load (Gamma_L = +0.5): far end steps to 1.5 V at Td, near end at 2*Td
    {
        Nodalys::CircuitBuilder builder(4);
        builder.AddVpulse("VIN", 1, 0, step_pulse);
        builder.AddResistor("RS", 1, 2, Z0);
        builder.AddTransmissionLine("TL1", 2, 0, 3, 0, {.ImpedanceOhms = Z0, .DelaySeconds = DELAY});
        builder.AddResistor("RL", 3, 0, 150.0);
        const auto circuit = builder.Build();
        REQUIRE(circuit.has_value());

        const auto tran = Nodalys::ComputeTransient(*circuit, {.StopTime = 8e-9, .TimeStep = 0.1e-9});
        REQUIRE(tran.has_value());
        CHECK_THAT(SampleAtOrAfter(*tran, 3, 3.2e-9), WithinAbs(1.5, 1e-3));
        CHECK_THAT(SampleAtOrAfter(*tran, 2, 5.2e-9), WithinAbs(1.5, 1e-3));
    }

    // 4. Picosecond interconnect in a microsecond run (TimeStep >> Td) stays stable via semi-implicit fallback
    {
        Nodalys::CircuitBuilder builder(4);
        builder.AddVpulse("VIN",
                          1,
                          0,
                          {.Low = 0.0,
                           .High = 4.0,
                           .Delay = 1e-6,
                           .RiseTime = 0.1e-6,
                           .FallTime = 0.1e-6,
                           .Width = 20e-6,
                           .Period = 40e-6});
        builder.AddResistor("RS", 1, 2, 50.0);
        builder.AddTransmissionLine("TLSHORT", 2, 0, 3, 0, {.ImpedanceOhms = 50.0, .DelaySeconds = 5e-12});
        builder.AddResistor("RL", 3, 0, 150.0);
        const auto circuit = builder.Build();
        REQUIRE(circuit.has_value());

        const auto tran = Nodalys::ComputeTransient(*circuit, {.StopTime = 10e-6, .TimeStep = 0.2e-6});
        REQUIRE(tran.has_value());
        CHECK_THAT(SampleAtOrAfter(*tran, 3, 5e-6), WithinAbs(3.0, 1e-3));
    }
}
