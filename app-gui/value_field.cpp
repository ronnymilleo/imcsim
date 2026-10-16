/**
 * @file    value_field.cpp
 * @brief   Text input for a value typed with SPICE suffixes, such as "4.7k" or "10m".
 */

#include "value_field.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "spice_value.h"
#include "theme.h"
#include <utility>

namespace GUI {

namespace {

// In font sizes, so the layout follows the DPI scale
constexpr float LabelWidth = 6.0f;
constexpr float InputWidth = 8.0f;

// +1 for one standard value up and -1 for one down, from the arrow keys while the input is active and from the mouse
// wheel over it; 0 when the user did neither this frame. Call right after the input
int ReadStepFromInput() {
    if (ImGui::IsItemActive()) {
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
            return 1;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
            return -1;
        }
    }
    const float wheel = ImGui::GetIO().MouseWheel;
    if (ImGui::IsItemHovered() && wheel != 0.0f) {
        return wheel > 0.0f ? 1 : -1;
    }
    return 0;
}

// The two buttons that step through the series: +1 for the up one and -1 for the down one, 0 when none was clicked
int DrawStepButtons() {
    int step = 0;
    ImGui::SameLine();
    if (ImGui::ArrowButton("##previous", ImGuiDir_Down)) {
        step = -1;
    }
    ImGui::SetItemTooltip("Previous standard value");
    ImGui::SameLine();
    if (ImGui::ArrowButton("##next", ImGuiDir_Up)) {
        step = 1;
    }
    ImGui::SetItemTooltip("Next standard value");
    return step;
}

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
 * @brief   Focuses the input when the user started typing elsewhere or a step rewrote its text.
 * @return  The character typed before the field was shown, which the input starts with.
 */
std::optional<char> ValueField::FocusInput() {
    // Focusing from code selects the whole text, so once the input is active the cursor moves after the character.
    // The character counts as an edit, so a value typed as a single digit applies too
    const std::optional<char> first = std::exchange(m_TypingStart, std::nullopt);
    if (first) {
        m_Text.fill('\0');
        m_Text[0] = *first;
        m_CursorToEnd = true;
        ImGui::SetKeyboardFocusHere();
    } else if (std::exchange(m_RefocusInput, false)) {
        // A step rewrote the text, which an active input only takes up when it is focused again
        ImGui::SetKeyboardFocusHere();
    }
    return first;
}

/**
 * @brief   Draws the text input, with the cursor after a character typed from outside.
 * @return  Whether the user changed the text this frame.
 */
bool ValueField::DrawInput() {
    const auto place_cursor = [](ImGuiInputTextCallbackData *data) {
        auto &cursor_to_end = *static_cast<bool *>(data->UserData);
        if (std::exchange(cursor_to_end, false)) {
            data->CursorPos = data->BufTextLen;
            data->SelectionStart = data->BufTextLen;
            data->SelectionEnd = data->BufTextLen;
        }
        return 0;
    };
    return ImGui::InputText("##value", m_Text.data(), m_Text.size(), ImGuiInputTextFlags_CallbackAlways, place_cursor,
                            &m_CursorToEnd);
}

/**
 * @brief   Moves the value a number of steps through a series and loads it as the new text.
 * @param[in] unit      Unit the text may carry.
 * @param[in] is_valid  Tells whether a value is acceptable; the value does not move when the next one is not.
 * @param[in] series    Standard series to step through.
 * @param[in] step      Number of steps, negative for down.
 * @return  The new value, or no value when the text cannot be read or the stepped value is not valid.
 */
std::optional<double> ValueField::ApplyStep(const std::string &unit, const std::function<bool(double)> &is_valid,
                                            const Core::ValueSeries series, const int step) {
    const std::optional<double> current = Core::ParseValue(m_Text.data(), unit);
    if (!current) {
        return std::nullopt;
    }
    const double stepped = Core::StepStandardValue(series, *current, step);
    if (!is_valid(stepped)) {
        return std::nullopt;
    }
    // While active, the input keeps its own text over the one loaded here, so it lets go and takes the focus again
    // next frame
    ImGui::ClearActiveID();
    Load(stepped);
    m_RefocusInput = true;
    return stepped;
}

/**
 * @brief   Draws the label, the text input and the unit on one line, and an error below when the text is invalid.
 * @param[in] label     Text before the input; it also gives the input its ImGui ID, so it must be unique in the
 *                      window.
 * @param[in] unit      Unit shown after the input, which the user may also type after the suffix.
 * @param[in] is_valid  Tells whether a parsed value is acceptable, such as a positive frequency.
 * @param[in] series    Standard series the value must be in; with one, a typed value becomes the nearest value of
 *                      the series, and two buttons, the arrow keys and the mouse wheel step through the series.
 * @return  The new value when the text was changed this frame and is valid, so the caller can apply it.
 */
std::optional<double> ValueField::Draw(const char *label, const std::string &unit,
                                       const std::function<bool(double)> &is_valid, const Core::ValueSeries series) {
    DrawFieldLabel(label);
    ImGui::PushID(label);
    std::optional<double> edited;
    const std::optional<char> first = FocusInput();
    const bool changed = DrawInput();
    const bool steps = series != Core::ValueSeries::Free;
    int step = 0;
    if (steps) {
        // The wheel over the input steps the value, so the window behind it must not scroll too
        ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
        step = ReadStepFromInput();
    }
    // What the user typed, moved to the series
    const auto typed_value = [&] {
        const std::optional<double> value = Core::ParseValue(m_Text.data(), unit);
        return value ? std::optional(Core::NearestStandardValue(series, *value)) : std::nullopt;
    };
    if (changed || first) {
        const std::optional<double> value = typed_value();
        m_Invalid = !value || !is_valid(*value);
        if (!m_Invalid) {
            edited = value;
        }
    }
    if (ImGui::IsItemDeactivatedAfterEdit() && !m_Invalid) {
        // Rewrite the text in canonical form, so "4700" becomes "4.7k"
        if (const std::optional<double> value = typed_value()) {
            Load(*value);
        }
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(unit.c_str());
    if (steps) {
        if (const int clicked = DrawStepButtons(); clicked != 0) {
            step = clicked;
        }
    }
    ImGui::PopID();
    if (step != 0) {
        if (const std::optional<double> stepped = ApplyStep(unit, is_valid, series, step)) {
            edited = stepped;
        }
    }
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
