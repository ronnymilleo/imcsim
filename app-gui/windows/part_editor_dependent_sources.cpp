/**
 * @file    part_editor_dependent_sources.cpp
 * @brief   The properties of controlled and behavioral sources: their gain, the quantity they follow and their
 * expression.
 */

#include "windows/part_editor_dependent_sources.h"

#include "base/theme.h"
#include "circuit/behavioral_expression.h"
#include "circuit/circuit_currents.h"
#include "misc/cpp/imgui_stdlib.h"
#include "simulation/circuit_checks.h"
#include "simulation/simulator.h"
#include "windows/part_fields.h"
#include <format>
#include <optional>

namespace GUI {

namespace {

/// Width of the expression field of a behavioral source, in font sizes, so a typical expression shows whole.
constexpr float EXPRESSION_FIELD_WIDTH = 20.0f;
/// Width at which the help below the expression wraps, in font sizes.
constexpr float EXPRESSION_HELP_WIDTH = 24.0f;

/**
 * @brief   Any current the results report can control the source, except its own; they read as the plots name them.
 */
void DrawControllingCurrent(Core::ControlledSource &source, Schematic &schematic) {
    const std::string &current = source.GetControllingCurrent();
    const std::string preview = current.empty() ? "(none)" : std::format("I({})", current);
    DrawFieldLabel("Follows");
    if (ImGui::BeginCombo("##Follows", preview.c_str())) {
        for (const auto &element : schematic.GetElements()) {
            if (&element->GetComponent() == &source) {
                continue;
            }
            for (const std::string &name : Core::GetCurrentNames(element->GetComponent())) {
                const bool selected = name == current;
                if (ImGui::Selectable(std::format("I({})", name).c_str(), selected) && !selected) {
                    source.SetControllingCurrent(name);
                    schematic.MarkModified();
                }
            }
        }
        ImGui::EndCombo();
    }
    if (current.empty()) {
        ImGui::TextColored(GetWarningTextColor(), "Pick the current the source follows");
    }
}

} // namespace

/**
 * @brief   The gain is the ratio of output to control, so the line below spells out what it multiplies.
 */
void DrawControlledSource(Core::ControlledSource &source, ValueField &main_value, Schematic &schematic) {
    if (const std::optional<double> value = main_value.Draw("Gain", source.GetUnit(), AnyValue)) {
        source.SetValue(*value);
        schematic.MarkModified();
    }
    const Core::ComponentType type = source.GetType();
    if (Core::IsCurrentControlled(type)) {
        DrawControllingCurrent(source, schematic);
    }
    if (type == Core::ComponentType::VCVS) {
        ImGui::TextDisabled("V(1, 2) = gain x V(+, -) of the control pins");
    } else if (type == Core::ComponentType::VCCS) {
        ImGui::TextDisabled("Pushes gain x V(+, -) from terminal 1 to 2");
    } else if (type == Core::ComponentType::CCCS) {
        ImGui::TextDisabled("Pushes gain x I from terminal 1 to 2");
    } else {
        ImGui::TextDisabled("V(1, 2) = gain x I");
    }
    ImGui::TextDisabled("A negative gain inverts the output");
}

/**
 * @brief   The expression applies as soon as it parses, so a half-typed one keeps the last that did; below it, what is
 *          wrong with it in the circuit as it is now, or what it may use.
 */
void DrawBehavioralSource(Core::BehavioralSource &source, std::string &expression_text, Schematic &schematic) {
    const bool is_voltage = source.GetType() == Core::ComponentType::BEHAVIORAL_VOLTAGE;
    DrawFieldLabel(is_voltage ? "V =" : "I =");
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * EXPRESSION_FIELD_WIDTH);
    const bool changed = ImGui::InputText("##expression", &expression_text);
    const auto parsed = Core::ParseBehavioralExpression(expression_text);
    if (changed && parsed) {
        source.SetExpression(expression_text);
        schematic.MarkModified();
    }
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetFontSize() * EXPRESSION_HELP_WIDTH);
    if (!parsed) {
        ImGui::TextColored(GetErrorTextColor(), "%s", parsed.error().c_str());
    } else if (const std::optional<std::string> problem =
                   Core::CheckBehavioralExpression(schematic.BuildCircuit(), source)) {
        ImGui::TextColored(GetErrorTextColor(), "%s", problem->c_str());
    }
    ImGui::TextDisabled("%s", is_voltage ? "Drives this voltage from + to -" : "Pushes this current from + to -");
    ImGui::TextDisabled("V(OUT), V(IN, OUT) by net label, I(R1), I(Q1.C), numbers such as 2.2k, + - * / ^, and "
                        "ngspice functions such as tanh(), exp(), abs(), min(), max(), x > 1 ? a : b, time, temper");
    ImGui::PopTextWrapPos();
}

} // namespace GUI
