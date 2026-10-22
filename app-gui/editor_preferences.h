/**
 * @file    editor_preferences.h
 * @brief   How the schematic editor draws the circuit, picked in the View menu and kept in imgui.ini.
 */

#ifndef IMCSIM_EDITOR_PREFERENCES_H
#define IMCSIM_EDITOR_PREFERENCES_H

#include "imgui.h"
#include "part_editor.h"
#include "ui_elements/ui_element.h"
#include <array>
#include <cstddef>
#include <string_view>

namespace GUI {

/**
 * @enum    WireColoring
 * @brief   What the colors of wires (and, by current and power, of parts) show.
 * @details Voltage, Current and Power color by the state on display, on a heat scale with a legend.
 */
enum class WireColoring {
    PLAIN,
    NODES,
    VOLTAGE,
    CURRENT,
    POWER,
};

/**
 * @enum    LiveEffect
 * @brief   An animated effect that reads the state on display and draws on top of the schematic.
 * @details Each effect is turned on by itself, over any wire coloring.
 */
enum class LiveEffect {
    CURRENT_FLOW,
    LED_GLOW,
    HEAT_GLOW,
    COUNT
};

/**
 * @class   EditorPreferences
 * @brief   The View preferences of the editor: wire colors, live effects, symbol style, terminal numbers and the
 *          series of standard values.
 * @details The series of standard values belongs to the part editor, which applies it; this class only shows and
 *          remembers it with the others.
 */
class EditorPreferences {
public:
    explicit EditorPreferences(PartEditor &part_editor);

    // View menu
    void DrawMenuItems();

    // imgui.ini
    void RegisterSettingsHandler();

    // Choices
    WireColoring GetWireColoring() const;
    bool IsEffectOn(LiveEffect effect) const;
    SymbolStyle GetSymbolStyle() const;
    bool ShowsTerminalNumbers() const;

private:
    static constexpr std::size_t NUMBER_OF_EFFECTS = static_cast<std::size_t>(LiveEffect::COUNT);

    PartEditor &m_PartEditor;
    WireColoring m_WireColoring = WireColoring::PLAIN;
    // Live effects by enum value, each on its own; all off until asked for
    std::array<bool, NUMBER_OF_EFFECTS> m_LiveEffects = {};
    SymbolStyle m_SymbolStyle = SymbolStyle::IEC;
    bool m_ShowTerminalNumbers = false;

    bool DrawValueSeriesItems();
    void ReadSetting(std::string_view line);
    void WriteSettings(ImGuiTextBuffer &buffer) const;
};

} // namespace GUI

#endif // IMCSIM_EDITOR_PREFERENCES_H
