/**
 * @file    part_editor_passives.cpp
 * @brief   The properties of parts with a value: resistors, capacitors, inductors and the like, their initial
 * condition, tolerance and wiper.
 */

#include "windows/part_editor_passives.h"

#include "circuit/components/source.h"
#include "circuit/components/subcircuit.h"
#include "spice/standard_values.h"
#include "windows/part_fields.h"
#include <format>
#include <optional>

namespace GUI {

namespace {

/**
 * @brief   Inside a block, a resistor, capacitor or inductor can take its value from a parameter of the block, which
 *          each instance sets.
 * @details Returns whether it does, in which case it has no value of its own to edit.
 */
bool DrawParameterChoice(Core::Component &component, ValueField &main_value,
                         const std::vector<std::string> &block_parameters, Schematic &schematic) {
    const std::optional<std::string> &expression = component.GetValueExpression();
    if (!component.CanFollowParameter() || (block_parameters.empty() && !expression)) {
        return false;
    }
    constexpr const char *OWN_VALUE = "(its own value)";
    const std::string preview = expression ? std::string(Core::SubcircuitInstance::GetExpressionParameter(*expression))
                                           : std::string(OWN_VALUE);
    DrawFieldLabel("Follows");
    if (ImGui::BeginCombo("##follows", preview.c_str())) {
        if (ImGui::Selectable(OWN_VALUE, !expression) && expression) {
            component.ClearValueExpression();
            main_value.Load(component.GetValue());
            schematic.MarkModified();
        }
        for (const std::string &name : block_parameters) {
            if (ImGui::Selectable(name.c_str(), name == preview) && name != preview) {
                component.SetValueExpression(std::format("{{{}}}", name));
                schematic.MarkModified();
            }
        }
        ImGui::EndCombo();
    }
    if (!component.GetValueExpression()) {
        return false;
    }
    ImGui::TextDisabled("Set by each instance, or the parameter's default");
    return true;
}

} // namespace

/**
 * @brief   The initial condition and the tolerance of the part, or empty fields when it has none.
 */
void LoadPassiveFields(PassiveFields &fields, const Core::Component &component) {
    fields.InitialCondition.LoadOptional(component.GetInitialCondition());
    fields.Tolerance.LoadOptional(component.GetTolerance());
}

void DrawValue(Core::Component &component, ValueField &main_value, const std::vector<std::string> &block_parameters,
               const Preferences &preferences, Schematic &schematic) {
    if (DrawParameterChoice(component, main_value, block_parameters, schematic)) {
        return;
    }
    const auto is_valid = [&component](const double value) { return component.IsValidValue(value); };
    // Standard values are for the passive parts; the value of a source or a gain is whatever the circuit needs
    const Core::ComponentType type = component.GetType();
    const bool passive = type == Core::ComponentType::RESISTOR || type == Core::ComponentType::CAPACITOR ||
                         type == Core::ComponentType::INDUCTOR;
    const Core::ValueSeries series = passive ? preferences.ValueSeries : Core::ValueSeries::FREE;
    if (const std::optional<double> value = main_value.Draw("Value", component.GetUnit(), is_valid, series)) {
        component.SetValue(*value);
        schematic.MarkModified();
    }
    if (series != Core::ValueSeries::FREE) {
        ImGui::TextDisabled("Standard values: %s, %g%%", Core::GetSeriesName(series),
                            Core::GetSeriesTolerancePercent(series));
    }
}

/**
 * @brief   A capacitor's initial condition is a voltage and an inductor's a current; leaving the field empty means no
 *          initial condition, but once any part has one the whole transient runs with uic and an empty part starts at
 *          zero.
 */
void DrawInitialCondition(Core::Component &component, PassiveFields &fields, Schematic &schematic) {
    const bool is_capacitor = component.GetType() == Core::ComponentType::CAPACITOR;
    const GUI::OptionalValueEdit edit =
        fields.InitialCondition.DrawOptional("Initial", is_capacitor ? "V" : "A", AnyValue);
    if (edit.Changed) {
        if (edit.Value) {
            component.SetInitialCondition(*edit.Value);
        } else {
            component.ClearInitialCondition();
        }
        schematic.MarkModified();
    }
    ImGui::TextDisabled("Skips the operating point (uic); parts left empty start at zero");
}

/**
 * @brief   The percent the value may be off in a Monte Carlo run; an empty field means the part keeps its value. A
 *          voltage source only varies while it is a DC one, and the series of standard values of the preferences
 *          offers its own tolerance for a resistor, capacitor or inductor.
 */
void DrawTolerance(Core::Component &component, PassiveFields &fields, const Preferences &preferences,
                   Schematic &schematic) {
    const bool is_source = Core::IsSource(component.GetType());
    if (is_source && static_cast<const Core::Source &>(component).GetSourceType() != Core::Source::SourceType::DC) {
        return;
    }
    const GUI::OptionalValueEdit edit = fields.Tolerance.DrawOptional(
        "Tolerance", "%", [](const double percent) { return percent > 0.0 && percent <= Core::MAX_TOLERANCE_PERCENT; });
    if (edit.Changed) {
        if (edit.Value) {
            component.SetTolerance(*edit.Value);
        } else {
            component.ClearTolerance();
        }
        schematic.MarkModified();
    }
    const double series_percent = Core::GetSeriesTolerancePercent(preferences.ValueSeries);
    if (!is_source && !component.HasTolerance() && series_percent > 0.0 &&
        ImGui::SmallButton(
            std::format("Use {} ({:g}%)", Core::GetSeriesName(preferences.ValueSeries), series_percent).c_str())) {
        component.SetTolerance(series_percent);
        fields.Tolerance.LoadOptional(component.GetTolerance());
        schematic.MarkModified();
    }
    ImGui::TextDisabled("Monte Carlo draws the value within it; empty keeps the value");
}

/**
 * @brief   The wiper as a share of the track, counted from terminal 1.
 */
void DrawWiper(Core::Potentiometer &pot, Schematic &schematic) {
    DrawFieldLabel("Wiper");
    auto percent = static_cast<float>(pot.GetWiper() * 100.0);
    if (ImGui::SliderFloat("##wiper", &percent, 0.0f, 100.0f, "%.0f%%")) {
        pot.SetWiper(static_cast<double>(percent) / 100.0);
        schematic.MarkLiveChanged(pot);
    }
    ImGui::TextDisabled("0%% is terminal 1 and 100%% is terminal 2");
}

} // namespace GUI
