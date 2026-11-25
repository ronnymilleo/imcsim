/**
 * @file    waveform_types.h
 * @brief   Time-domain independent source waveform parameter structures and random distribution kinds for Nodalys.
 */

#ifndef NODALYS_WAVEFORM_TYPES_H
#define NODALYS_WAVEFORM_TYPES_H

#include <vector>

namespace Nodalys {

/**
 * @enum    RandomDistribution
 * @brief   Distribution of the values of a SPICE TRRANDOM source, numbered as its TYPE parameter.
 */
enum class RandomDistribution {
    UNIFORM = 1,
    GAUSSIAN = 2,
    EXPONENTIAL = 3,
    POISSON = 4
};

// Source waveforms

/**
 * @struct  AmParameters
 * @brief   SPICE AM(SA FC MF FS TD) amplitude-modulation parameters.
 */
struct AmParameters {
    double Amplitude = 1.0;
    double Offset = 0.0;
    double ModulatingFrequency = 100.0;
    double CarrierFrequency = 1e3;
    double Delay = 0.0;

    /**
     * @brief   Compares two AmParameters values for equality.
     * @return  True when all fields match.
     */
    bool operator==(const AmParameters &) const = default;
};

/**
 * @struct  ClockParameters
 * @brief   Digital clock pulse train parameters.
 */
struct ClockParameters {
    double Frequency = 1e3;
    double DutyCycle = 0.5;
    double LogicLevel = 5.0;
    double StartDelay = 0.0;

    /**
     * @brief   Compares two ClockParameters values for equality.
     * @return  True when all fields match.
     */
    bool operator==(const ClockParameters &) const = default;
};

/**
 * @struct  ExpParameters
 * @brief   SPICE EXP(V1 V2 TD1 TAU1 TD2 TAU2) parameters.
 */
struct ExpParameters {
    double InitialValue = 0.0;
    double PeakValue = 5.0;
    double RiseDelay = 1e-3;
    double RiseTimeConstant = 0.5e-3;
    double FallDelay = 5e-3;
    double FallTimeConstant = 1e-3;

    /**
     * @brief   Compares two ExpParameters values for equality.
     * @return  True when all fields match.
     */
    bool operator==(const ExpParameters &) const = default;
};

/**
 * @struct  NoiseParameters
 * @brief   SPICE TRNOISE(NA NT ALPHA NALPHA RTSAM RTSCAPT RTSEMT) parameters, plus DC offset.
 */
struct NoiseParameters {
    double Offset = 0.0;
    double WhiteAmplitude = 0.1;
    double TimeStep = 10e-6;
    double FlickerExponent = 1.0;
    double FlickerAmplitude = 0.0;
    double TelegraphAmplitude = 0.0;
    double CaptureTime = 1e-3;
    double EmissionTime = 1e-3;

    /**
     * @brief   Compares two NoiseParameters values for equality.
     * @return  True when all fields match.
     */
    bool operator==(const NoiseParameters &) const = default;
};

/**
 * @struct  PulseParameters
 * @brief   SPICE PULSE(V1 V2 TD TR TF PW PER) parameters.
 */
struct PulseParameters {
    double Low = 0.0;
    double High = 5.0;
    double Delay = 0.0;
    double RiseTime = 1e-6;
    double FallTime = 1e-6;
    double Width = 0.5e-3;
    double Period = 1e-3;

    /**
     * @brief   Compares two PulseParameters values for equality.
     * @return  True when all fields match.
     */
    bool operator==(const PulseParameters &) const = default;
};

/**
 * @struct  PwlPoint
 * @brief   One time/voltage point of a piece-wise linear waveform.
 */
struct PwlPoint {
    double Time = 0.0;
    double Value = 0.0;

    /**
     * @brief   Compares two PwlPoint values for equality.
     * @return  True when all fields match.
     */
    bool operator==(const PwlPoint &) const = default;
};

/**
 * @struct  PwlParameters
 * @brief   SPICE PWL(T1 V1 T2 V2 ...) piece-wise linear parameters.
 */
struct PwlParameters {
    std::vector<PwlPoint> Points = {{0.0, 0.0}, {1e-3, 5.0}, {2e-3, 0.0}};

    /**
     * @brief   Compares two PwlParameters values for equality.
     * @return  True when all fields match.
     */
    bool operator==(const PwlParameters &) const = default;
};

/**
 * @struct  RandomParameters
 * @brief   SPICE TRRANDOM(TYPE TS TD P1 P2) parameters.
 */
struct RandomParameters {
    RandomDistribution Distribution = RandomDistribution::GAUSSIAN;
    double TimeStep = 10e-6;
    double Delay = 0.0;
    double First = 0.1;
    double Second = 0.0;

    /**
     * @brief   Compares two RandomParameters values for equality.
     * @return  True when all fields match.
     */
    bool operator==(const RandomParameters &) const = default;
};

/**
 * @struct  SffmParameters
 * @brief   SPICE SFFM(VO VA FC MDI FS) single-frequency FM parameters.
 */
struct SffmParameters {
    double Offset = 0.0;
    double Amplitude = 1.0;
    double CarrierFrequency = 1e3;
    double ModulationIndex = 5.0;
    double SignalFrequency = 100.0;

    /**
     * @brief   Compares two SffmParameters values for equality.
     * @return  True when all fields match.
     */
    bool operator==(const SffmParameters &) const = default;
};

/**
 * @struct  SineParameters
 * @brief   SPICE SIN(VO VA FREQ TD THETA) parameters.
 */
struct SineParameters {
    double Amplitude = 1.0;
    double Frequency = 1e3;
    double Offset = 0.0;
    double Magnitude = 1.0;
    double PhaseDegrees = 0.0; ///< Phase angle in degrees.

    /**
     * @brief   Compares two SineParameters values for equality.
     * @return  True when all fields match.
     */
    bool operator==(const SineParameters &) const = default;
};

} // namespace Nodalys

#endif // NODALYS_WAVEFORM_TYPES_H
