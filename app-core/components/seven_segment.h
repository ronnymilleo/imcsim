/**
 * @file    seven_segment.h
 * @brief   A 7-segment display that lights each segment whose node is at a logic 1, in the simulation model.
 */

#ifndef IMCSIM_SEVEN_SEGMENT_H
#define IMCSIM_SEVEN_SEGMENT_H

#include "components/component.h"
#include <array>
#include <memory>

namespace Core {

/// The segments in terminal order: a at the top, then clockwise b to f, and g across the middle.
inline constexpr std::array<const char *, 7> SEGMENT_NAMES = {"a", "b", "c", "d", "e", "f", "g"};

/**
 * @struct  SevenSegmentParameters
 * @brief   The logic level the voltages of the segments are read against, in volts.
 */
struct SevenSegmentParameters {
    double LogicLevel = 5.0;
};

/**
 * @class   SevenSegmentDisplay
 * @brief   A display with one input per segment, a to g, that takes no part in the simulation.
 * @details Like the logic output, it has no netlist line, no name and no value: the schematic reads the voltage of
 *          each input, at or above 70% of the logic level as 1 and at or below 30% as 0, and lights the segments that
 *          read 1, so a BCD decoder shows its digit without LEDs, resistors or a Ground.
 */
class SevenSegmentDisplay : public Component {
public:
    SevenSegmentDisplay();
    ~SevenSegmentDisplay() override = default;

    std::unique_ptr<Component> Clone() const override;

    const SevenSegmentParameters &GetParameters() const;
    void SetParameters(const SevenSegmentParameters &parameters);
    static bool IsValidParameters(const SevenSegmentParameters &parameters);

private:
    SevenSegmentParameters m_Parameters;
};

} // namespace Core

#endif // IMCSIM_SEVEN_SEGMENT_H
