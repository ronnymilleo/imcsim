/**
 * @file    properties_window.cpp
 * @brief   Window that shows and edits the component selected in the schematic editor.
 */

#include "properties_window.h"

#include "spice_value.h"
#include <format>
#include <string>

namespace GUI {

namespace {

constexpr ImVec4 ErrorTextColor = {1.0f, 0.4f, 0.4f, 1.0f};

} // namespace

/**
 * @brief   Creates the window for an editor.
 * @param[in] editor  Editor whose selection is shown; it must outlive the window.
 */
PropertiesWindow::PropertiesWindow(EditorWindow &editor) : AppWindow("Properties", true), m_Editor(editor) {
}

// Values are typed with SPICE suffixes and applied as soon as they are valid: the editor window is drawn before
// this one, so a click on the canvas would change the selection before a deferred edit was applied
void PropertiesWindow::Draw() {
    if (m_Editor.IsWireSelected()) {
        ImGui::TextUnformatted("Wire");
        return;
    }
    UIElement *element = m_Editor.GetSelectedElement();
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

    if (m_ValueTextSelection != m_Editor.GetSelectionVersion()) {
        LoadValueText(component);
    }
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
    if (ImGui::InputText("##value", m_ValueText.data(), m_ValueText.size())) {
        const std::optional<double> value = Core::ParseValue(m_ValueText.data(), component.GetUnit());
        m_ValueTextInvalid = !value || !component.IsValidValue(*value);
        if (!m_ValueTextInvalid) {
            component.SetValue(*value);
            m_Editor.MarkModified();
        }
    }
    if (ImGui::IsItemDeactivatedAfterEdit() && !m_ValueTextInvalid) {
        // Rewrite the text in canonical form, so "4700" becomes "4.7k"
        LoadValueText(component);
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(component.GetUnit());
    if (m_ValueTextInvalid) {
        ImGui::TextColored(ErrorTextColor, "Invalid value");
    }
    ImGui::TextDisabled("Suffixes: T G M k m u n p f (case sensitive)");
}

void PropertiesWindow::LoadValueText(const Core::Component &component) {
    const std::string text = Core::FormatValue(component.GetValue());
    m_ValueText.fill('\0');
    text.copy(m_ValueText.data(), m_ValueText.size() - 1);
    m_ValueTextSelection = m_Editor.GetSelectionVersion();
    m_ValueTextInvalid = false;
}

} // namespace GUI
