/**
 * @file    properties_window.cpp
 * @brief   Window that shows and edits the component selected in the schematic.
 */

#include "properties_window.h"

#include "spice_value.h"
#include "theme.h"
#include <array>
#include <cstddef>
#include <format>
#include <optional>
#include <string>

namespace GUI {

namespace {

// In font sizes, so the layout follows the DPI scale; matches the labels of ValueField
constexpr float LabelWidth = 6.0f;
constexpr float InputWidth = 8.0f;

bool AnyValue(double /*value*/) {
    return true;
}

/**
 * @struct  ParameterField
 * @brief   One member of a parameter set, such as Core::PulseParameters, as a field of the Properties window.
 */
template <typename Parameters> struct ParameterField {
    const char *Label;
    double Parameters::*Value;
    // No unit means the unit of the source, volts or amperes
    const char *Unit;
};

constexpr auto ACFields = std::to_array<ParameterField<Core::ACParameters>>({
    {"Amplitude", &Core::ACParameters::Amplitude, nullptr},
    {"Frequency", &Core::ACParameters::Frequency, "Hz"},
    {"Offset", &Core::ACParameters::Offset, nullptr},
});

constexpr auto PulseFields = std::to_array<ParameterField<Core::PulseParameters>>({
    {"Low", &Core::PulseParameters::Low, nullptr},
    {"High", &Core::PulseParameters::High, nullptr},
    {"Delay", &Core::PulseParameters::Delay, "s"},
    {"Rise", &Core::PulseParameters::RiseTime, "s"},
    {"Fall", &Core::PulseParameters::FallTime, "s"},
    {"Width", &Core::PulseParameters::Width, "s"},
    {"Period", &Core::PulseParameters::Period, "s"},
});

// Labeled with the SPICE parameter names, as datasheet models list them
constexpr auto DiodeFields = std::to_array<ParameterField<Core::DiodeParameters>>({
    {"IS", &Core::DiodeParameters::SaturationCurrent, "A"},
    {"N", &Core::DiodeParameters::EmissionCoefficient, ""},
    {"RS", &Core::DiodeParameters::SeriesResistance, "Ohm"},
    {"BV", &Core::DiodeParameters::BreakdownVoltage, "V"},
    {"IBV", &Core::DiodeParameters::BreakdownCurrent, "A"},
    {"CJO", &Core::DiodeParameters::JunctionCapacitance, "F"},
    {"VJ", &Core::DiodeParameters::JunctionPotential, "V"},
    {"M", &Core::DiodeParameters::GradingCoefficient, ""},
    {"TT", &Core::DiodeParameters::TransitTime, "s"},
});

template <typename Parameters, std::size_t Count>
void LoadParameterFields(const std::array<ParameterField<Parameters>, Count> &fields,
                         std::array<ValueField, Count> &value_fields, const Parameters &parameters) {
    for (std::size_t index = 0; index < Count; ++index) {
        value_fields[index].Load(parameters.*fields[index].Value);
    }
}

// Draws one field per member and applies a valid edit to the parameters; each value is checked by trying it in a
// copy of the whole set, since some limits depend on several members. Returns whether a value changed
template <typename Parameters, std::size_t Count>
bool DrawParameterFields(const std::array<ParameterField<Parameters>, Count> &fields,
                         std::array<ValueField, Count> &value_fields, const char *source_unit, const auto &is_valid,
                         Parameters &parameters) {
    bool changed = false;
    for (std::size_t index = 0; index < Count; ++index) {
        const ParameterField<Parameters> &field = fields[index];
        const auto is_valid_value = [&](const double value) {
            Parameters candidate = parameters;
            candidate.*field.Value = value;
            return is_valid(candidate);
        };
        const char *unit = field.Unit != nullptr ? field.Unit : source_unit;
        if (const std::optional<double> value = value_fields[index].Draw(field.Label, unit, is_valid_value)) {
            parameters.*field.Value = *value;
            changed = true;
        }
    }
    return changed;
}

} // namespace

/**
 * @brief   Creates the window for a schematic.
 * @param[in] schematic  Schematic whose selection is shown; it must outlive the window.
 */
PropertiesWindow::PropertiesWindow(Schematic &schematic) : AppWindow("Properties", true), m_Schematic(schematic) {
}

void PropertiesWindow::Draw() {
    if (m_Schematic.GetSelectedWireIndex()) {
        ImGui::TextUnformatted("Wire");
        return;
    }
    UIElement *element = m_Schematic.GetSelectedElement();
    if (element == nullptr) {
        ImGui::TextDisabled("Select a component to edit it");
        return;
    }

    Core::Component &component = element->GetComponent();
    ImGui::TextUnformatted(component.GetTypeName());
    if (!component.GetName().empty()) {
        ImGui::TextUnformatted(std::format("Name: {}", component.GetName()).c_str());
    }
    const bool is_diode = Core::IsDiode(component.GetType());
    if (!component.HasValue() && !is_diode) {
        return;
    }

    if (m_LoadedSelection != m_Schematic.GetSelectionVersion()) {
        LoadFields(component);
    }
    if (is_diode) {
        auto &diode = static_cast<Core::Diode &>(component);
        DrawDiode(diode);
        if (!diode.IsCustom()) {
            return;
        }
    } else if (Core::IsSource(component.GetType())) {
        DrawSource(static_cast<Core::Source &>(component));
    } else {
        DrawValue(component);
    }
    ImGui::TextDisabled("Suffixes: T G M k m u n p f (case sensitive)");
}

void PropertiesWindow::LoadFields(const Core::Component &component) {
    m_Value.Load(component.GetValue());
    if (Core::IsSource(component.GetType())) {
        const auto &source = static_cast<const Core::Source &>(component);
        LoadParameterFields(ACFields, m_ACFields, source.GetAC());
        LoadParameterFields(PulseFields, m_PulseFields, source.GetPulse());
    }
    if (Core::IsDiode(component.GetType())) {
        const auto &diode = static_cast<const Core::Diode &>(component);
        LoadParameterFields(DiodeFields, m_DiodeFields, diode.GetParameters());
    }
    m_LoadedSelection = m_Schematic.GetSelectionVersion();
}

void PropertiesWindow::DrawValue(Core::Component &component) {
    const auto is_valid = [&component](const double value) { return component.IsValidValue(value); };
    if (const std::optional<double> value = m_Value.Draw("Value", component.GetUnit(), is_valid)) {
        component.SetValue(*value);
        m_Schematic.MarkModified();
    }
}

void PropertiesWindow::DrawDiode(Core::Diode &diode) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Model");
    ImGui::SameLine(ImGui::GetFontSize() * LabelWidth);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * InputWidth);
    if (ImGui::BeginCombo("##Model", diode.GetModelName())) {
        for (const Core::DiodeModel &model : Core::GetDiodeModels(diode.GetType())) {
            const bool selected = &model == diode.GetModel();
            if (ImGui::Selectable(model.Name, selected) && !selected) {
                diode.SetModel(model);
                m_Schematic.MarkModified();
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        // Custom starts from the parameters of the model it replaces, so a ready part can be tweaked
        if (ImGui::Selectable(Core::CustomDiodeModelName, diode.IsCustom()) && !diode.IsCustom()) {
            diode.SetCustom();
            LoadParameterFields(DiodeFields, m_DiodeFields, diode.GetParameters());
            m_Schematic.MarkModified();
        }
        ImGui::EndCombo();
    }

    if (!diode.IsCustom()) {
        ImGui::TextDisabled("%s", diode.GetModel()->Description);
        return;
    }
    Core::DiodeParameters parameters = diode.GetParameters();
    const auto is_valid = [&diode](const Core::DiodeParameters &candidate) {
        return diode.IsValidParameters(candidate);
    };
    if (DrawParameterFields(DiodeFields, m_DiodeFields, "", is_valid, parameters)) {
        diode.SetCustomParameters(parameters);
        m_Schematic.MarkModified();
    }
    const char *breakdown_hint = diode.GetType() == Core::ComponentType::ZenerDiode
                                     ? "BV is the voltage the Zener regulates at"
                                     : "BV is the reverse voltage the diode is rated for";
    ImGui::TextDisabled("%s", breakdown_hint);
}

void PropertiesWindow::DrawSource(Core::Source &source) {
    using SourceType = Core::Source::SourceType;
    for (const SourceType type : {SourceType::DC, SourceType::AC, SourceType::Pulse}) {
        if (type != SourceType::DC) {
            ImGui::SameLine();
        }
        if (ImGui::RadioButton(Core::GetSourceTypeName(type), source.GetSourceType() == type) &&
            source.GetSourceType() != type) {
            source.SetSourceType(type);
            m_Schematic.MarkModified();
        }
    }

    switch (source.GetSourceType()) {
    case SourceType::DC: {
        const char *label = source.GetType() == Core::ComponentType::CurrentSource ? "Current" : "Voltage";
        if (const std::optional<double> value = m_Value.Draw(label, source.GetUnit(), AnyValue)) {
            source.SetValue(*value);
            m_Schematic.MarkModified();
        }
        break;
    }
    case SourceType::AC:
        DrawSine(source);
        break;
    case SourceType::Pulse:
        DrawPulse(source);
        break;
    }
}

void PropertiesWindow::DrawSine(Core::Source &source) {
    Core::ACParameters ac = source.GetAC();
    const auto is_valid = [&source](const Core::ACParameters &candidate) { return source.IsValidAC(candidate); };
    if (DrawParameterFields(ACFields, m_ACFields, source.GetUnit(), is_valid, ac)) {
        source.SetAC(ac);
        m_Schematic.MarkModified();
    }
    ImGui::TextDisabled("The amplitude is the peak; it is also the AC sweep magnitude");
}

void PropertiesWindow::DrawPulse(Core::Source &source) {
    Core::PulseParameters pulse = source.GetPulse();
    const auto is_valid = [&source](const Core::PulseParameters &candidate) { return source.IsValidPulse(candidate); };
    if (DrawParameterFields(PulseFields, m_PulseFields, source.GetUnit(), is_valid, pulse)) {
        source.SetPulse(pulse);
        m_Schematic.MarkModified();
    }

    ImGui::TextDisabled("%s", std::format("Repeats at {}Hz", Core::FormatValue(1.0 / pulse.Period)).c_str());
    if (pulse.RiseTime + pulse.Width + pulse.FallTime > pulse.Period) {
        ImGui::TextColored(GetWarningTextColor(), "Rise, width and fall add up to more than the period");
    }
}

} // namespace GUI
