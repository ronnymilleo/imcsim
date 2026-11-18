/**
 * @file    bjt_device_tests.cpp
 * @brief   Tests for the polarity handling of the bipolar transistor, which every method reads from one junction state.
 */

#include "devices/bjt_device.h"

#include <catch2/catch_test_macros.hpp>
#include <complex>
#include <vector>

namespace {

constexpr int COLLECTOR = 0;
constexpr int BASE = 1;
constexpr int EMITTER = 2;
constexpr int NO_INTERNAL_NODE = -1;
constexpr int TERMINAL_COUNT = 3;

/// A transistor with Early effect, knee current, leakage and every charge term, so no branch of the model is skipped.
Nodalys::BjtParameters MakeFullParameters() {
    Nodalys::BjtParameters parameters;
    parameters.SaturationCurrent = 1e-15;
    parameters.ForwardBeta = 120.0;
    parameters.ReverseBeta = 3.0;
    parameters.EarlyVoltage = 60.0;
    parameters.ForwardKneeCurrent = 0.02;
    parameters.LeakageSaturationCurrent = 1e-14;
    parameters.EmitterCapacitance = 2e-12;
    parameters.CollectorCapacitance = 1e-12;
    parameters.TransitTime = 300e-12;
    return parameters;
}

Nodalys::BjtDevice MakeTransistor(const bool is_pnp) {
    return {COLLECTOR,
            BASE,
            EMITTER,
            NO_INTERNAL_NODE,
            NO_INTERNAL_NODE,
            NO_INTERNAL_NODE,
            MakeFullParameters(),
            is_pnp};
}

std::vector<double> Negated(std::vector<double> voltages) {
    for (double &voltage : voltages) {
        voltage = -voltage;
    }
    return voltages;
}

} // namespace

TEST_CASE("A PNP carries the opposite currents and charges of an NPN at mirrored voltages", "[bjt]") {
    const Nodalys::BjtDevice npn = MakeTransistor(false);
    const Nodalys::BjtDevice pnp = MakeTransistor(true);
    // Forward-active NPN bias, then its mirror image for the PNP
    const std::vector<double> npn_voltages = {3.0, 0.65, 0.0};
    const std::vector<double> pnp_voltages = Negated(npn_voltages);

    for (int terminal = 0; terminal < TERMINAL_COUNT; ++terminal) {
        CHECK(pnp.TerminalCurrent(terminal, pnp_voltages) == -npn.TerminalCurrent(terminal, npn_voltages));
        CHECK(pnp.TerminalCharge(terminal, pnp_voltages) == -npn.TerminalCharge(terminal, npn_voltages));
    }
    // The currents of the two polarities are not zero, so the equalities above are not vacuous
    CHECK(npn.TerminalCurrent(COLLECTOR, npn_voltages) > 0.0);
    CHECK(npn.TerminalCharge(BASE, npn_voltages) != 0.0);
}

TEST_CASE("A PNP has the same small-signal currents as an NPN at mirrored bias", "[bjt]") {
    const Nodalys::BjtDevice npn = MakeTransistor(false);
    const Nodalys::BjtDevice pnp = MakeTransistor(true);
    const std::vector<double> npn_bias = {3.0, 0.65, 0.0};
    const std::vector<double> pnp_bias = Negated(npn_bias);
    const std::vector<std::complex<double>> perturbation = {{0.0, 0.0}, {1e-3, 2e-4}, {0.0, 0.0}};
    constexpr double OMEGA = 2.0e7;

    for (int terminal = 0; terminal < TERMINAL_COUNT; ++terminal) {
        CHECK(pnp.AcTerminalCurrent(terminal, pnp_bias, perturbation, OMEGA)
              == npn.AcTerminalCurrent(terminal, npn_bias, perturbation, OMEGA));
    }
}
