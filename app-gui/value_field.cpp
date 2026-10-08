/**
 * @file    value_field.cpp
 * @brief   Text input for a value typed with SPICE suffixes, such as "4.7k" or "10m".
 */

#include "value_field.h"

#include "imgui.h"
#include "spice_value.h"
#include "theme.h"
#include <utility>

namespace GUI {

namespace {

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
 * @brief   Makes the next Draw() focus the input, emptied, and type a character into it, as if the user had typed
 *          it there.
 * @param[in] first  Character typed before the field was shown, such as the first digit of a value.
 */
void ValueField::StartTyping(const char first) {
    m_TypingStart = first;
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
    DrawFieldLabel(label);
    ImGui::PushID(label);
    std::optional<double> edited;
    // Focusing from code selects the whole text, so once the input is active the cursor moves after the character.
    // The character counts as an edit, so a value typed as a single digit applies too
    const std::optional<char> first = std::exchange(m_TypingStart, std::nullopt);
    if (first) {
        m_Text.fill('\0');
        m_Text[0] = *first;
        m_CursorToEnd = true;
        ImGui::SetKeyboardFocusHere();
    }
    const auto place_cursor = [](ImGuiInputTextCallbackData *data) {
        auto &cursor_to_end = *static_cast<bool *>(data->UserData);
        if (std::exchange(cursor_to_end, false)) {
            data->CursorPos = data->BufTextLen;
            data->SelectionStart = data->BufTextLen;
            data->SelectionEnd = data->BufTextLen;
        }
        return 0;
    };
    const bool changed = ImGui::InputText("##value", m_Text.data(), m_Text.size(), ImGuiInputTextFlags_CallbackAlways,
                                          place_cursor, &m_CursorToEnd);
    if (changed || first) {
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
        ImGui::TextColored(GetErrorTextColor(), "Invalid value");
    }
    return edited;
}

/**
 * @brief   Draws a label and sizes the next widget like the input of a ValueField, so other widgets line up with it.
 * @param[in] label  Text before the widget.
 */
void DrawFieldLabel(const char *label) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(ImGui::GetFontSize() * LabelWidth);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * InputWidth);
}

} // namespace GUI
