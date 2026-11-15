/**
 * @file    part_editor_logic.h
 * @brief   The properties of the digital parts: gates and flip-flops, clocks, logic inputs, outputs and displays.
 */

#ifndef IMCSIM_PART_EDITOR_LOGIC_H
#define IMCSIM_PART_EDITOR_LOGIC_H

#include "base/value_field.h"
#include "circuit/components/clock.h"
#include "circuit/components/component.h"
#include "circuit/components/logic_input.h"
#include "circuit/components/logic_output.h"
#include "circuit/components/logic_part.h"
#include "circuit/components/seven_segment.h"
#include "schematic/schematic.h"
#include <array>

namespace GUI {

/**
 * @struct  LogicFields
 * @brief   The texts being edited for the parameters of the digital parts.
 * @details One field per member of each set of parameters.
 */
struct LogicFields {
    /// One per member of Core::LogicParameters.
    std::array<ValueField, 2> Logic;
    /// One per member of Core::ClockParameters.
    std::array<ValueField, 4> Clock;
    /// One per member of Core::LogicInputParameters.
    std::array<ValueField, 1> Input;
    /// One per member of Core::LogicOutputParameters.
    std::array<ValueField, 1> Output;
    /// One per member of Core::SevenSegmentParameters.
    std::array<ValueField, 1> SevenSegment;
};

// Loading
void LoadLogicFields(LogicFields &fields, const Core::Component &component);

// Drawing
void DrawLogicPart(Core::LogicPart &part, LogicFields &fields, Schematic &schematic);
void DrawClock(Core::Clock &clock, LogicFields &fields, Schematic &schematic);
void DrawLogicInput(Core::LogicInput &input, LogicFields &fields, Schematic &schematic);
void DrawLogicOutput(Core::LogicOutput &output, LogicFields &fields, Schematic &schematic);
void DrawSevenSegment(Core::SevenSegmentDisplay &display, LogicFields &fields, Schematic &schematic);

} // namespace GUI

#endif // IMCSIM_PART_EDITOR_LOGIC_H
