/**
 * @file    parameter_conversion.cpp
 * @brief   Turns the parameters of the parts of imcsim into those the Nodalys devices take.
 */

#include "backends/nodalys/parameter_conversion.h"

#include <cmath>

namespace Core {

namespace {

/// Emission coefficient of the co-packaged diode of an IGBT, that of a fast power diode.
constexpr double CO_PACKAGED_DIODE_EMISSION = 1.5;
/// Share of the forward voltage of the co-packaged diode at the rated current that its series resistance drops.
constexpr double CO_PACKAGED_DIODE_RESISTIVE_SHARE = 0.3;
/// Thermal voltage kT/q at 27 degrees Celsius, in Volts.
constexpr double THERMAL_VOLTAGE = 0.025865;
/// Breakdown voltage of the co-packaged diode, far beyond any rating, as neither it nor the IGBT models avalanche.
constexpr double CO_PACKAGED_DIODE_BREAKDOWN = 1e6;

/**
 * @brief   The diode of an IGBT package, from its forward voltage at the rated collector current.
 * @details A share of the drop is the series resistance and the rest the junction, whose saturation current follows
 *          from the Shockley equation at that current.
 */
Nodalys::DiodeParameters ToCoPackagedDiode(const IGBTParameters &parameters) {
    const double rated_current = parameters.MaxCollectorCurrent;
    const double resistive_drop = CO_PACKAGED_DIODE_RESISTIVE_SHARE * parameters.DiodeForwardVoltage;
    const double junction_drop = parameters.DiodeForwardVoltage - resistive_drop;
    return {.SaturationCurrent =
                rated_current * std::exp(-junction_drop / (CO_PACKAGED_DIODE_EMISSION * THERMAL_VOLTAGE)),
            .EmissionCoefficient = CO_PACKAGED_DIODE_EMISSION,
            .SeriesResistance = resistive_drop / rated_current,
            .BreakdownVoltage = CO_PACKAGED_DIODE_BREAKDOWN};
}

} // namespace

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
 * @brief   The figures of an IGBT, without its ratings, with its co-packaged diode when it has one.
 * @param[in] parameters  Datasheet figures of the part.
 * @return  The same figures for the Nodalys IGBT.
 */
Nodalys::IgbtParameters ToNodalys(const IGBTParameters &parameters) {
    Nodalys::IgbtParameters figures{.GateThresholdVoltage = parameters.GateThresholdVoltage,
                                    .Transconductance = parameters.Transconductance,
                                    .TransconductanceCurrent = parameters.TransconductanceCurrent,
                                    .OnThresholdVoltage = parameters.OnThresholdVoltage,
                                    .OnResistance = parameters.OnResistance,
                                    .InputCapacitance = parameters.InputCapacitance,
                                    .OutputCapacitance = parameters.OutputCapacitance,
                                    .ReverseTransferCapacitance = parameters.ReverseTransferCapacitance,
                                    .TailFraction = parameters.TailFraction,
                                    .TailTimeConstant = parameters.TailTimeConstant};
    if (parameters.DiodeForwardVoltage > 0.0) {
        figures.AntiParallelDiode = ToCoPackagedDiode(parameters);
    }
    return figures;
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
 * @brief   Converts transient noise source parameters to Nodalys.
 * @param[in] parameters  Core noise parameters.
 * @return  Equivalent Nodalys noise parameters.
 */
Nodalys::NoiseParameters ToNodalys(const NoiseParameters &parameters) {
    return {.Offset = parameters.Offset,
            .WhiteAmplitude = parameters.WhiteAmplitude,
            .TimeStep = parameters.TimeStep,
            .FlickerExponent = parameters.FlickerExponent,
            .FlickerAmplitude = parameters.FlickerAmplitude,
            .TelegraphAmplitude = parameters.TelegraphAmplitude,
            .CaptureTime = parameters.CaptureTime,
            .EmissionTime = parameters.EmissionTime};
}

/**
 * @brief   Converts transient random source parameters to Nodalys.
 * @param[in] parameters  Core random parameters.
 * @return  Equivalent Nodalys random parameters.
 */
Nodalys::RandomParameters ToNodalys(const RandomParameters &parameters) {
    Nodalys::RandomDistribution distribution = Nodalys::RandomDistribution::UNIFORM;
    switch (parameters.Distribution) {
    case RandomDistribution::UNIFORM:
        distribution = Nodalys::RandomDistribution::UNIFORM;
        break;
    case RandomDistribution::GAUSSIAN:
        distribution = Nodalys::RandomDistribution::GAUSSIAN;
        break;
    case RandomDistribution::EXPONENTIAL:
        distribution = Nodalys::RandomDistribution::EXPONENTIAL;
        break;
    case RandomDistribution::POISSON:
        distribution = Nodalys::RandomDistribution::POISSON;
        break;
    default:
        break;
    }
    return {.Distribution = distribution,
            .TimeStep = parameters.TimeStep,
            .Delay = parameters.Delay,
            .First = parameters.First,
            .Second = parameters.Second};
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

/**
 * @brief   The figures of a fuse.
 * @param[in] parameters  Datasheet figures of the fuse.
 * @return  The same fuse for Nodalys, which works out the cooling time constant from the rated current and the I2t.
 */
Nodalys::FuseParameters ToNodalys(const FuseParameters &parameters) {
    return {.RatedCurrent = parameters.RatedCurrent,
            .MeltingI2t = parameters.MeltingI2t,
            .ColdResistance = parameters.ColdResistance,
            .BlownResistance = parameters.BlownResistance};
}

/**
 * @brief   The figures of a thyristor (SCR), TRIAC or GTO.
 * @param[in] parameters  Datasheet figures of the thyristor, with the direction the part type fixes.
 * @return  The same figures for the Nodalys device, which takes every one of them as a parameter.
 */
Nodalys::ThyristorParameters ToNodalys(const ThyristorParameters &parameters) {
    return {.OnResistance = parameters.OnResistance,
            .OnThresholdVoltage = parameters.OnThresholdVoltage,
            .OffResistance = parameters.OffResistance,
            .HoldingCurrent = parameters.HoldingCurrent,
            .GateTriggerCurrent = parameters.GateTriggerCurrent,
            .GateTriggerVoltage = parameters.GateTriggerVoltage,
            .GateResistance = parameters.GateResistance,
            .BreakoverVoltage = parameters.BreakoverVoltage,
            .CriticalDvDt = parameters.CriticalDvDt,
            .TurnOffTime = parameters.TurnOffTime,
            .GateTurnOffCurrent = parameters.GateTurnOffCurrent,
            .IsBidirectional = parameters.IsBidirectional};
}

/**
 * @brief   The figures of a DIAC.
 * @param[in] parameters  Datasheet figures of the DIAC.
 * @return  The same figures for the Nodalys device.
 */
Nodalys::DiacParameters ToNodalys(const DiacParameters &parameters) {
    return {.BreakoverVoltage = parameters.BreakoverVoltage,
            .OnThresholdVoltage = parameters.OnThresholdVoltage,
            .OnResistance = parameters.OnResistance,
            .OffResistance = parameters.OffResistance,
            .HoldingCurrent = parameters.HoldingCurrent};
}

/**
 * @brief   Combines the characteristic impedance and the delay/loss figures of a transmission line for Nodalys.
 * @param[in] impedance_ohms  Lossless characteristic impedance \f$Z_0\f$ in Ohms.
 * @param[in] parameters      Delay and optional conductor/dielectric loss figures.
 * @return  Equivalent Nodalys transmission line parameters.
 */
Nodalys::TransmissionLineParameters ToNodalys(const double impedance_ohms,
                                              const TransmissionLineParameters &parameters) {
    return {.ImpedanceOhms = impedance_ohms,
            .DelaySeconds = parameters.Delay,
            .DcResistanceOhms = parameters.DcResistance,
            .SkinResistanceOhms = parameters.SkinResistance,
            .DcConductanceSiemens = parameters.DcConductance,
            .LossTangent = parameters.LossTangent,
            .ReferenceFrequencyHz = parameters.ReferenceFrequency};
}

} // namespace Core
