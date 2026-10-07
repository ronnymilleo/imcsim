/**
 * @file    value_field.cpp
 * @brief   Text input for a value typed with SPICE suffixes, such as "4.7k" or "10m".
 */

#include "value_field.h"

#include "imgui.h"
#include "spice_value.h"

namespace GUI {

namespace {

constexpr ImVec4 ErrorTextColor = {1.0f, 0.4f, 0.4f, 1.0f};
// In font sizes, so the layout follows the DPI scale
constexpr float LabelWidth = 6.0f;
constexpr float InputWidth = 8.0f;

} // namespace

/**
 * @brief   Replaces the text with a value in canonical form, such as "4.7k".
 * @param[in] value  Value to show; call it whenever the field starts editing another value.
 */
void ValueField::Load(const double value) {
    const std::string text = Core::FormatValue(value);
    m_Text.fill('\0');
    text.copy(m_Text.data(), m_Text.size() - 1);
    m_Invalid = false;
}

/**
 * @brief   Draws the label, the text input and the unit on one line, and an error below when the text is invalid.
 * @param[in] label     Text before the input; it also gives the input its ImGui ID, so it must be unique in the
 *                      window.
 * @param[in] unit      Unit shown after the input, which the user may also type after the suffix.
 * @param[in] is_valid  Tells whether a parsed value is acceptable, such as a positive frequency.
 * @return  The new value when the text was changed this frame and is valid, so the caller can apply it.
 */
std::optional<double> ValueField::Draw(const char *label, const std::string &unit,
                                       const std::function<bool(double)> &is_valid) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(ImGui::GetFontSize() * LabelWidth);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * InputWidth);
    ImGui::PushID(label);
    std::optional<double> edited;
    if (ImGui::InputText("##value", m_Text.data(), m_Text.size())) {
        const std::optional<double> value = Core::ParseValue(m_Text.data(), unit);
        m_Invalid = !value || !is_valid(*value);
        if (!m_Invalid) {
            edited = value;
        }
    }
    if (ImGui::IsItemDeactivatedAfterEdit() && !m_Invalid) {
        // Rewrite the text in canonical form, so "4700" becomes "4.7k"
        if (const std::optional<double> value = Core::ParseValue(m_Text.data(), unit)) {
            Load(*value);
        }
    }
    ImGui::PopID();
    ImGui::SameLine();
    ImGui::TextUnformatted(unit.c_str());
    if (m_Invalid) {
        ImGui::TextColored(ErrorTextColor, "Invalid value");
    }
    return edited;
}

} // namespace GUI
