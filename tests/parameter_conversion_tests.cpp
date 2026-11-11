/**
 * @file    parameter_conversion_tests.cpp
 * @brief   Tests for the parameters of the parts of imcsim turned into those the Nodalys devices take.
 */

#include "nodalys/parameter_conversion.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Every model figure of a semiconductor reaches Nodalys, and the ratings stay in imcsim",
          "[parameter_conversion]") {
    // Values unlike the defaults, so a figure left out or swapped with another shows
    const Core::DiodeParameters diode{.SaturationCurrent = 1e-9,
                                      .EmissionCoefficient = 1.8,
                                      .SeriesResistance = 0.5,
                                      .BreakdownVoltage = 5.1,
                                      .BreakdownCurrent = 5e-3,
                                      .JunctionCapacitance = 4e-12,
                                      .JunctionPotential = 0.7,
                                      .GradingCoefficient = 0.33,
                                      .TransitTime = 6e-9,
                                      .MaxForwardCurrent = 1.0,
                                      .MaxPower = 1.3};
    CHECK(Core::ToNodalys(diode) == Nodalys::DiodeParameters{.SaturationCurrent = 1e-9,
                                                             .EmissionCoefficient = 1.8,
                                                             .SeriesResistance = 0.5,
                                                             .BreakdownVoltage = 5.1,
                                                             .BreakdownCurrent = 5e-3,
                                                             .JunctionCapacitance = 4e-12,
                                                             .JunctionPotential = 0.7,
                                                             .GradingCoefficient = 0.33,
                                                             .TransitTime = 6e-9});

    const Core::MOSFETParameters mosfet{.ThresholdVoltage = 2.1,
                                        .Transconductance = 0.4,
                                        .ChannelModulation = 0.01,
                                        .DrainResistance = 0.1,
                                        .SourceResistance = 0.2,
                                        .GateSourceOverlap = 1e-9,
                                        .GateDrainOverlap = 2e-9,
                                        .Width = 2e-3,
                                        .Length = 1e-6};
    CHECK(Core::ToNodalys(mosfet) == Nodalys::MosfetParameters{.ThresholdVoltage = 2.1,
                                                               .Transconductance = 0.4,
                                                               .ChannelModulation = 0.01,
                                                               .DrainResistance = 0.1,
                                                               .SourceResistance = 0.2,
                                                               .GateSourceOverlap = 1e-9,
                                                               .GateDrainOverlap = 2e-9,
                                                               .Width = 2e-3,
                                                               .Length = 1e-6});

    const Core::BJTParameters bjt{
        .SaturationCurrent = 2e-14, .ForwardBeta = 250.0, .EarlyVoltage = 74.0, .TransitTime = 4e-10};
    const Nodalys::BjtParameters converted = Core::ToNodalys(bjt);
    CHECK(converted.SaturationCurrent == 2e-14);
    CHECK(converted.ForwardBeta == 250.0);
    CHECK(converted.EarlyVoltage == 74.0);
    CHECK(converted.TransitTime == 4e-10);
}

TEST_CASE("A waveform reaches Nodalys as it is, and an AC source starts at phase 0", "[parameter_conversion]") {
    const Core::ACParameters ac{.Amplitude = 2.0, .Frequency = 50.0, .Offset = 1.0, .Magnitude = 0.5};
    CHECK(Core::ToNodalys(ac) ==
          Nodalys::SineParameters{.Amplitude = 2.0, .Frequency = 50.0, .Offset = 1.0, .Magnitude = 0.5, .Phase = 0.0});

    const Core::PWLParameters pwl{.Points = {{0.0, 1.0}, {1e-3, -1.0}}};
    CHECK(Core::ToNodalys(pwl) == Nodalys::PwlParameters{.Points = {{0.0, 1.0}, {1e-3, -1.0}}});

    const Core::PulseParameters pulse{
        .Low = -1.0, .High = 3.0, .Delay = 1e-6, .RiseTime = 2e-9, .FallTime = 3e-9, .Width = 4e-6, .Period = 1e-5};
    CHECK(Core::ToNodalys(pulse) == Nodalys::PulseParameters{.Low = -1.0,
                                                             .High = 3.0,
                                                             .Delay = 1e-6,
                                                             .RiseTime = 2e-9,
                                                             .FallTime = 3e-9,
                                                             .Width = 4e-6,
                                                             .Period = 1e-5});

    const Core::ClockParameters clock{.Frequency = 2e3, .DutyCycle = 0.25, .LogicLevel = 3.3, .StartDelay = 1e-4};
    CHECK(Core::ToNodalys(clock) ==
          Nodalys::ClockParameters{.Frequency = 2e3, .DutyCycle = 0.25, .LogicLevel = 3.3, .StartDelay = 1e-4});

    const Core::SwitchParameters contact{
        .Threshold = 1.0, .Hysteresis = 0.2, .OnResistance = 0.5, .OffResistance = 1e6};
    CHECK(Core::ToNodalys(contact) ==
          Nodalys::SwitchParameters{.Threshold = 1.0, .Hysteresis = 0.2, .OnResistance = 0.5, .OffResistance = 1e6});
}
