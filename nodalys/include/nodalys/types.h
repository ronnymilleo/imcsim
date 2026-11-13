/**
 * @file    types.h
 * @brief   Common types, device parameter structures, and tolerance options for Nodalys.
 */

#ifndef NODALYS_TYPES_H
#define NODALYS_TYPES_H

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace Nodalys {

/// Unique integer identifier for a circuit node. Ground is always node 0.
using NodeId = int;
inline constexpr NodeId GROUND_NODE = 0;

/**
 * @enum    IntegrationMethod
 * @brief   Formula that turns each charge or flux derivative of a transient into a companion model.
 * @details Both are second order. Backward Euler always takes the first step and the step that leaves a source
 *          corner, whatever the method.
 */
enum class IntegrationMethod {
    /// A-stable, keeps the amplitude of a lossless resonance, but rings on a stiff mode.
    TRAPEZOIDAL,
    /// Variable-step BDF-2 (SPICE `method=gear`): L-stable, so it settles stiff switching without ringing, at the
    /// cost of some numerical damping of a lightly damped resonance.
    GEAR2
};

/**
 * @struct  DiagnosticMessage
 * @brief   A message emitted by the simulator (e.g. convergence statistics, warnings).
 */
struct DiagnosticMessage {
    std::string Text;
    bool IsError = false;
};

/**
 * @struct  CircuitPort
 * @brief   RF scattering parameter port definition.
 */
struct CircuitPort {
    int Number = 1;
    NodeId Node = 0;
    double ReferenceOhms = 50.0;

    bool operator==(const CircuitPort &) const = default;
};

/**
 * @struct  DiodeParameters
 * @brief   Shockley diode and junction charge parameters matching Berkeley SPICE3.
 */
struct DiodeParameters {
    double SaturationCurrent = 1e-14;
    double EmissionCoefficient = 1.0;
    double SeriesResistance = 0.0;
    double BreakdownVoltage = 100.0;
    double BreakdownCurrent = 1e-3;
    double JunctionCapacitance = 0.0;
    double JunctionPotential = 1.0;
    double GradingCoefficient = 0.5;
    double TransitTime = 0.0;
    /// Band gap `EG` in electron-volts, which sets how fast the saturation current grows with the temperature.
    double EnergyGap = 1.11;
    /// Exponent `XTI` of the temperature ratio in the growth of the saturation current.
    double SaturationCurrentExponent = 3.0;

    bool operator==(const DiodeParameters &) const = default;
};

/**
 * @struct  BjtParameters
 * @brief   Gummel-Poon BJT device parameters.
 */
struct BjtParameters {
    double SaturationCurrent = 1e-16;
    double ForwardBeta = 100.0;
    double ReverseBeta = 1.0;
    double EarlyVoltage = 0.0;
    double ForwardKneeCurrent = 0.0;
    double LeakageSaturationCurrent = 0.0;
    double LeakageEmissionCoefficient = 1.5;
    double BaseResistance = 0.0;
    double CollectorResistance = 0.0;
    double EmitterResistance = 0.0;
    double EmitterCapacitance = 0.0;
    double CollectorCapacitance = 0.0;
    double TransitTime = 0.0;
    /// Band gap `EG` in electron-volts, which sets how fast the saturation currents grow with the temperature.
    double EnergyGap = 1.11;
    /// Exponent `XTI` of the temperature ratio in the growth of the saturation currents.
    double SaturationCurrentExponent = 3.0;
    /// Exponent `XTB` of the temperature ratio in the forward and reverse current gains.
    double BetaExponent = 0.0;

    bool operator==(const BjtParameters &) const = default;
};

/**
 * @struct  MosfetParameters
 * @brief   Level-1 Shichman-Hodges MOSFET parameters.
 */
struct MosfetParameters {
    double ThresholdVoltage = 0.0;
    double Transconductance = 2e-5;
    double ChannelModulation = 0.0;
    double DrainResistance = 0.0;
    double SourceResistance = 0.0;
    double GateSourceOverlap = 0.0;
    double GateDrainOverlap = 0.0;
    double Width = 100e-6;
    double Length = 100e-6;
    /// Surface potential `PHI` in volts, which the threshold voltage follows with the temperature.
    double SurfacePotential = 0.6;

    bool operator==(const MosfetParameters &) const = default;
};

/**
 * @struct  SwitchParameters
 * @brief   Voltage-controlled hysteretic switch parameters.
 */
struct SwitchParameters {
    double Threshold = 2.5;
    double Hysteresis = 0.1;
    double OnResistance = 0.01;
    double OffResistance = 1e9;

    bool operator==(const SwitchParameters &) const = default;
};

/**
 * @struct  FuseParameters
 * @brief   Datasheet figures of a fuse that blows by I squared t.
 * @details The element heats with the current squared and cools with the time constant I2t / Ih^2, so it never
 *          blows at or below the rated current, and blows a short overload once the current squared times its
 *          duration reaches the melting I2t.
 */
struct FuseParameters {
    /// Largest current the fuse carries for ever, in amperes.
    double RatedCurrent = 1.0;
    /// Current squared times time that melts the element in a short overload, in A^2 s.
    double MeltingI2t = 1.045;
    double ColdResistance = 0.0964;
    double BlownResistance = 1e9;

    bool operator==(const FuseParameters &) const = default;
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

    bool operator==(const PulseParameters &) const = default;
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
    double Phase = 0.0; // degrees

    bool operator==(const SineParameters &) const = default;
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

    bool operator==(const ExpParameters &) const = default;
};

/**
 * @struct  PwlPoint
 * @brief   One time/voltage point of a piece-wise linear waveform.
 */
struct PwlPoint {
    double Time = 0.0;
    double Value = 0.0;

    bool operator==(const PwlPoint &) const = default;
};

/**
 * @struct  PwlParameters
 * @brief   SPICE PWL(T1 V1 T2 V2 ...) piece-wise linear parameters.
 */
struct PwlParameters {
    std::vector<PwlPoint> Points = {{0.0, 0.0}, {1e-3, 5.0}, {2e-3, 0.0}};

    bool operator==(const PwlParameters &) const = default;
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

    bool operator==(const SffmParameters &) const = default;
};

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

    bool operator==(const ClockParameters &) const = default;
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

    bool operator==(const NoiseParameters &) const = default;
};

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

    bool operator==(const RandomParameters &) const = default;
};

/**
 * @struct  BoyleOpAmpParameters
 * @brief   Datasheet specifications for the Boyle op-amp macromodel (IEEE JSSC 1974).
 */
struct BoyleOpAmpParameters {
    double OpenLoopGainDecibels = 106.0;
    double GainBandwidth = 1e6;
    /// Volts per microsecond, as datasheets specify.
    double SlewRate = 0.5;
    double PhaseMarginDegrees = 60.0;
    double InputBiasCurrent = 80e-9;
    double CommonModeRejectionDecibels = 90.0;
    double OutputResistance = 75.0;
    double ShortCircuitCurrent = 25e-3;
    double PositiveHeadroom = 1.0;
    double NegativeHeadroom = 1.0;
    double SupplyCurrent = 1.7e-3;
    double InputOffsetVoltage = 0.0;

    bool operator==(const BoyleOpAmpParameters &) const = default;
};

enum class SemiconductorPolarity {
    NPN,
    PNP,
    NMOS,
    PMOS
};

enum class LiveQuantity {
    RESISTANCE,
    DC_VOLTAGE
};

struct LiveChange {
    std::string Element;
    LiveQuantity Quantity = LiveQuantity::RESISTANCE;
    double Value = 0.0;

    bool operator==(const LiveChange &) const = default;
};

/**
 * @enum    LogicGateType
 * @brief   Kinds of smooth continuous logic gates.
 */
enum class LogicGateType {
    NOT,
    BUFFER,
    AND,
    NAND,
    OR,
    NOR,
    XOR,
    XNOR,
    SCHMITT_TRIGGER,
};

/**
 * @enum    DigitalLevel
 * @brief   Discrete logic states for co-simulation and digital-analog boundary.
 */
enum class DigitalLevel {
    LOW,
    HIGH,
    UNKNOWN,
    HIGH_Z,
};

/**
 * @struct  SmoothLogicParameters
 * @brief   Physical and analog parameters for C^inf smooth continuous logic gates.
 */
struct SmoothLogicParameters {
    /// Low logic level voltage in Volts.
    double VLow = 0.0;
    /// High logic level voltage in Volts.
    double VHigh = 5.0;
    /// Input switching threshold in Volts.
    double VThreshold = 2.5;
    /// Delta transition width in Volts for smooth tanh saturation.
    double TransitionWidth = 0.1;
    /// Inertial dynamic propagation delay in seconds.
    double PropagationDelay = 1e-9;
    /// Thevenin output stage resistance in Ohms.
    double OutputResistance = 50.0;
    /// High-impedance input leakage resistance in Ohms.
    double InputResistance = 1e9;
    /// Hysteresis voltage span for Schmitt triggers in Volts.
    double Hysteresis = 0.5;

    bool operator==(const SmoothLogicParameters &) const = default;
};

} // namespace Nodalys

#endif // NODALYS_TYPES_H
