/**
 * @file    parameter_conversion.cpp
 * @brief   Turns the parameters of the parts of imcsim into those the Nodalys devices take.
 */

#include "backends/nodalys/parameter_conversion.h"

namespace Core {

/**
 * @brief   The SPICE diode model of a diode part, without its ratings, which only imcsim checks.
 * @param[in] parameters  Parameters of the part.
 * @return  The same figures for the Nodalys diode.
 */
Nodalys::DiodeParameters ToNodalys(const DiodeParameters &parameters) {
    return {.SaturationCurrent = parameters.SaturationCurrent,
            .EmissionCoefficient = parameters.EmissionCoefficient,
            .SeriesResistance = parameters.SeriesResistance,
            .BreakdownVoltage = parameters.BreakdownVoltage,
            .BreakdownCurrent = parameters.BreakdownCurrent,
            .JunctionCapacitance = parameters.JunctionCapacitance,
            .JunctionPotential = parameters.JunctionPotential,
            .GradingCoefficient = parameters.GradingCoefficient,
            .TransitTime = parameters.TransitTime};
}

/**
 * @brief   The Gummel-Poon model of a bipolar transistor part, without its ratings.
 * @param[in] parameters  Parameters of the part.
 * @return  The same figures for the Nodalys transistor.
 */
Nodalys::BjtParameters ToNodalys(const BJTParameters &parameters) {
    return {.SaturationCurrent = parameters.SaturationCurrent,
            .ForwardBeta = parameters.ForwardBeta,
            .ReverseBeta = parameters.ReverseBeta,
            .EarlyVoltage = parameters.EarlyVoltage,
            .ForwardKneeCurrent = parameters.ForwardKneeCurrent,
            .LeakageSaturationCurrent = parameters.LeakageSaturationCurrent,
            .LeakageEmissionCoefficient = parameters.LeakageEmissionCoefficient,
            .BaseResistance = parameters.BaseResistance,
            .CollectorResistance = parameters.CollectorResistance,
            .EmitterResistance = parameters.EmitterResistance,
            .EmitterCapacitance = parameters.EmitterCapacitance,
            .CollectorCapacitance = parameters.CollectorCapacitance,
            .TransitTime = parameters.TransitTime};
}

/**
 * @brief   The level 1 model and channel size of a MOSFET part, without its ratings.
 * @param[in] parameters  Parameters of the part.
 * @return  The same figures for the Nodalys MOSFET.
 */
Nodalys::MosfetParameters ToNodalys(const MOSFETParameters &parameters) {
    return {.ThresholdVoltage = parameters.ThresholdVoltage,
            .Transconductance = parameters.Transconductance,
            .ChannelModulation = parameters.ChannelModulation,
            .DrainResistance = parameters.DrainResistance,
            .SourceResistance = parameters.SourceResistance,
            .GateSourceOverlap = parameters.GateSourceOverlap,
            .GateDrainOverlap = parameters.GateDrainOverlap,
            .Width = parameters.Width,
            .Length = parameters.Length};
}

/**
 * @brief   The sine of an AC source, which starts at phase 0 as the netlist of imcsim writes it.
 * @param[in] parameters  Waveform of the source.
 * @return  The same sine for Nodalys.
 */
Nodalys::SineParameters ToNodalys(const ACParameters &parameters) {
    return {.Amplitude = parameters.Amplitude,
            .Frequency = parameters.Frequency,
            .Offset = parameters.Offset,
            .Magnitude = parameters.Magnitude};
}

/**
 * @brief   The pulse train of a pulse source.
 * @param[in] parameters  Waveform of the source.
 * @return  The same pulse for Nodalys.
 */
Nodalys::PulseParameters ToNodalys(const PulseParameters &parameters) {
    return {.Low = parameters.Low,
            .High = parameters.High,
            .Delay = parameters.Delay,
            .RiseTime = parameters.RiseTime,
            .FallTime = parameters.FallTime,
            .Width = parameters.Width,
            .Period = parameters.Period};
}

/**
 * @brief   The rise and decay of an exponential source.
 * @param[in] parameters  Waveform of the source.
 * @return  The same waveform for Nodalys.
 */
Nodalys::ExpParameters ToNodalys(const EXPParameters &parameters) {
    return {.InitialValue = parameters.InitialValue,
            .PeakValue = parameters.PeakValue,
            .RiseDelay = parameters.RiseDelay,
            .RiseTimeConstant = parameters.RiseTimeConstant,
            .FallDelay = parameters.FallDelay,
            .FallTimeConstant = parameters.FallTimeConstant};
}

/**
 * @brief   The points of a piecewise linear source.
 * @param[in] parameters  Waveform of the source.
 * @return  The same points for Nodalys.
 */
Nodalys::PwlParameters ToNodalys(const PWLParameters &parameters) {
    Nodalys::PwlParameters converted{.Points = {}};
    converted.Points.reserve(parameters.Points.size());
    for (const PWLPoint &point : parameters.Points) {
        converted.Points.push_back({.Time = point.Time, .Value = point.Value});
    }
    return converted;
}

/**
 * @brief   The frequency-modulated sine of an SFFM source.
 * @param[in] parameters  Waveform of the source.
 * @return  The same waveform for Nodalys.
 */
Nodalys::SffmParameters ToNodalys(const SFFMParameters &parameters) {
    return {.Offset = parameters.Offset,
            .Amplitude = parameters.Amplitude,
            .CarrierFrequency = parameters.CarrierFrequency,
            .ModulationIndex = parameters.ModulationIndex,
            .SignalFrequency = parameters.SignalFrequency};
}

/**
 * @brief   The amplitude-modulated sine of an AM source.
 * @param[in] parameters  Waveform of the source.
 * @return  The same waveform for Nodalys.
 */
Nodalys::AmParameters ToNodalys(const AMParameters &parameters) {
    return {.Amplitude = parameters.Amplitude,
            .Offset = parameters.Offset,
            .ModulatingFrequency = parameters.ModulatingFrequency,
            .CarrierFrequency = parameters.CarrierFrequency,
            .Delay = parameters.Delay};
}

/**
 * @brief   The square wave of a clock.
 * @param[in] parameters  Settings of the clock.
 * @return  The same clock for Nodalys.
 */
Nodalys::ClockParameters ToNodalys(const ClockParameters &parameters) {
    return {.Frequency = parameters.Frequency,
            .DutyCycle = parameters.DutyCycle,
            .LogicLevel = parameters.LogicLevel,
            .StartDelay = parameters.StartDelay};
}

/**
 * @brief   The thresholds and resistances of a voltage-controlled switch or a relay contact.
 * @param[in] parameters  Settings of the switch.
 * @return  The same switch for Nodalys.
 */
Nodalys::SwitchParameters ToNodalys(const SwitchParameters &parameters) {
    return {.Threshold = parameters.Threshold,
            .Hysteresis = parameters.Hysteresis,
            .OnResistance = parameters.OnResistance,
            .OffResistance = parameters.OffResistance};
}

} // namespace Core
