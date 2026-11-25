/**
 * @file    types.h
 * @brief   Common types, device parameter structures, and tolerance options for Nodalys.
 */

#ifndef NODALYS_TYPES_H
#define NODALYS_TYPES_H

#include "nodalys/waveform_types.h"
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace Nodalys {

/// Unique integer identifier for a circuit node. Ground is always node 0.
using NodeId = int;
inline constexpr NodeId GROUND_NODE = 0;

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
 * @enum    IntegrationMethod
 * @brief   Formula that turns each charge or flux derivative of a transient into a companion model.
 * @details Both are second order. Backward Euler always takes the first step and the step that leaves a source
 *          corner, whatever the method.
 */
enum class IntegrationMethod {
    TRAPEZOIDAL, ///< A-stable, keeps the amplitude of a lossless resonance, but rings on a stiff mode.
    /// Variable-step BDF-2 (SPICE `method=gear`): L-stable, so it settles stiff switching without ringing, at the
    /// cost of some numerical damping of a lightly damped resonance.
    GEAR2
};

/**
 * @enum    LiveQuantity
 * @brief   Device parameter kinds that can be modified interactively during a transient simulation.
 */
enum class LiveQuantity {
    RESISTANCE,
    DC_VOLTAGE
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
 * @enum    SemiconductorPolarity
 * @brief   Conduction polarity of a bipolar or MOS transistor.
 */
enum class SemiconductorPolarity {
    NPN,
    PNP,
    NMOS,
    PMOS
};

// Circuit and simulation records

/**
 * @struct  CircuitPort
 * @brief   RF scattering parameter port definition.
 */
struct CircuitPort {
    int Number = 1;
    NodeId Node = 0;
    double ReferenceOhms = 50.0;
    NodeId NegativeNode = 0; ///< Reference terminal of the port (`0` for ground).

    /**
     * @brief   Compares two port descriptors for equality.
     * @return  True when all fields match.
     */
    bool operator==(const CircuitPort &) const = default;
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
 * @struct  LiveChange
 * @brief   An interactive parameter update queued for an element during a live transient run.
 */
struct LiveChange {
    std::string Element;
    LiveQuantity Quantity = LiveQuantity::RESISTANCE;
    double Value = 0.0;

    /**
     * @brief   Compares two live parameter change requests for equality.
     * @return  True when all fields match.
     */
    bool operator==(const LiveChange &) const = default;
};

// Device models

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
    double BetaExponent = 0.0; ///< Exponent `XTB` of the temperature ratio in the forward and reverse current gains.

    /**
     * @brief   Compares two BjtParameters values for equality.
     * @return  True when all fields match.
     */
    bool operator==(const BjtParameters &) const = default;
};

/**
 * @struct  BoyleOpAmpParameters
 * @brief   Datasheet specifications for the Boyle op-amp macromodel (IEEE JSSC 1974).
 */
struct BoyleOpAmpParameters {
    bool IsPnpInput = false;
    double OpenLoopGainDecibels = 106.0;
    double GainBandwidth = 1e6;
    double SlewRateVoltsPerMicrosecond = 0.5; ///< Volts per microsecond, as datasheets specify.
    double PhaseMarginDegrees = 60.0;
    double InputBiasCurrent = 80e-9;
    double CommonModeRejectionDecibels = 90.0;
    double OutputResistance = 75.0;
    double ShortCircuitCurrent = 25e-3;
    double PositiveHeadroom = 1.0;
    double NegativeHeadroom = 1.0;
    double SupplyCurrent = 1.7e-3;
    double InputOffsetVoltage = 0.0;

    /**
     * @brief   Compares two BoyleOpAmpParameters values for equality.
     * @return  True when all fields match.
     */
    bool operator==(const BoyleOpAmpParameters &) const = default;
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

    /**
     * @brief   Compares two DiodeParameters values for equality.
     * @return  True when all fields match.
     */
    bool operator==(const DiodeParameters &) const = default;
};

/**
 * @struct  FuseParameters
 * @brief   Datasheet figures of a fuse that blows by I squared t.
 * @details The element heats with the current squared and cools with the time constant I2t / Ih^2, so it never
 *          blows at or below the rated current, and blows a short overload once the current squared times its
 *          duration reaches the melting I2t.
 */
struct FuseParameters {
    double RatedCurrent = 1.0; ///< Largest current the fuse carries for ever, in amperes.
    double MeltingI2t = 1.045; ///< Current squared times time that melts the element in a short overload, in A^2 s.
    double ColdResistance = 0.0964;
    double BlownResistance = 1e9;

    /**
     * @brief   Compares two FuseParameters values for equality.
     * @return  True when all fields match.
     */
    bool operator==(const FuseParameters &) const = default;
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

    /**
     * @brief   Compares two MosfetParameters values for equality.
     * @return  True when all fields match.
     */
    bool operator==(const MosfetParameters &) const = default;
};

/**
 * @struct  SmoothLogicParameters
 * @brief   Physical and analog parameters for C^inf smooth continuous logic gates.
 */
struct SmoothLogicParameters {
    double VLow = 0.0;              ///< Low logic level voltage in Volts.
    double VHigh = 5.0;             ///< High logic level voltage in Volts.
    double VThreshold = 2.5;        ///< Input switching threshold in Volts.
    double TransitionWidth = 0.1;   ///< Delta transition width in Volts for smooth tanh saturation.
    double PropagationDelay = 1e-9; ///< Inertial dynamic propagation delay in seconds.
    double OutputResistance = 50.0; ///< Thevenin output stage resistance in Ohms.
    double InputResistance = 1e9;   ///< High-impedance input leakage resistance in Ohms.
    double Hysteresis = 0.5;        ///< Hysteresis voltage span for Schmitt triggers in Volts.

    /**
     * @brief   Compares two SmoothLogicParameters values for equality.
     * @return  True when all fields match.
     */
    bool operator==(const SmoothLogicParameters &) const = default;
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

    /**
     * @brief   Compares two SwitchParameters values for equality.
     * @return  True when all fields match.
     */
    bool operator==(const SwitchParameters &) const = default;
};

/**
 * @struct  TransmissionLineParameters
 * @brief   Electrical parameters of a uniform two-port TEM transmission line (lossless or dispersive lossy).
 * @details The lossless backbone is set by `ImpedanceOhms` (\f$Z_0 > 0\f$) and `DelaySeconds` (\f$T_d > 0\f$), giving
 *          total inductance \f$L = Z_0 T_d\f$ and total capacitance \f$C = T_d / Z_0\f$. Optional conductor and
 *          dielectric losses add frequency-dependent series resistance
 *          \f$R(f) = R_{\text{DC}} + R_{\text{skin}} \sqrt{f / f_{\text{ref}}}\f$ and shunt conductance
 *          \f$G(f) = G_{\text{DC}} + 2\pi f C \tan\delta\f$ in frequency-domain and Harmonic Balance analyses; a line
 *          with any non-zero loss parameter is frequency-domain only and is refused in time-domain transient runs.
 */
struct TransmissionLineParameters {
    double ImpedanceOhms = 50.0;       ///< Lossless characteristic impedance \f$Z_0\f$ in Ohms.
    double DelaySeconds = 1e-9;        ///< One-way propagation delay \f$T_d\f$ in seconds.
    double DcResistanceOhms = 0.0;     ///< Total end-to-end DC series conductor resistance \f$R_{\text{DC}}\f$ in Ohms.
    double SkinResistanceOhms = 0.0;   ///< Skin-effect AC resistance \f$R_{\text{skin}}\f$ at `ReferenceFrequencyHz`.
    double DcConductanceSiemens = 0.0; ///< Total end-to-end DC shunt conductance \f$G_{\text{DC}}\f$ in Siemens.
    double LossTangent = 0.0;          ///< Dielectric loss tangent \f$\tan\delta\f$.
    double ReferenceFrequencyHz = 1e9; ///< Reference frequency \f$f_{\text{ref}}\f$ in Hertz for `SkinResistanceOhms`.

    /**
     * @brief   Reports whether any conductor or dielectric loss parameter is active.
     * @return  True when any of DC resistance, skin resistance, DC conductance, or loss tangent is positive.
     */
    [[nodiscard]] bool IsLossy() const {
        return DcResistanceOhms > 0.0 || SkinResistanceOhms > 0.0 || DcConductanceSiemens > 0.0 || LossTangent > 0.0;
    }

    /**
     * @brief   Compares two TransmissionLineParameters values for equality.
     * @return  True when all fields match.
     */
    bool operator==(const TransmissionLineParameters &) const = default;
};

} // namespace Nodalys

#endif // NODALYS_TYPES_H
