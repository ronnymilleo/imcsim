/**
 * @file    part_editor_logic.cpp
 * @brief   The properties of the digital parts: gates and flip-flops, clocks, logic inputs, outputs and displays.
 */

#include "windows/part_editor_logic.h"

#include "windows/part_fields.h"
#include <string>

namespace GUI {

namespace {

constexpr auto LOGIC_FIELDS = std::to_array<ParameterField<Core::LogicParameters>>({
    {"Logic level", &Core::LogicParameters::LogicLevel, "V"},
    {"Delay", &Core::LogicParameters::PropagationDelay, "s"},
});

/// A clock picks its own rise and fall times, so the user sets only these figures.
constexpr auto CLOCK_FIELDS = std::to_array<ParameterField<Core::ClockParameters>>({
    {"Frequency", &Core::ClockParameters::Frequency, "Hz"},
    {"Duty", &Core::ClockParameters::DutyCycle, "%", 100.0},
    {"Logic level", &Core::ClockParameters::LogicLevel, "V"},
    {"Start delay", &Core::ClockParameters::StartDelay, "s"},
});

constexpr auto LOGIC_INPUT_FIELDS = std::to_array<ParameterField<Core::LogicInputParameters>>({
    {"Logic level", &Core::LogicInputParameters::LogicLevel, "V"},
});

constexpr auto LOGIC_OUTPUT_FIELDS = std::to_array<ParameterField<Core::LogicOutputParameters>>({
    {"Logic level", &Core::LogicOutputParameters::LogicLevel, "V"},
});

constexpr auto SEVEN_SEGMENT_FIELDS = std::to_array<ParameterField<Core::SevenSegmentParameters>>({
    {"Logic level", &Core::SevenSegmentParameters::LogicLevel, "V"},
});

} // namespace

/**
 * @brief   The parameters of the digital parts, the clock, and the logic inputs, outputs and displays.
 */
void LoadLogicFields(LogicFields &fields, const Core::Component &component) {
    const Core::ComponentType type = component.GetType();
    if (Core::IsLogicPart(type)) {
        LoadParameterFields(LOGIC_FIELDS, fields.Logic,
                            static_cast<const Core::LogicPart &>(component).GetParameters());
    } else if (type == Core::ComponentType::CLOCK) {
        LoadParameterFields(CLOCK_FIELDS, fields.Clock, static_cast<const Core::Clock &>(component).GetParameters());
    } else if (type == Core::ComponentType::LOGIC_INPUT) {
        LoadParameterFields(LOGIC_INPUT_FIELDS, fields.Input,
                            static_cast<const Core::LogicInput &>(component).GetParameters());
    } else if (type == Core::ComponentType::LOGIC_OUTPUT) {
        LoadParameterFields(LOGIC_OUTPUT_FIELDS, fields.Output,
                            static_cast<const Core::LogicOutput &>(component).GetParameters());
    } else if (type == Core::ComponentType::SEVEN_SEGMENT) {
        LoadParameterFields(SEVEN_SEGMENT_FIELDS, fields.SevenSegment,
                            static_cast<const Core::SevenSegmentDisplay &>(component).GetParameters());
    }
}

/**
 * @brief   The levels the part swaps at follow from the logic level: it reads 1 above 70% of it and 0 below 30%.
 */
void DrawLogicPart(Core::LogicPart &part, LogicFields &fields, Schematic &schematic) {
    Core::LogicParameters parameters = part.GetParameters();
    const auto is_valid = [](const Core::LogicParameters &candidate) {
        return Core::LogicPart::IsValidParameters(candidate);
    };
    if (DrawParameterFields(LOGIC_FIELDS, fields.Logic, "", is_valid, parameters)) {
        part.SetParameters(parameters);
        schematic.MarkModified();
    }
    if (Core::HasVariableInputs(part.GetType())) {
        // The pins that go away when the count falls are left with the wires they had, which stay loose
        const int input_count = part.GetInputCount();
        DrawFieldLabel("Inputs");
        if (ImGui::BeginCombo("##inputs", std::to_string(input_count).c_str())) {
            for (int candidate = Core::MIN_LOGIC_INPUTS; candidate <= Core::MAX_LOGIC_INPUTS; ++candidate) {
                if (ImGui::Selectable(std::to_string(candidate).c_str(), candidate == input_count) &&
                    candidate != input_count) {
                    part.SetInputCount(candidate);
                    schematic.MarkModified();
                }
            }
            ImGui::EndCombo();
        }
    }
    ImGui::TextDisabled("Inputs read 1 above 70%% of the level and 0 below 30%%; an open input reads 0");
}

/**
 * @brief   The clock picks its own rise and fall times; the frequency sets the period and the duty the share that is
 *          high.
 */
void DrawClock(Core::Clock &clock, LogicFields &fields, Schematic &schematic) {
    Core::ClockParameters parameters = clock.GetParameters();
    const auto is_valid = [](const Core::ClockParameters &candidate) {
        return Core::Clock::IsValidParameters(candidate);
    };
    if (DrawParameterFields(CLOCK_FIELDS, fields.Clock, "", is_valid, parameters)) {
        clock.SetParameters(parameters);
        schematic.MarkModified();
    }
    ImGui::TextDisabled("Duty is the share of the period above half the level, from 0 to 100%%");
    ImGui::TextDisabled("The start delay holds the clock low; at 0 the first rising edge is at t = 0");
}

/**
 * @brief   A constant source from the part to ground: high drives the logic level and low 0 V, so a latch has a defined
 *          start.
 */
void DrawLogicInput(Core::LogicInput &input, LogicFields &fields, Schematic &schematic) {
    bool high = input.IsHigh();
    if (ImGui::Checkbox("High", &high)) {
        input.SetHigh(high);
        schematic.MarkLiveChanged(input);
    }
    Core::LogicInputParameters parameters = input.GetParameters();
    const auto is_valid = [](const Core::LogicInputParameters &candidate) {
        return Core::LogicInput::IsValidParameters(candidate);
    };
    if (DrawParameterFields(LOGIC_INPUT_FIELDS, fields.Input, "", is_valid, parameters)) {
        input.SetParameters(parameters);
        schematic.MarkModified();
    }
    ImGui::TextDisabled("A source from its terminal to ground, so it gives a gate a defined level");
}

/**
 * @brief   The logic level the output reads its node against; it changes no result, only the reading.
 */
void DrawLogicOutput(Core::LogicOutput &output, LogicFields &fields, Schematic &schematic) {
    Core::LogicOutputParameters parameters = output.GetParameters();
    const auto is_valid = [](const Core::LogicOutputParameters &candidate) {
        return Core::LogicOutput::IsValidParameters(candidate);
    };
    if (DrawParameterFields(LOGIC_OUTPUT_FIELDS, fields.Output, "", is_valid, parameters)) {
        output.SetParameters(parameters);
        schematic.MarkModified();
    }
    ImGui::TextDisabled("Shows 1 at 70%% of the level or above, 0 at 30%% or below, X between; it adds nothing to the "
                        "circuit");
}

/**
 * @brief   The logic level the display reads its inputs against; it changes no result, only which segments light.
 */
void DrawSevenSegment(Core::SevenSegmentDisplay &display, LogicFields &fields, Schematic &schematic) {
    Core::SevenSegmentParameters parameters = display.GetParameters();
    const auto is_valid = [](const Core::SevenSegmentParameters &candidate) {
        return Core::SevenSegmentDisplay::IsValidParameters(candidate);
    };
    if (DrawParameterFields(SEVEN_SEGMENT_FIELDS, fields.SevenSegment, "", is_valid, parameters)) {
        display.SetParameters(parameters);
        schematic.MarkModified();
    }
    ImGui::TextDisabled("Lights a segment at 70%% of the level or above; it adds nothing to the circuit");
}

} // namespace GUI
