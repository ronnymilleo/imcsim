/**
 * @file    preferences_window.cpp
 * @brief   Window where the user sets their own choices, and the View menu shortcuts to the most used of them.
 */

#include "preferences_window.h"

#include "standard_values.h"
#include "theme.h"
#include <array>
#include <cstddef>
#include <format>
#include <string>

namespace GUI {

namespace {

// In font sizes, before imgui.ini remembers the size the user gave it
constexpr ImVec2 INITIAL_SIZE = {30.0f, 36.0f};
// Width of the column that names each choice, in font sizes
constexpr float LABEL_COLUMN_WIDTH = 12.0f;
constexpr auto VALUE_SERIES =
    std::to_array({Core::ValueSeries::FREE, Core::ValueSeries::E6, Core::ValueSeries::E12, Core::ValueSeries::E24,
                   Core::ValueSeries::E48, Core::ValueSeries::E96, Core::ValueSeries::E192});

std::string GetValueSeriesLabel(const Core::ValueSeries series) {
    if (series == Core::ValueSeries::FREE) {
        return "Any value";
    }
    return std::format("{} ({:g}%)", Core::GetSeriesName(series), Core::GetSeriesTolerancePercent(series));
}

// A section is a heading over a table of two columns, the name of each choice and its widget
bool BeginSection(const char *title) {
    ImGui::SeparatorText(title);
    if (!ImGui::BeginTable(title, 2)) {
        return false;
    }
    ImGui::TableSetupColumn("Choice", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * LABEL_COLUMN_WIDTH);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
    return true;
}

// Starts a row: the name of the choice on the left, then the widget fills the rest of the row
void BeginChoice(const char *label) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1.0f);
}

void DrawThemeItems(Preferences &preferences) {
    for (const ThemePalette &theme : GetThemes()) {
        if (ImGui::MenuItem(theme.Name, nullptr, preferences.Theme == theme.Name)) {
            preferences.Theme = theme.Name;
        }
    }
}

void DrawWireColorItems(Preferences &preferences) {
    for (std::size_t index = 0; index < static_cast<std::size_t>(WireColoring::COUNT); ++index) {
        const auto coloring = static_cast<WireColoring>(index);
        if (ImGui::MenuItem(GetWireColoringName(coloring), nullptr, preferences.WireColors == coloring)) {
            preferences.WireColors = coloring;
        }
    }
}

void DrawLiveEffectItems(Preferences &preferences) {
    for (std::size_t index = 0; index < NUMBER_OF_LIVE_EFFECTS; ++index) {
        const auto effect = static_cast<LiveEffect>(index);
        if (ImGui::MenuItem(GetLiveEffectName(effect), nullptr, preferences.IsEffectOn(effect))) {
            preferences.SetEffectOn(effect, !preferences.IsEffectOn(effect));
        }
    }
}

} // namespace

/**
 * @brief   Creates the window over the preferences it edits.
 * @param[in,out] preferences  Choices to show and change; they must outlive the window.
 */
PreferencesWindow::PreferencesWindow(Preferences &preferences)
    : AppWindow("Preferences", true), m_Preferences(preferences) {
    SetInitialSize(INITIAL_SIZE);
    SetOpen(false);
}

/**
 * @brief   Draws the View menu shortcuts to the preferences that change while looking at results: the wire colors,
 *          the live effects and the theme.
 * @note    Call between BeginMenu() and EndMenu().
 */
void PreferencesWindow::DrawViewMenuItems() {
    if (ImGui::BeginMenu("Wire Colors")) {
        DrawWireColorItems(m_Preferences);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Live Effects")) {
        DrawLiveEffectItems(m_Preferences);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Theme")) {
        DrawThemeItems(m_Preferences);
        ImGui::EndMenu();
    }
}

void PreferencesWindow::Draw() {
    DrawAppearance();
    DrawSchematic();
    DrawParts();
    DrawResults();
    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::Button("Restore Defaults")) {
        m_Preferences = Preferences{};
    }
    ImGui::SetItemTooltip("Set every preference back to what a new user gets");
}

void PreferencesWindow::DrawAppearance() {
    if (!BeginSection("Appearance")) {
        return;
    }
    BeginChoice("Theme");
    if (ImGui::BeginCombo("##theme", m_Preferences.Theme.c_str())) {
        DrawThemeItems(m_Preferences);
        ImGui::EndCombo();
    }
    BeginChoice("Symbols");
    if (ImGui::RadioButton("IEC", m_Preferences.Symbols == SymbolStyle::IEC)) {
        m_Preferences.Symbols = SymbolStyle::IEC;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("ANSI", m_Preferences.Symbols == SymbolStyle::ANSI)) {
        m_Preferences.Symbols = SymbolStyle::ANSI;
    }
    BeginChoice("Terminal numbers");
    ImGui::Checkbox("##terminal_numbers", &m_Preferences.ShowTerminalNumbers);
    ImGui::SetItemTooltip("Number the terminals of each part; terminal 1 is where a positive current enters");
    ImGui::EndTable();
}

void PreferencesWindow::DrawSchematic() {
    if (!BeginSection("Schematic with results")) {
        return;
    }
    BeginChoice("Wire colors");
    if (ImGui::BeginCombo("##wire_colors", GetWireColoringName(m_Preferences.WireColors))) {
        DrawWireColorItems(m_Preferences);
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("What the colors of wires, and of parts by current or power, show");
    BeginChoice("Live effects");
    for (std::size_t index = 0; index < NUMBER_OF_LIVE_EFFECTS; ++index) {
        const auto effect = static_cast<LiveEffect>(index);
        bool is_on = m_Preferences.IsEffectOn(effect);
        if (ImGui::Checkbox(GetLiveEffectName(effect), &is_on)) {
            m_Preferences.SetEffectOn(effect, is_on);
        }
    }
    ImGui::EndTable();
}

void PreferencesWindow::DrawParts() {
    if (!BeginSection("Parts")) {
        return;
    }
    BeginChoice("Standard values");
    if (ImGui::BeginCombo("##value_series", GetValueSeriesLabel(m_Preferences.ValueSeries).c_str())) {
        for (const Core::ValueSeries series : VALUE_SERIES) {
            if (ImGui::Selectable(GetValueSeriesLabel(series).c_str(), m_Preferences.ValueSeries == series)) {
                m_Preferences.ValueSeries = series;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Resistors, capacitors and inductors take the nearest value of the series as it is typed; "
                          "values already set are kept until edited");
    ImGui::EndTable();
}

void PreferencesWindow::DrawResults() {
    if (!BeginSection("Results")) {
        return;
    }
    BeginChoice("Bring Output to front");
    ImGui::Checkbox("##output_to_front", &m_Preferences.BringOutputToFront);
    ImGui::SetItemTooltip("Bring the Output window to the front on a new result; otherwise it is marked with a dot");
    ImGui::EndTable();
}

} // namespace GUI
