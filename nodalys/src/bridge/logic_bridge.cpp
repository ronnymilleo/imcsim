/**
 * @file    logic_bridge.cpp
 * @brief   Bridge interfaces between discrete digital levels and continuous analog waveforms for co-simulation.
 */

#include "nodalys/logic_bridge.h"

#include <algorithm>
#include <cmath>

namespace nodalys {

/**
 * @brief   Quantizes a continuous analog voltage into a discrete digital logic level.
 * @param[in] voltage  Analog voltage to test.
 * @param[in] params   Logic family parameters specifying switching thresholds.
 * @param[in] margin   Safety noise margin in Volts around threshold.
 * @return  Quantized digital level (LOW, HIGH, or UNKNOWN when in the noise margin band).
 */
DigitalLevel VoltageToDigital(const double voltage, const SmoothLogicParameters &params, const double margin) {
    const double low_bound = params.VThreshold - std::abs(margin);
    const double high_bound = params.VThreshold + std::abs(margin);

    if (voltage <= low_bound) {
        return DigitalLevel::LOW;
    }
    if (voltage >= high_bound) {
        return DigitalLevel::HIGH;
    }
    return DigitalLevel::UNKNOWN;
}

/**
 * @brief   Maps a discrete digital level to its nominal analog voltage representation.
 * @param[in] level   Discrete digital level.
 * @param[in] params  Logic family parameters.
 * @return  Corresponding continuous target voltage.
 */
double DigitalToVoltage(const DigitalLevel level, const SmoothLogicParameters &params) {
    switch (level) {
    case DigitalLevel::LOW:
        return params.VLow;
    case DigitalLevel::HIGH:
        return params.VHigh;
    case DigitalLevel::UNKNOWN:
    case DigitalLevel::HIGH_Z:
    default:
        return params.VThreshold;
    }
}

/**
 * @brief   Formats a digital level into its standard text representation.
 * @param[in] level  Digital level to format.
 * @return  String such as "0", "1", "X", or "Z".
 */
std::string FormatDigitalLevel(const DigitalLevel level) {
    switch (level) {
    case DigitalLevel::LOW:
        return "0";
    case DigitalLevel::HIGH:
        return "1";
    case DigitalLevel::UNKNOWN:
        return "X";
    case DigitalLevel::HIGH_Z:
        return "Z";
    default:
        return "X";
    }
}

/**
 * @brief   Parses a single character or token into a digital logic level.
 * @param[in] text  Text to parse.
 * @return  Parsed DigitalLevel, or nullopt if unrecognized.
 */
std::optional<DigitalLevel> ParseDigitalLevel(const std::string_view text) {
    if (text == "0" || text == "LOW" || text == "low") {
        return DigitalLevel::LOW;
    }
    if (text == "1" || text == "HIGH" || text == "high") {
        return DigitalLevel::HIGH;
    }
    if (text == "X" || text == "x" || text == "UNKNOWN" || text == "unknown") {
        return DigitalLevel::UNKNOWN;
    }
    if (text == "Z" || text == "z" || text == "HIGH_Z" || text == "high_z") {
        return DigitalLevel::HIGH_Z;
    }
    return std::nullopt;
}

/**
 * @brief   Evaluates an infinitely differentiable C^inf smooth voltage transition at a specific time.
 * @param[in] transition  Transition start time, duration, and voltages.
 * @param[in] time        Evaluation time in seconds.
 * @return  Continuous voltage value at the given time.
 */
double EvaluateSmoothTransition(const SmoothTransition &transition, const double time) {
    if (transition.Duration <= 0.0) {
        return (time >= transition.StartTime) ? transition.TargetVoltage : transition.StartVoltage;
    }

    const double t_mid = transition.StartTime + 0.5 * transition.Duration;
    const double argument = 4.0 * (time - t_mid) / transition.Duration;
    const double clamped = std::clamp(argument, -40.0, 40.0);
    const double scale = 0.5 * (1.0 + std::tanh(clamped));

    return transition.StartVoltage + (transition.TargetVoltage - transition.StartVoltage) * scale;
}

} // namespace nodalys
