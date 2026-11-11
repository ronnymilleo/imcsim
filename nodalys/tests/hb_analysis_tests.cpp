/**
 * @file    hb_analysis_tests.cpp
 * @brief   Unit, cross-validation, and benchmark tests for Harmonic Balance (.hb) analysis.
 */

#include "linear/fft.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <nodalys/nodalys.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <format>
#include <iostream>
#include <numbers>
#include <string>
#include <vector>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

std::vector<double> InterpolateUniformPeriod(const std::vector<double> &times, const std::vector<double> &values,
                                             const double t_start, const double period, const std::size_t num_samples) {
    REQUIRE(times.size() == values.size());
    REQUIRE(times.size() >= 2);

    std::vector<double> samples(num_samples, 0.0);
    std::size_t cursor = 0;

    for (std::size_t m = 0; m < num_samples; ++m) {
        const double t = t_start + (static_cast<double>(m) / static_cast<double>(num_samples)) * period;
        while (cursor + 1 < times.size() && times[cursor + 1] < t) {
            ++cursor;
        }
        if (t <= times.front()) {
            samples[m] = values.front();
        } else if (cursor + 1 >= times.size()) {
            samples[m] = values.back();
        } else {
            const double dt = times[cursor + 1] - times[cursor];
            const double alpha = (dt > 0.0) ? (t - times[cursor]) / dt : 0.0;
            samples[m] = values[cursor] + alpha * (values[cursor + 1] - values[cursor]);
        }
    }
    return samples;
}

std::vector<std::complex<double>> ExtractTransientHarmonics(const std::vector<double> &times,
                                                            const std::vector<double> &values, const double t_start,
                                                            const double period, const std::size_t fft_samples,
                                                            const std::size_t max_harmonic) {
    const auto uniform = InterpolateUniformPeriod(times, values, t_start, period, fft_samples);
    std::vector<double> coeffs;
    REQUIRE(nodalys::RealToHarmonics(uniform, static_cast<int>(max_harmonic), coeffs));
    std::vector<std::complex<double>> phasors(max_harmonic + 1);
    phasors[0] = {coeffs[0], 0.0};
    for (std::size_t k = 1; k <= max_harmonic; ++k) {
        phasors[k] = {coeffs[2 * k - 1], -coeffs[2 * k]};
    }
    return phasors;
}

const nodalys::ComponentPhasors *FindCurrentPhasor(const nodalys::HarmonicBalanceResult &hb, std::string_view name) {
    for (const auto &cur : hb.CurrentPhasors) {
        if (cur.Name == name) {
            return &cur;
        }
    }
    return nullptr;
}

const nodalys::ComponentTrace *FindCurrentTrace(const nodalys::TransientResult &tran, std::string_view name) {
    for (const auto &cur : tran.Currents) {
        if (cur.Name == name) {
            return &cur;
        }
    }
    return nullptr;
}

TEST_CASE("Harmonic Balance validates configuration, waveforms, and unsupported components", "[nodalys][hb]") {
    SECTION("Rejects invalid config parameters") {
        nodalys::CircuitBuilder b;
        b.AddVsine("V1", 1, 0, {.Amplitude = 1.0, .Frequency = 1e3});
        b.AddResistor("R1", 1, 0, 1e3);
        auto circuit = b.Build();
        REQUIRE(circuit.has_value());

        CHECK_FALSE(
            nodalys::ComputeHarmonicBalance(*circuit, {.FundamentalFrequency = 0.0, .Harmonics = 4}).has_value());
        CHECK_FALSE(
            nodalys::ComputeHarmonicBalance(*circuit, {.FundamentalFrequency = -1e3, .Harmonics = 4}).has_value());
        CHECK_FALSE(
            nodalys::ComputeHarmonicBalance(*circuit, {.FundamentalFrequency = 1e3, .Harmonics = -1}).has_value());
        // Non-power-of-two sample count
        CHECK_FALSE(
            nodalys::ComputeHarmonicBalance(*circuit, {.FundamentalFrequency = 1e3, .Harmonics = 4, .Samples = 24})
                .has_value());
        // Samples <= 2*H fails Nyquist margin
        CHECK_FALSE(
            nodalys::ComputeHarmonicBalance(*circuit, {.FundamentalFrequency = 1e3, .Harmonics = 8, .Samples = 16})
                .has_value());
    }

    SECTION("Refuses voltage-controlled switches in Harmonic Balance") {
        const std::string_view netlist = "* Switch test\n"
                                         "V1 1 0 SIN(0 5 1k)\n"
                                         "S1 1 2 1 0 SWMOD\n"
                                         "R1 2 0 1k\n"
                                         ".model SWMOD SW(Vt=2.5 Vh=0.2 Ron=1 Roff=1Meg)\n"
                                         ".end\n";
        const auto parsed = nodalys::ParseNetlist(netlist);
        REQUIRE(parsed.has_value());

        const auto res =
            nodalys::ComputeHarmonicBalance(parsed->CircuitInstance, {.FundamentalFrequency = 1e3, .Harmonics = 4});
        CHECK_FALSE(res.has_value());
    }

    SECTION("Refuses incommensurate, damped SIN, EXP, and PWL waveforms") {
        // Incommensurate source frequency (1.5 kHz with f0 = 1 kHz)
        {
            const auto parsed = nodalys::ParseNetlist("* Incommensurate\nV1 1 0 SIN(0 1 1.5k)\nR1 1 0 1k\n.end\n");
            REQUIRE(parsed.has_value());
            CHECK_FALSE(
                nodalys::ComputeHarmonicBalance(parsed->CircuitInstance, {.FundamentalFrequency = 1e3, .Harmonics = 4})
                    .has_value());
        }

        // Source frequency above H * f0
        {
            const auto parsed = nodalys::ParseNetlist("* Above H\nV1 1 0 SIN(0 1 6k)\nR1 1 0 1k\n.end\n");
            REQUIRE(parsed.has_value());
            CHECK_FALSE(
                nodalys::ComputeHarmonicBalance(parsed->CircuitInstance, {.FundamentalFrequency = 1e3, .Harmonics = 5})
                    .has_value());
        }

        // Damped SIN (theta != 0)
        {
            const auto parsed = nodalys::ParseNetlist("* Damped SIN\nV1 1 0 SIN(0 5 1k 0 50)\nR1 1 0 1k\n.end\n");
            REQUIRE(parsed.has_value());
            CHECK_FALSE(
                nodalys::ComputeHarmonicBalance(parsed->CircuitInstance, {.FundamentalFrequency = 1e3, .Harmonics = 4})
                    .has_value());
        }

        // EXP source
        {
            const auto parsed = nodalys::ParseNetlist("* EXP\nV1 1 0 EXP(0 5 0 1u 10u 2u 20u)\nR1 1 0 1k\n.end\n");
            REQUIRE(parsed.has_value());
            CHECK_FALSE(
                nodalys::ComputeHarmonicBalance(parsed->CircuitInstance, {.FundamentalFrequency = 1e3, .Harmonics = 4})
                    .has_value());
        }

        // PWL source
        {
            const auto parsed = nodalys::ParseNetlist("* PWL\nV1 1 0 PWL(0 0 1m 5)\nR1 1 0 1k\n.end\n");
            REQUIRE(parsed.has_value());
            CHECK_FALSE(
                nodalys::ComputeHarmonicBalance(parsed->CircuitInstance, {.FundamentalFrequency = 1e3, .Harmonics = 4})
                    .has_value());
        }
    }

    SECTION("Warns when PULSE edge is narrower than sampling step T0/M") {
        const auto parsed = nodalys::ParseNetlist("* Narrow pulse edge\n"
                                                  "V1 1 0 PULSE(0 5 0 1u 1u 500u 1m)\n"
                                                  "R1 1 0 1k\n"
                                                  ".end\n");
        REQUIRE(parsed.has_value());
        // f0 = 1 kHz -> T0 = 1 ms, Samples = 32 -> dt = 31.25 us > 1 us
        const auto hb = nodalys::ComputeHarmonicBalance(parsed->CircuitInstance,
                                                        {.FundamentalFrequency = 1e3, .Harmonics = 4, .Samples = 32});
        REQUIRE(hb.has_value());
        CHECK(!hb->Warnings.empty());
    }
}

TEST_CASE("Harmonic Balance matches AC analysis and closed-form phasors on linear circuits", "[nodalys][hb]") {
    SECTION("RC low-pass filter phasors and reconstructed waveform") {
        constexpr double R = 1000.0;
        constexpr double C = 1e-6;
        constexpr double F0 = 1e3 / (2.0 * std::numbers::pi); // fc = 1/(2*pi*R*C) = 1000/(2*pi) Hz so w0*R*C = 1
        constexpr double VIN_PEAK = 5.0;
        constexpr double VIN_DC = 2.0;

        nodalys::CircuitBuilder b_hb;
        b_hb.AddVsine("V1", 1, 0, {.Amplitude = VIN_PEAK, .Frequency = F0, .Offset = VIN_DC});
        b_hb.AddResistor("R1", 1, 2, R);
        b_hb.AddCapacitor("C1", 2, 0, C);
        auto hb_circuit = b_hb.Build();
        REQUIRE(hb_circuit.has_value());

        const auto hb =
            nodalys::ComputeHarmonicBalance(*hb_circuit, {.FundamentalFrequency = F0, .Harmonics = 5, .Samples = 64});
        REQUIRE(hb.has_value());
        CHECK(hb->Strategy == "linear");
        CHECK(hb->Frequencies.size() == 6);
        CHECK(hb->Times.size() == 64);

        // V1(t) = 2 + 5 * sin(w0*t) => DC phasor = 2, k=1 phasor = -j * 5
        CHECK_THAT(hb->NodePhasors[1][0].real(), WithinRel(VIN_DC, 1e-9));
        CHECK_THAT(hb->NodePhasors[1][1].real(), WithinAbs(0.0, 1e-9));
        CHECK_THAT(hb->NodePhasors[1][1].imag(), WithinRel(-VIN_PEAK, 1e-9));

        // Node 2 DC = 2 V (capacitor is open at DC)
        CHECK_THAT(hb->NodePhasors[2][0].real(), WithinRel(VIN_DC, 1e-9));
        CHECK_THAT(hb->NodePhasors[2][0].imag(), WithinAbs(0.0, 1e-12));

        // At w0*R*C = 1, H(j*w0) = 1 / (1 + j) = 0.5 - j*0.5
        // Input phasor = -j*5 => V2(1) = (-j*5) * (0.5 - j*0.5) = -2.5 - j*2.5
        const std::complex<double> expected_v2_1(-2.5, -2.5);
        CHECK_THAT(hb->NodePhasors[2][1].real(), WithinRel(expected_v2_1.real(), 1e-9));
        CHECK_THAT(hb->NodePhasors[2][1].imag(), WithinRel(expected_v2_1.imag(), 1e-9));

        // Higher harmonics must be zero
        for (std::size_t h = 2; h <= 5; ++h) {
            CHECK_THAT(std::abs(hb->NodePhasors[2][h]), WithinAbs(0.0, 1e-11));
        }

        // Cross-check against .ac at F0
        const std::string ac_netlist = std::format("* RC AC reference\n"
                                                   "V1 1 0 AC {} -90\n"
                                                   "R1 1 2 1000\n"
                                                   "C1 2 0 1u\n"
                                                   ".ac lin 1 {:.12g} {:.12g}\n"
                                                   ".end\n",
                                                   VIN_PEAK, F0, F0);
        const auto parsed_ac = nodalys::ParseNetlist(ac_netlist);
        REQUIRE(parsed_ac.has_value());
        const auto &ac_cfg = std::get<nodalys::AcSweepConfig>(parsed_ac->Analyses[0].Config);
        const auto ac = nodalys::ComputeACSweep(parsed_ac->CircuitInstance, ac_cfg);
        REQUIRE(ac.has_value());
        const double ac_mag = std::pow(10.0, ac->NodeMagnitudesDecibels[2][0] / 20.0);
        const double ac_phase = ac->NodePhasesDegrees[2][0] * std::numbers::pi / 180.0;
        // .ac source has 0-deg reference (cos(w0 t)), whereas SIN(0 A f0) is -j * A (sin(w0 t))
        const std::complex<double> ac_v2 = std::polar(ac_mag, ac_phase) * std::complex<double>(0.0, -1.0);
        CHECK_THAT(hb->NodePhasors[2][1].real(), WithinRel(ac_v2.real(), 1e-8));
        CHECK_THAT(hb->NodePhasors[2][1].imag(), WithinRel(ac_v2.imag(), 1e-8));

        // Check component current phasors I(R1) = I(C1) = (V1 - V2) / R
        const auto *i_r1 = FindCurrentPhasor(*hb, "R1");
        const auto *i_c1 = FindCurrentPhasor(*hb, "C1");
        REQUIRE(i_r1 != nullptr);
        REQUIRE(i_c1 != nullptr);
        const std::complex<double> expected_i = (hb->NodePhasors[1][1] - hb->NodePhasors[2][1]) / R;
        CHECK_THAT(i_r1->Phasors[1].real(), WithinRel(expected_i.real(), 1e-9));
        CHECK_THAT(i_r1->Phasors[1].imag(), WithinRel(expected_i.imag(), 1e-9));
        CHECK_THAT(i_c1->Phasors[1].real(), WithinRel(expected_i.real(), 1e-9));
        CHECK_THAT(i_c1->Phasors[1].imag(), WithinRel(expected_i.imag(), 1e-9));

        // Verify reconstructed waveform matches v2(t) = 2 - 2.5*cos(w0 t) + 2.5*sin(w0 t)
        const double period = 1.0 / F0;
        for (std::size_t m = 0; m < hb->Times.size(); ++m) {
            const double phase = 2.0 * std::numbers::pi * hb->Times[m] / period;
            const double exact = VIN_DC - 2.5 * std::cos(phase) + 2.5 * std::sin(phase);
            CHECK_THAT(hb->NodeWaveforms[2][m], WithinAbs(exact, 1e-9));
        }

        // Pure linear sine response must have near-zero THD
        CHECK_THAT(nodalys::ComputeThd(hb->NodePhasors[2]), WithinAbs(0.0, 1e-11));
    }

    SECTION("High-Q series RLC resonant tank matches closed-form resonance") {
        // R = 1 Ohm, f0 = 1 kHz, Q = 50 => w0*L = Q*R = 50 Ohm, L = 50 / (2*pi*1e3), C = 1 / (w0 * 50)
        constexpr double F0 = 1000.0;
        constexpr double W0 = 2.0 * std::numbers::pi * F0;
        constexpr double R = 1.0;
        constexpr double Q = 50.0;
        constexpr double L = (Q * R) / W0;
        constexpr double C = 1.0 / (W0 * Q * R);

        nodalys::CircuitBuilder b;
        b.AddVsine("V1", 1, 0, {.Amplitude = 2.0, .Frequency = F0});
        b.AddResistor("R1", 1, 2, R);
        b.AddInductor("L1", 2, 3, L);
        b.AddCapacitor("C1", 3, 0, C);
        auto circuit = b.Build();
        REQUIRE(circuit.has_value());

        const auto hb =
            nodalys::ComputeHarmonicBalance(*circuit, {.FundamentalFrequency = F0, .Harmonics = 3, .Samples = 32});
        REQUIRE(hb.has_value());
        // At series resonance, current amplitude = V1 / R = 2 A, and |Vc| = Q * V1 = 100 V
        CHECK_THAT(std::abs(hb->NodePhasors[3][1]), WithinRel(Q * 2.0, 1e-8));
        const auto *i_l1 = FindCurrentPhasor(*hb, "L1");
        REQUIRE(i_l1 != nullptr);
        CHECK_THAT(std::abs(i_l1->Phasors[1]), WithinRel(2.0, 1e-8));
    }

    SECTION("Multi-tone superposition of fundamental and 3rd harmonic sources") {
        const std::string_view netlist = "* Multi-tone RC network\n"
                                         "V1 1 0 SIN(1.5 4.0 1k)\n"
                                         "I3 0 2 SIN(0 2m 3k)\n"
                                         "R1 1 2 1k\n"
                                         "R2 2 0 1k\n"
                                         "C1 2 0 100n\n"
                                         ".hb 1k 5 samples=64\n"
                                         ".end\n";
        const auto parsed = nodalys::ParseNetlist(netlist);
        REQUIRE(parsed.has_value());
        const auto &cfg = std::get<nodalys::HarmonicBalanceConfig>(parsed->Analyses[0].Config);
        const auto hb = nodalys::ComputeHarmonicBalance(parsed->CircuitInstance, cfg);
        REQUIRE(hb.has_value());

        // DC at node 2: R1=1k, R2=1k divider from V1_dc=1.5 => 0.75 V
        CHECK_THAT(hb->NodePhasors[2][0].real(), WithinRel(0.75, 1e-9));

        // Harmonic 1 (1 kHz): only V1 active -> V2(1) = V1(1) * Z2 / (R1 + Z2) where Z2 = 1 / (1/1k + j*w1*C1)
        const double w1 = 2.0 * std::numbers::pi * 1e3;
        const std::complex<double> y2_1(1.0 / 1e3, w1 * 100e-9);
        const std::complex<double> z2_1 = 1.0 / y2_1;
        const std::complex<double> v1_1(0.0, -4.0);
        const std::complex<double> exp_v2_1 = v1_1 * z2_1 / (1e3 + z2_1);
        CHECK_THAT(hb->NodePhasors[2][1].real(), WithinRel(exp_v2_1.real(), 1e-8));
        CHECK_THAT(hb->NodePhasors[2][1].imag(), WithinRel(exp_v2_1.imag(), 1e-8));

        // Harmonic 3 (3 kHz): only I3 (2 mA peak sine into node 2) active -> V2(3) = I3(3) / (1/1k + 1/1k + j*w3*C1)
        const double w3 = 2.0 * std::numbers::pi * 3e3;
        const std::complex<double> y_total_3(2.0 / 1e3, w3 * 100e-9);
        const std::complex<double> i3_3(0.0, -2e-3);
        const std::complex<double> exp_v2_3 = i3_3 / y_total_3;
        CHECK_THAT(hb->NodePhasors[2][3].real(), WithinRel(exp_v2_3.real(), 1e-8));
        CHECK_THAT(hb->NodePhasors[2][3].imag(), WithinRel(exp_v2_3.imag(), 1e-8));
    }
}

TEST_CASE("Harmonic Balance with H=0 reproduces DC operating point on nonlinear circuits", "[nodalys][hb]") {
    const std::string_view netlist = "* Diode + resistor DC bias\n"
                                     "V1 1 0 5V\n"
                                     "R1 1 2 1k\n"
                                     "D1 2 0 DMOD\n"
                                     ".model DMOD D(IS=1e-14 N=1.0 RS=0.5)\n"
                                     ".end\n";
    const auto parsed = nodalys::ParseNetlist(netlist);
    REQUIRE(parsed.has_value());

    const auto op = nodalys::ComputeOperatingPoint(parsed->CircuitInstance);
    REQUIRE(op.has_value());

    const auto hb =
        nodalys::ComputeHarmonicBalance(parsed->CircuitInstance, {.FundamentalFrequency = 1e3, .Harmonics = 0});
    REQUIRE(hb.has_value());
    REQUIRE(hb->Frequencies.size() == 1);

    CHECK_THAT(hb->NodePhasors[1][0].real(), WithinRel(op->NodeVoltages[1], 1e-6));
    CHECK_THAT(hb->NodePhasors[2][0].real(), WithinRel(op->NodeVoltages[2], 1e-6));
    const auto *i_d1 = FindCurrentPhasor(*hb, "D1");
    REQUIRE(i_d1 != nullptr);
    double op_d1 = 0.0;
    for (const auto &c : op->Currents) {
        if (c.Name == "D1") {
            op_d1 = c.Current;
        }
    }
    CHECK_THAT(i_d1->Phasors[0].real(), WithinRel(op_d1, 1e-6));
}

TEST_CASE("Harmonic Balance matches steady-state transient FFT on nonlinear diode circuits", "[nodalys][hb]") {
    SECTION("Half-wave diode rectifier with RC load") {
        const std::string_view netlist = "* Half-wave diode rectifier\n"
                                         "V1 1 0 SIN(0 5 1k)\n"
                                         "D1 1 2 DMOD\n"
                                         "R1 2 0 1k\n"
                                         "C1 2 0 2u\n"
                                         ".model DMOD D(IS=1e-12 N=1.1 RS=1.0)\n"
                                         ".end\n";
        const auto parsed = nodalys::ParseNetlist(netlist);
        REQUIRE(parsed.has_value());

        constexpr double F0 = 1e3;
        constexpr double T0 = 1.0 / F0;
        constexpr int H = 24;
        constexpr int M = 256;

        const auto hb = nodalys::ComputeHarmonicBalance(parsed->CircuitInstance,
                                                        {.FundamentalFrequency = F0, .Harmonics = H, .Samples = M});
        REQUIRE(hb.has_value());

        // tau = R*C = 2 ms = 2*T0 -> 26 ms = 13*tau decays initial transient well below 1e-5
        nodalys::TransientConfig tran_cfg{
            .StopTime = 26.0 * T0, .TimeStep = T0 / 2048.0, .MaxStep = T0 / 2048.0, .UseInitialConditions = false};
        const auto tran = nodalys::ComputeTransient(parsed->CircuitInstance, tran_cfg);
        REQUIRE(tran.has_value());

        const auto tran_v2 = ExtractTransientHarmonics(tran->Times, tran->NodeVoltages[2], 25.0 * T0, T0, 512, H);
        const auto *tran_d1 = FindCurrentTrace(*tran, "D1");
        REQUIRE(tran_d1 != nullptr);
        const auto tran_i_d1 = ExtractTransientHarmonics(tran->Times, tran_d1->Values, 25.0 * T0, T0, 512, H);

        // Rectified DC voltage across load is > 3V
        CHECK(hb->NodePhasors[2][0].real() > 3.0);
        CHECK_THAT(hb->NodePhasors[2][0].real(), WithinRel(tran_v2[0].real(), 2e-3));
        CHECK_THAT(std::abs(hb->NodePhasors[2][1]), WithinRel(std::abs(tran_v2[1]), 5e-3));
        CHECK_THAT(hb->NodePhasors[2][1].real(), WithinAbs(tran_v2[1].real(), 3e-3));
        CHECK_THAT(hb->NodePhasors[2][1].imag(), WithinAbs(tran_v2[1].imag(), 3e-3));
        CHECK_THAT(std::abs(hb->NodePhasors[2][2]), WithinAbs(std::abs(tran_v2[2]), 3e-3));
        CHECK_THAT(std::abs(hb->NodePhasors[2][3]), WithinAbs(std::abs(tran_v2[3]), 3e-3));

        const auto *hb_d1 = FindCurrentPhasor(*hb, "D1");
        REQUIRE(hb_d1 != nullptr);
        CHECK_THAT(hb_d1->Phasors[0].real(), WithinRel(tran_i_d1[0].real(), 3e-3));
        CHECK_THAT(std::abs(hb_d1->Phasors[1]), WithinAbs(std::abs(tran_i_d1[1]), 1e-4));

        const double hb_thd = nodalys::ComputeThd(hb->NodePhasors[2]);
        const double tran_thd = nodalys::ComputeThd(tran_v2);
        CHECK_THAT(hb_thd, WithinAbs(tran_thd, 5e-3));
    }

    SECTION("Symmetric anti-parallel diode clipper suppresses even harmonics") {
        const std::string_view netlist = "* Symmetric diode clipper\n"
                                         "V1 1 0 SIN(0 4 1k)\n"
                                         "R1 1 2 1k\n"
                                         "D1 2 0 DMOD\n"
                                         "D2 0 2 DMOD\n"
                                         ".model DMOD D(IS=1e-13 N=1.0 RS=1.0)\n"
                                         ".end\n";
        const auto parsed = nodalys::ParseNetlist(netlist);
        REQUIRE(parsed.has_value());

        constexpr double F0 = 1e3;
        constexpr double T0 = 1.0 / F0;
        const auto hb = nodalys::ComputeHarmonicBalance(parsed->CircuitInstance,
                                                        {.FundamentalFrequency = F0, .Harmonics = 25, .Samples = 256});
        REQUIRE(hb.has_value());

        // Symmetry: DC and even harmonics are zero
        CHECK_THAT(hb->NodePhasors[2][0].real(), WithinAbs(0.0, 1e-9));
        CHECK_THAT(std::abs(hb->NodePhasors[2][2]), WithinAbs(0.0, 1e-9));
        CHECK_THAT(std::abs(hb->NodePhasors[2][4]), WithinAbs(0.0, 1e-9));
        // Strong odd harmonics due to diode clipping
        CHECK(std::abs(hb->NodePhasors[2][3]) > 0.15);
        CHECK(std::abs(hb->NodePhasors[2][5]) > 0.05);

        const auto tran = nodalys::ComputeTransient(
            parsed->CircuitInstance, {.StopTime = 4.0 * T0, .TimeStep = T0 / 1024.0, .MaxStep = T0 / 1024.0});
        REQUIRE(tran.has_value());
        const auto tran_v2 = ExtractTransientHarmonics(tran->Times, tran->NodeVoltages[2], 3.0 * T0, T0, 512, 25);

        CHECK_THAT(hb->NodePhasors[2][1].real(), WithinAbs(tran_v2[1].real(), 2e-3));
        CHECK_THAT(hb->NodePhasors[2][1].imag(), WithinAbs(tran_v2[1].imag(), 2e-3));
        CHECK_THAT(hb->NodePhasors[2][3].real(), WithinAbs(tran_v2[3].real(), 2e-3));
        CHECK_THAT(hb->NodePhasors[2][3].imag(), WithinAbs(tran_v2[3].imag(), 2e-3));
    }

    SECTION("Current-driven parallel diode RC network exercises periodic current sources") {
        const std::string_view netlist = "* Sinusoidal current source driving diode + RC\n"
                                         "I1 0 1 SIN(5m 10m 2k)\n"
                                         "R1 1 0 1k\n"
                                         "C1 1 0 200n\n"
                                         "D1 1 0 DMOD\n"
                                         ".model DMOD D(IS=1e-13 N=1.0)\n"
                                         ".end\n";
        const auto parsed = nodalys::ParseNetlist(netlist);
        REQUIRE(parsed.has_value());

        constexpr double F0 = 2e3;
        constexpr double T0 = 1.0 / F0;
        const auto hb = nodalys::ComputeHarmonicBalance(parsed->CircuitInstance,
                                                        {.FundamentalFrequency = F0, .Harmonics = 24, .Samples = 256});
        REQUIRE(hb.has_value());

        const auto tran = nodalys::ComputeTransient(
            parsed->CircuitInstance, {.StopTime = 12.0 * T0, .TimeStep = T0 / 1024.0, .MaxStep = T0 / 1024.0});
        REQUIRE(tran.has_value());
        const auto tran_v1 = ExtractTransientHarmonics(tran->Times, tran->NodeVoltages[1], 11.0 * T0, T0, 512, 24);

        CHECK_THAT(hb->NodePhasors[1][0].real(), WithinAbs(tran_v1[0].real(), 3e-3));
        CHECK_THAT(hb->NodePhasors[1][1].real(), WithinAbs(tran_v1[1].real(), 3e-3));
        CHECK_THAT(hb->NodePhasors[1][1].imag(), WithinAbs(tran_v1[1].imag(), 3e-3));
        CHECK_THAT(std::abs(hb->NodePhasors[1][2]), WithinAbs(std::abs(tran_v1[2]), 3e-3));
    }

    SECTION("Delayed PULSE waveform wraps modulo period cleanly in steady state") {
        // Delay = 1.25*T0 wraps modulo T0 to 0.25*T0
        const std::string_view netlist = "* Delayed pulse RC circuit\n"
                                         "V1 1 0 PULSE(0 5 1.25m 50u 50u 250u 1m)\n"
                                         "R1 1 2 1k\n"
                                         "D1 2 0 DMOD\n"
                                         "C1 2 0 100n\n"
                                         ".model DMOD D(IS=1e-14 N=1.0)\n"
                                         ".end\n";
        const auto parsed = nodalys::ParseNetlist(netlist);
        REQUIRE(parsed.has_value());

        constexpr double F0 = 1e3;
        constexpr double T0 = 1.0 / F0;
        const auto hb = nodalys::ComputeHarmonicBalance(parsed->CircuitInstance,
                                                        {.FundamentalFrequency = F0, .Harmonics = 24, .Samples = 256});
        REQUIRE(hb.has_value());

        // Run past 1.25*T0 + several RC time constants so steady state is reached
        const auto tran = nodalys::ComputeTransient(
            parsed->CircuitInstance, {.StopTime = 8.0 * T0, .TimeStep = T0 / 1024.0, .MaxStep = T0 / 1024.0});
        REQUIRE(tran.has_value());
        const auto tran_v2 = ExtractTransientHarmonics(tran->Times, tran->NodeVoltages[2], 7.0 * T0, T0, 512, 24);

        CHECK_THAT(hb->NodePhasors[2][0].real(), WithinAbs(tran_v2[0].real(), 5e-3));
        CHECK_THAT(std::abs(hb->NodePhasors[2][1]), WithinAbs(std::abs(tran_v2[1]), 1e-2));
    }
}

TEST_CASE("Harmonic Balance matches steady-state transient FFT on BJT and MOSFET nonlinear circuits", "[nodalys][hb]") {
    SECTION("BJT common-emitter amplifier driven into saturation and distortion") {
        const std::string_view netlist = "* BJT large-signal common-emitter amplifier\n"
                                         "VCC 1 0 12V\n"
                                         "VIN 2 0 SIN(0.85 0.45 1k)\n"
                                         "RB1 1 3 47k\n"
                                         "RB2 3 0 6.8k\n"
                                         "RIN 2 3 1k\n"
                                         "RC 1 4 2.2k\n"
                                         "RE 5 0 220\n"
                                         "Q1 4 3 5 QNPN\n"
                                         ".model QNPN NPN(IS=1e-14 BF=150 VAF=80 CJE=10p CJC=5p)\n"
                                         ".end\n";
        const auto parsed = nodalys::ParseNetlist(netlist);
        REQUIRE(parsed.has_value());

        constexpr double F0 = 1e3;
        constexpr double T0 = 1.0 / F0;
        const auto hb = nodalys::ComputeHarmonicBalance(parsed->CircuitInstance,
                                                        {.FundamentalFrequency = F0, .Harmonics = 16, .Samples = 128});
        REQUIRE(hb.has_value());

        const auto tran = nodalys::ComputeTransient(
            parsed->CircuitInstance, {.StopTime = 5.0 * T0, .TimeStep = T0 / 1024.0, .MaxStep = T0 / 1024.0});
        REQUIRE(tran.has_value());
        const auto tran_v4 = ExtractTransientHarmonics(tran->Times, tran->NodeVoltages[4], 4.0 * T0, T0, 512, 16);

        // Large distortion creates significant 2nd harmonic (~0.19 V)
        CHECK(std::abs(hb->NodePhasors[4][2]) > 0.15);
        CHECK_THAT(hb->NodePhasors[4][0].real(), WithinRel(tran_v4[0].real(), 2e-3));
        CHECK_THAT(hb->NodePhasors[4][1].real(), WithinAbs(tran_v4[1].real(), 5e-3));
        CHECK_THAT(hb->NodePhasors[4][1].imag(), WithinAbs(tran_v4[1].imag(), 5e-3));
        CHECK_THAT(hb->NodePhasors[4][2].real(), WithinAbs(tran_v4[2].real(), 5e-3));
        CHECK_THAT(hb->NodePhasors[4][2].imag(), WithinAbs(tran_v4[2].imag(), 5e-3));
        CHECK_THAT(std::abs(hb->NodePhasors[4][3]), WithinAbs(std::abs(tran_v4[3]), 5e-3));
        CHECK_THAT(nodalys::ComputeThd(hb->NodePhasors[4]), WithinAbs(nodalys::ComputeThd(tran_v4), 5e-3));
    }

    SECTION("MOSFET common-source stage with overlap capacitance Cgs and Cgd") {
        const std::string_view netlist = "* NMOS common-source large-signal amplifier\n"
                                         "VDD 1 0 10V\n"
                                         "VG 2 0 SIN(2.8 1.5 10k)\n"
                                         "RG 2 3 1k\n"
                                         "RD 1 4 1k\n"
                                         "CL 4 0 50p\n"
                                         "M1 4 3 0 0 NMOSM\n"
                                         ".model NMOSM NMOS(VTO=1.5 KP=5m LAMBDA=0.02 CGS=20p CGD=10p)\n"
                                         ".end\n";
        const auto parsed = nodalys::ParseNetlist(netlist);
        REQUIRE(parsed.has_value());

        constexpr double F0 = 10e3;
        constexpr double T0 = 1.0 / F0;
        const auto hb = nodalys::ComputeHarmonicBalance(parsed->CircuitInstance,
                                                        {.FundamentalFrequency = F0, .Harmonics = 16, .Samples = 128});
        REQUIRE(hb.has_value());

        const auto tran = nodalys::ComputeTransient(
            parsed->CircuitInstance, {.StopTime = 6.0 * T0, .TimeStep = T0 / 1024.0, .MaxStep = T0 / 1024.0});
        REQUIRE(tran.has_value());
        const auto tran_v4 = ExtractTransientHarmonics(tran->Times, tran->NodeVoltages[4], 5.0 * T0, T0, 512, 16);

        CHECK(std::abs(hb->NodePhasors[4][2]) > 0.05);
        CHECK_THAT(hb->NodePhasors[4][0].real(), WithinRel(tran_v4[0].real(), 1e-3));
        CHECK_THAT(hb->NodePhasors[4][1].real(), WithinAbs(tran_v4[1].real(), 3e-3));
        CHECK_THAT(hb->NodePhasors[4][1].imag(), WithinAbs(tran_v4[1].imag(), 3e-3));
        CHECK_THAT(hb->NodePhasors[4][2].real(), WithinAbs(tran_v4[2].real(), 3e-3));
        CHECK_THAT(hb->NodePhasors[4][2].imag(), WithinAbs(tran_v4[2].imag(), 3e-3));
        CHECK_THAT(nodalys::ComputeThd(hb->NodePhasors[4]), WithinAbs(nodalys::ComputeThd(tran_v4), 2e-3));
    }
}

TEST_CASE("Harmonic Balance matches steady-state transient FFT on continuous smooth logic inverter", "[nodalys][hb]") {
    nodalys::CircuitBuilder b;
    constexpr double F0 = 1e6;
    constexpr double T0 = 1.0 / F0;

    nodalys::PulseParameters pulse;
    pulse.Low = 0.0;
    pulse.High = 5.0;
    pulse.Delay = 0.0;
    pulse.RiseTime = 50e-9;
    pulse.FallTime = 50e-9;
    pulse.Width = 450e-9;
    pulse.Period = T0;
    b.AddVpulse("V_IN", 1, 0, pulse);

    nodalys::SmoothLogicParameters params;
    params.PropagationDelay = 20e-9;
    params.TransitionWidth = 0.3;
    b.AddInverter("U1", 2, 1, params);

    auto circuit = b.Build();
    REQUIRE(circuit.has_value());

    const auto hb =
        nodalys::ComputeHarmonicBalance(*circuit, {.FundamentalFrequency = F0, .Harmonics = 8, .Samples = 64});
    REQUIRE(hb.has_value());

    const auto tran =
        nodalys::ComputeTransient(*circuit, {.StopTime = 12.0 * T0, .TimeStep = T0 / 512.0, .MaxStep = T0 / 512.0});
    REQUIRE(tran.has_value());
    const auto tran_v2 = ExtractTransientHarmonics(tran->Times, tran->NodeVoltages[2], 11.0 * T0, T0, 256, 5);

    CHECK_THAT(hb->NodePhasors[2][0].real(), WithinAbs(tran_v2[0].real(), 0.02));
    CHECK_THAT(std::abs(hb->NodePhasors[2][1]), WithinAbs(std::abs(tran_v2[1]), 0.03));
    CHECK_THAT(std::abs(hb->NodePhasors[2][2]), WithinAbs(std::abs(tran_v2[2]), 0.03));
}

TEST_CASE("Harmonic Balance vs steady-state transient benchmark metrics", "[.benchmark][nodalys][hb]") {
    SECTION("High-Q series RLC circuit with shunt diode limiter") {
        // R = 1 Ohm, L = 31.831 mH (w0*L = 200 Ohm), C = 795.77 nF (1/(w0*C) = 200 Ohm), f0 = 1 kHz => Q = 200
        // Time constant tau = 2L/R = 63.66 ms -> needs ~400 ms (400 cycles) in transient to settle
        const std::string_view netlist = "* High-Q nonlinear RLC tank benchmark\n"
                                         "V1 1 0 SIN(0 0.2 1k)\n"
                                         "R1 1 2 1\n"
                                         "L1 2 3 31.8309886m\n"
                                         "C1 3 0 795.774715n\n"
                                         "R2 3 0 10k\n"
                                         "D1 3 0 DMOD\n"
                                         ".model DMOD D(IS=1e-13 N=1.5)\n"
                                         ".end\n";
        const auto parsed = nodalys::ParseNetlist(netlist);
        REQUIRE(parsed.has_value());

        constexpr double F0 = 1e3;
        constexpr double T0 = 1.0 / F0;

        const auto hb_start = std::chrono::steady_clock::now();
        const auto hb = nodalys::ComputeHarmonicBalance(parsed->CircuitInstance,
                                                        {.FundamentalFrequency = F0, .Harmonics = 8, .Samples = 64});
        const auto hb_end = std::chrono::steady_clock::now();
        REQUIRE(hb.has_value());

        const auto tran_start = std::chrono::steady_clock::now();
        const auto tran = nodalys::ComputeTransient(
            parsed->CircuitInstance, {.StopTime = 400.0 * T0, .TimeStep = T0 / 256.0, .MaxStep = T0 / 256.0});
        const auto tran_end = std::chrono::steady_clock::now();
        REQUIRE(tran.has_value());
        const auto tran_v3 = ExtractTransientHarmonics(tran->Times, tran->NodeVoltages[3], 399.0 * T0, T0, 256, 5);
        const auto fft_end = std::chrono::steady_clock::now();

        const double hb_ms = std::chrono::duration<double, std::milli>(hb_end - hb_start).count();
        const double tran_ms = std::chrono::duration<double, std::milli>(fft_end - tran_start).count();
        const double v1_err = std::abs(std::abs(hb->NodePhasors[3][1]) - std::abs(tran_v3[1]));

        std::cout << std::format(
            "[BENCHMARK High-Q RLC+Diode] HB={:.3f} ms (iters={}, strategy={}), Tran+FFT={:.3f} ms, "
            "Speedup={:.1f}x, |V3(f0)| HB={:.4f} V Tran={:.4f} V (err={:.4e} V)\n",
            hb_ms, hb->Iterations, hb->Strategy, tran_ms, tran_ms / std::max(hb_ms, 1e-6),
            std::abs(hb->NodePhasors[3][1]), std::abs(tran_v3[1]), v1_err);

        CHECK(v1_err < 0.05);
    }

    SECTION("Half-wave diode rectifier with long RC time constant (tau = 100 ms = 100 T0)") {
        const std::string_view netlist = "* Long-tau rectifier benchmark\n"
                                         "V1 1 0 SIN(0 6 1k)\n"
                                         "D1 1 2 DMOD\n"
                                         "R1 2 0 10k\n"
                                         "C1 2 0 10u\n"
                                         ".model DMOD D(IS=1e-12 N=1.1 RS=1.0)\n"
                                         ".end\n";
        const auto parsed = nodalys::ParseNetlist(netlist);
        REQUIRE(parsed.has_value());

        constexpr double F0 = 1e3;
        constexpr double T0 = 1.0 / F0;

        const auto hb_start = std::chrono::steady_clock::now();
        const auto hb = nodalys::ComputeHarmonicBalance(parsed->CircuitInstance,
                                                        {.FundamentalFrequency = F0, .Harmonics = 16, .Samples = 128});
        const auto hb_end = std::chrono::steady_clock::now();
        REQUIRE(hb.has_value());

        const auto tran_start = std::chrono::steady_clock::now();
        const auto tran = nodalys::ComputeTransient(
            parsed->CircuitInstance, {.StopTime = 500.0 * T0, .TimeStep = T0 / 256.0, .MaxStep = T0 / 256.0});
        const auto fft_end = std::chrono::steady_clock::now();
        REQUIRE(tran.has_value());
        const auto tran_v2 = ExtractTransientHarmonics(tran->Times, tran->NodeVoltages[2], 499.0 * T0, T0, 256, 5);

        const double hb_ms = std::chrono::duration<double, std::milli>(hb_end - hb_start).count();
        const double tran_ms = std::chrono::duration<double, std::milli>(fft_end - tran_start).count();
        const double dc_err = std::abs(hb->NodePhasors[2][0].real() - tran_v2[0].real());

        std::cout << std::format(
            "[BENCHMARK Long-Tau Rectifier] HB={:.3f} ms (iters={}, strategy={}), Tran+FFT={:.3f} ms, "
            "Speedup={:.1f}x, Vdc HB={:.4f} V Tran={:.4f} V (err={:.4e} V)\n",
            hb_ms, hb->Iterations, hb->Strategy, tran_ms, tran_ms / std::max(hb_ms, 1e-6), hb->NodePhasors[2][0].real(),
            tran_v2[0].real(), dc_err);

        CHECK(dc_err < 0.05);
    }
}

} // namespace
