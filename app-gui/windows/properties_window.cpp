/**
 * @file    properties_window.cpp
 * @brief   Window that shows and edits the component selected in the schematic.
 */

#include "properties_window.h"

#include "spice_value.h"
#include <array>
#include <format>
#include <string>
#include <tuple>

namespace GUI {

namespace {

constexpr ImVec4 WarningTextColor = {1.0f, 0.8f, 0.4f, 1.0f};

bool AnyValue(double /*value*/) {
    return true;
}

/**
 * @struct  PulseField
 * @brief   One pulse parameter in the Properties window: its label, its unit and the member it edits.
 */
struct PulseField {
    const char *Label;
    const char *Unit;
    double Core::PulseParameters::*Value;
};

constexpr auto PulseFields = std::to_array<PulseField>({
    {"Low", "V", &Core::PulseParameters::Low},
    {"High", "V", &Core::PulseParameters::High},
    {"Delay", "s", &Core::PulseParameters::Delay},
    {"Rise", "s", &Core::PulseParameters::RiseTime},
    {"Fall", "s", &Core::PulseParameters::FallTime},
    {"Width", "s", &Core::PulseParameters::Width},
    {"Period", "s", &Core::PulseParameters::Period},
});

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
    if (!component.HasValue()) {
        return;
    }

    if (m_LoadedSelection != m_Schematic.GetSelectionVersion()) {
        LoadFields(component);
    }
    if (component.GetType() == Core::ComponentType::VoltageSource) {
        DrawVoltageSource(static_cast<Core::VoltageSource &>(component));
    } else {
        DrawValue(component);
    }
    ImGui::TextDisabled("Suffixes: T G M k m u n p f (case sensitive)");
}

void PropertiesWindow::LoadFields(const Core::Component &component) {
    m_Value.Load(component.GetValue());
    if (component.GetType() == Core::ComponentType::VoltageSource) {
        const auto &source = static_cast<const Core::VoltageSource &>(component);
        m_Amplitude.Load(source.GetAmplitude());
        m_Frequency.Load(source.GetFrequency());
        m_Offset.Load(source.GetOffset());
        static_assert(PulseFields.size() == std::tuple_size_v<decltype(m_PulseFields)>);
        for (std::size_t index = 0; index < PulseFields.size(); ++index) {
            m_PulseFields[index].Load(source.GetPulse().*PulseFields[index].Value);
        }
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

void PropertiesWindow::DrawVoltageSource(Core::VoltageSource &source) {
    using SourceType = Core::VoltageSource::SourceType;
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
    case SourceType::DC:
        if (const std::optional<double> value = m_Value.Draw("Voltage", "V", AnyValue)) {
            source.SetValue(*value);
            m_Schematic.MarkModified();
        }
        break;
    case SourceType::AC:
        DrawSine(source);
        break;
    case SourceType::Pulse:
        DrawPulse(source);
        break;
    }
}

void PropertiesWindow::DrawSine(Core::VoltageSource &source) {
    if (const std::optional<double> amplitude = m_Amplitude.Draw("Amplitude", "V", AnyValue)) {
        source.SetAmplitude(*amplitude);
        m_Schematic.MarkModified();
    }
    const auto is_valid_frequency = [&source](const double frequency) { return source.IsValidFrequency(frequency); };
    if (const std::optional<double> frequency = m_Frequency.Draw("Frequency", "Hz", is_valid_frequency)) {
        source.SetFrequency(*frequency);
        m_Schematic.MarkModified();
    }
    if (const std::optional<double> offset = m_Offset.Draw("Offset", "V", AnyValue)) {
        source.SetOffset(*offset);
        m_Schematic.MarkModified();
    }
    ImGui::TextDisabled("The amplitude is the peak; it is also the AC sweep magnitude");
}

// Each field is checked by trying its value in a copy of the whole pulse
void PropertiesWindow::DrawPulse(Core::VoltageSource &source) {
    for (std::size_t index = 0; index < PulseFields.size(); ++index) {
        const PulseField &field = PulseFields[index];
        const auto is_valid = [&source, &field](const double value) {
            Core::PulseParameters candidate = source.GetPulse();
            candidate.*field.Value = value;
            return source.IsValidPulse(candidate);
        };
        if (const std::optional<double> value = m_PulseFields[index].Draw(field.Label, field.Unit, is_valid)) {
            Core::PulseParameters pulse = source.GetPulse();
            pulse.*field.Value = *value;
            source.SetPulse(pulse);
            m_Schematic.MarkModified();
        }
    }

    const Core::PulseParameters &pulse = source.GetPulse();
    ImGui::TextDisabled("%s", std::format("Repeats at {}Hz", Core::FormatValue(1.0 / pulse.Period)).c_str());
    if (pulse.RiseTime + pulse.Width + pulse.FallTime > pulse.Period) {
        ImGui::TextColored(WarningTextColor, "Rise, width and fall add up to more than the period");
    }
}

} // namespace GUI
