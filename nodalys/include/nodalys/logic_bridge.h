/**
 * @file    logic_bridge.h
 * @brief   Bridge interfaces between discrete digital levels and continuous analog waveforms for co-simulation.
 */

#ifndef NODALYS_LOGIC_BRIDGE_H
#define NODALYS_LOGIC_BRIDGE_H

#include "nodalys/types.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nodalys {

/**
 * @enum    PortDirection
 * @brief   Signal flow direction of a co-simulation logic pin.
 */
enum class PortDirection {
    INPUT,
    OUTPUT,
    BIDIRECTIONAL,
};

/**
 * @struct  CoSimPin
 * @brief   A digital-to-analog or analog-to-digital pin description for co-simulation adapters.
 */
struct CoSimPin {
    std::string Name;
    int Node = -1;
    PortDirection Direction = PortDirection::INPUT;
    SmoothLogicParameters Parameters;

    bool operator==(const CoSimPin &) const = default;
};

/**
 * @struct  SmoothTransition
 * @brief   Parameters for generating an infinitely differentiable C^inf voltage ramp between logic states.
 */
struct SmoothTransition {
    double StartTime = 0.0;
    double Duration = 1e-9;
    double StartVoltage = 0.0;
    double TargetVoltage = 5.0;

    bool operator==(const SmoothTransition &) const = default;
};

// Logic Level Conversion
[[nodiscard]] DigitalLevel VoltageToDigital(double voltage, const SmoothLogicParameters &params, double margin = 0.5);
[[nodiscard]] double DigitalToVoltage(DigitalLevel level, const SmoothLogicParameters &params);
[[nodiscard]] std::string FormatDigitalLevel(DigitalLevel level);
[[nodiscard]] std::optional<DigitalLevel> ParseDigitalLevel(std::string_view text);

// Smooth Waveforms
[[nodiscard]] double EvaluateSmoothTransition(const SmoothTransition &transition, double time);

} // namespace nodalys

#endif // NODALYS_LOGIC_BRIDGE_H
