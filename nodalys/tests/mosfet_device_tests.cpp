/**
 * @file    mosfet_device_tests.cpp
 * @brief   Tests for the channel frame of the MOSFET, which every method reads from one evaluation of the channel.
 */

#include "devices/mosfet_device.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <complex>
#include <vector>

using Catch::Matchers::WithinRel;

namespace {

constexpr int DRAIN = 0;
constexpr int GATE = 1;
constexpr int SOURCE = 2;
constexpr int NO_INTERNAL_NODE = -1;
constexpr int TERMINAL_COUNT = 3;

Nodalys::MosfetParameters MakeParameters(const bool is_pmos) {
    Nodalys::MosfetParameters parameters;
    // SPICE gives the threshold of a PMOS as a negative number
    parameters.ThresholdVoltage = is_pmos ? -0.8 : 0.8;
    parameters.Transconductance = 100e-6;
    parameters.ChannelModulation = 0.02;
    parameters.GateSourceOverlap = 1e-9;
    parameters.GateDrainOverlap = 2e-9;
    parameters.Width = 20e-6;
    parameters.Length = 2e-6;
    return parameters;
}

Nodalys::MosfetDevice MakeTransistor(const bool is_pmos) {
    return {DRAIN, GATE, SOURCE, NO_INTERNAL_NODE, NO_INTERNAL_NODE, MakeParameters(is_pmos), is_pmos};
}

std::vector<double> Negated(std::vector<double> voltages) {
    for (double &voltage : voltages) {
        voltage = -voltage;
    }
    return voltages;
}

} // namespace

TEST_CASE("A PMOS carries the opposite currents of an NMOS at mirrored voltages", "[mosfet]") {
    const Nodalys::MosfetDevice nmos = MakeTransistor(false);
    const Nodalys::MosfetDevice pmos = MakeTransistor(true);
    const std::vector<double> nmos_voltages = {3.0, 2.0, 0.0};
    const std::vector<double> pmos_voltages = Negated(nmos_voltages);

    for (int terminal = 0; terminal < TERMINAL_COUNT; ++terminal) {
        CHECK(pmos.TerminalCurrent(terminal, pmos_voltages) == -nmos.TerminalCurrent(terminal, nmos_voltages));
    }
    CHECK(nmos.TerminalCurrent(DRAIN, nmos_voltages) > 0.0);
}

TEST_CASE("A MOSFET swaps its drain and source when the channel runs backwards", "[mosfet]") {
    const Nodalys::MosfetDevice nmos = MakeTransistor(false);
    // The terminal at 0 V acts as the source in both, with the same gate drive of 2 V and 0.1 V across the channel
    const std::vector<double> forward = {0.1, 2.0, 0.0};
    const std::vector<double> backward = {0.0, 2.0, 0.1};

    CHECK(nmos.TerminalCurrent(DRAIN, forward) > 0.0);
    CHECK(nmos.TerminalCurrent(DRAIN, backward) < 0.0);
    // The body diode, forward biased by 0.1 V when reversed, adds a few picoamperes to the mirrored current
    CHECK_THAT(nmos.TerminalCurrent(DRAIN, backward), WithinRel(-nmos.TerminalCurrent(DRAIN, forward), 1e-6));
}

TEST_CASE("A MOSFET with a reversed channel drives the small-signal current the other way", "[mosfet]") {
    const Nodalys::MosfetDevice nmos = MakeTransistor(false);
    const std::vector<std::complex<double>> gate_perturbation = {{0.0, 0.0}, {1e-3, 0.0}, {0.0, 0.0}};
    // At zero frequency the overlap capacitances carry nothing, so only the channel is left
    constexpr double OMEGA = 0.0;

    const std::complex<double> forward = nmos.AcTerminalCurrent(DRAIN, {3.0, 2.0, 0.0}, gate_perturbation, OMEGA);
    const std::complex<double> backward = nmos.AcTerminalCurrent(DRAIN, {0.0, 2.0, 3.0}, gate_perturbation, OMEGA);
    CHECK(forward.real() > 0.0);
    CHECK(backward == -forward);
}
