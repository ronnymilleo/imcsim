/**
 * @file    editor_preferences.cpp
 * @brief   How the schematic editor draws the circuit, picked in the View menu and kept in imgui.ini.
 */

#include "editor_preferences.h"

#include "imgui_internal.h"
#include "standard_values.h"
#include <algorithm>
#include <format>
#include <optional>
#include <string>

namespace GUI {

namespace {

// Null-terminated literals, so data() can go to ImGui
constexpr auto WIRE_COLORING_NAMES = std::to_array<std::string_view>({"Plain", "Nodes", "Voltage", "Current", "Power"});
constexpr auto LIVE_EFFECTS_NAMES = std::to_array<std::string_view>({"Current Flow", "LED Glow", "Heat Glow"});
// Section of imgui.ini that keeps the View preferences
constexpr const char *SETTINGS_TYPE_NAME = "Editor";

// The value of a "key=value" line when it has that key
std::optional<std::string_view> ValueOf(const std::string_view line, const std::string_view key) {
    if (!line.starts_with(key) || line.size() <= key.size() || line[key.size()] != '=') {
        return std::nullopt;
    }
    return line.substr(key.size() + 1);
}

} // namespace

/**
 * @brief   Creates the preferences with their defaults: plain wires, no live effects, IEC symbols and no terminal
 *          numbers, until RegisterSettingsHandler() reads the saved ones.
 * @param[in] part_editor  Keeps the series of standard values; it must outlive the preferences.
 */
EditorPreferences::EditorPreferences(PartEditor &part_editor) : m_PartEditor(part_editor) {
}

/**
 * @brief   Draws the View menu items that set how the schematic looks; they apply right away and are remembered.
 * @note    Call between BeginMenu() and EndMenu().
 */
void EditorPreferences::DrawMenuItems() {
    bool changed = false;
    if (ImGui::BeginMenu("Wire Colors")) {
        for (std::size_t index = 0; index < WIRE_COLORING_NAMES.size(); ++index) {
            const auto coloring = static_cast<WireColoring>(index);
            if (ImGui::MenuItem(WIRE_COLORING_NAMES[index].data(), nullptr, m_WireColoring == coloring)) {
                m_WireColoring = coloring;
                changed = true;
            }
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Live Effects")) {
        for (std::size_t index = 0; index < LIVE_EFFECTS_NAMES.size(); ++index) {
            bool &enabled = m_LiveEffects[index];
            if (ImGui::MenuItem(LIVE_EFFECTS_NAMES[index].data(), nullptr, enabled)) {
                enabled = !enabled;
                changed = true;
            }
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Symbols")) {
        if (ImGui::MenuItem("IEC", nullptr, m_SymbolStyle == SymbolStyle::IEC)) {
            m_SymbolStyle = SymbolStyle::IEC;
            changed = true;
        }
        if (ImGui::MenuItem("ANSI", nullptr, m_SymbolStyle == SymbolStyle::ANSI)) {
            m_SymbolStyle = SymbolStyle::ANSI;
            changed = true;
        }
        ImGui::EndMenu();
    }
    if (ImGui::MenuItem("Terminal Numbers", nullptr, &m_ShowTerminalNumbers)) {
        changed = true;
    }
    ImGui::SetItemTooltip("Number the terminals of each part; terminal 1 is where a positive current enters");
    if (ImGui::BeginMenu("Component Values")) {
        changed |= DrawValueSeriesItems();
        ImGui::EndMenu();
    }
    if (changed) {
        ImGui::MarkIniSettingsDirty();
    }
}

/**
 * @brief   Keeps the View preferences (wire colors, live effects, symbol style, terminal numbers, standard values) in
 *          imgui.ini, next to the window layout, so they survive a restart.
 * @note    Call once after the ImGui context exists and before the first frame.
 */
void EditorPreferences::RegisterSettingsHandler() {
    ImGuiSettingsHandler handler;
    handler.TypeName = SETTINGS_TYPE_NAME;
    handler.TypeHash = ImHashStr(SETTINGS_TYPE_NAME);
    handler.UserData = this;
    handler.ReadOpenFn = [](ImGuiContext *, ImGuiSettingsHandler *settings, const char *) -> void * {
        return settings->UserData;
    };
    handler.ReadLineFn = [](ImGuiContext *, ImGuiSettingsHandler *, void *entry, const char *line) {
        static_cast<EditorPreferences *>(entry)->ReadSetting(line);
    };
    handler.WriteAllFn = [](ImGuiContext *, ImGuiSettingsHandler *settings, ImGuiTextBuffer *buffer) {
        static_cast<const EditorPreferences *>(settings->UserData)->WriteSettings(*buffer);
    };
    ImGui::AddSettingsHandler(&handler);
}

/**
 * @brief   Tells what the colors of wires and parts show.
 * @return  The wire coloring picked in the View menu.
 */
WireColoring EditorPreferences::GetWireColoring() const {
    return m_WireColoring;
}

/**
 * @brief   Tells whether a live effect is turned on.
 * @param[in] effect  Any effect but Count.
 * @return  True when the user turned it on in the View menu.
 */
bool EditorPreferences::IsEffectOn(const LiveEffect effect) const {
    return m_LiveEffects[static_cast<std::size_t>(effect)];
}

/**
 * @brief   Tells how the symbols are drawn.
 * @return  IEC or ANSI, as picked in the View menu.
 */
SymbolStyle EditorPreferences::GetSymbolStyle() const {
    return m_SymbolStyle;
}

/**
 * @brief   Tells whether the terminals of each part are numbered.
 * @return  True when Terminal Numbers is checked in the View menu.
 */
bool EditorPreferences::ShowsTerminalNumbers() const {
    return m_ShowTerminalNumbers;
}

// The choice of standard values for resistors, capacitors and inductors; true when it changed
bool EditorPreferences::DrawValueSeriesItems() {
    bool changed = false;
    constexpr auto CHOICES =
        std::to_array({Core::ValueSeries::FREE, Core::ValueSeries::E6, Core::ValueSeries::E12, Core::ValueSeries::E24,
                       Core::ValueSeries::E48, Core::ValueSeries::E96, Core::ValueSeries::E192});
    for (const Core::ValueSeries series : CHOICES) {
        const std::string label =
            series == Core::ValueSeries::FREE
                ? "Any value"
                : std::format("{} ({:g}%)", Core::GetSeriesName(series), Core::GetSeriesTolerancePercent(series));
        if (ImGui::MenuItem(label.c_str(), nullptr, m_PartEditor.GetValueSeries() == series)) {
            m_PartEditor.SetValueSeries(series);
            changed = true;
        }
    }
    ImGui::Separator();
    ImGui::TextDisabled("Resistors, capacitors and inductors take the nearest value of the series");
    return changed;
}

// One "Key=value" line of the preferences; keys and values that are not known are ignored
void EditorPreferences::ReadSetting(const std::string_view line) {
    if (const auto colors = ValueOf(line, "WireColors")) {
        const auto found = std::ranges::find(WIRE_COLORING_NAMES, *colors);
        if (found != WIRE_COLORING_NAMES.end()) {
            m_WireColoring = static_cast<WireColoring>(found - WIRE_COLORING_NAMES.begin());
        }
    } else if (const auto symbols = ValueOf(line, "Symbols")) {
        m_SymbolStyle = *symbols == "ANSI" ? SymbolStyle::ANSI : SymbolStyle::IEC;
    } else if (const auto numbers = ValueOf(line, "TerminalNumbers")) {
        m_ShowTerminalNumbers = *numbers == "1";
    } else if (const auto series = ValueOf(line, "ValueSeries")) {
        if (const auto parsed = Core::ParseSeriesName(*series)) {
            m_PartEditor.SetValueSeries(*parsed);
        }
    } else if (const auto effects = ValueOf(line, "LiveEffects")) {
        for (std::size_t index = 0; index < LIVE_EFFECTS_NAMES.size(); ++index) {
            m_LiveEffects[index] = effects->find(LIVE_EFFECTS_NAMES[index]) != std::string_view::npos;
        }
    }
}

void EditorPreferences::WriteSettings(ImGuiTextBuffer &buffer) const {
    buffer.appendf("[%s][Preferences]\n", SETTINGS_TYPE_NAME);
    buffer.appendf("WireColors=%s\n", WIRE_COLORING_NAMES[static_cast<std::size_t>(m_WireColoring)].data());
    buffer.appendf("Symbols=%s\n", m_SymbolStyle == SymbolStyle::ANSI ? "ANSI" : "IEC");
    buffer.appendf("TerminalNumbers=%d\n", m_ShowTerminalNumbers ? 1 : 0);
    buffer.appendf("ValueSeries=%s\n", Core::GetSeriesName(m_PartEditor.GetValueSeries()));
    std::string live_effects;
    for (std::size_t index = 0; index < LIVE_EFFECTS_NAMES.size(); ++index) {
        if (!m_LiveEffects[index]) {
            continue;
        }
        if (!live_effects.empty()) {
            live_effects += ',';
        }
        live_effects += LIVE_EFFECTS_NAMES[index];
    }
    buffer.appendf("LiveEffects=%s\n\n", live_effects.c_str());
}

} // namespace GUI
