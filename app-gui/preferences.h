/**
 * @file    preferences.h
 * @brief   The user's own choices, apart from any schematic, and the file that keeps them.
 */

#ifndef IMCSIM_PREFERENCES_H
#define IMCSIM_PREFERENCES_H

#include "standard_values.h"
#include "theme.h"
#include "ui_elements/ui_element.h"
#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

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
    COUNT
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

constexpr std::size_t NUMBER_OF_LIVE_EFFECTS = static_cast<std::size_t>(LiveEffect::COUNT);

/**
 * @struct  Preferences
 * @brief   How the program looks and behaves for this user, whatever schematic is open.
 * @details What belongs to a circuit, such as the simulation settings, is saved in its file instead. Every field
 *          starts at the choice a new user gets.
 */
struct Preferences {
    // Appearance
    // By name, so the choice survives themes being added or reordered
    std::string Theme = GetThemes().front().Name;
    SymbolStyle Symbols = SymbolStyle::IEC;
    // Numbers each terminal; terminal 1 is where a positive current enters
    bool ShowTerminalNumbers = false;

    // Schematic while showing results
    WireColoring WireColors = WireColoring::PLAIN;
    // By enum value, each on its own; all off until asked for
    std::array<bool, NUMBER_OF_LIVE_EFFECTS> LiveEffects = {};

    // Parts: resistors, capacitors and inductors take the nearest value of the series
    Core::ValueSeries ValueSeries = Core::ValueSeries::FREE;

    // Results: a new result brings the Output window to the front, or only marks it
    bool BringOutputToFront = true;
    // The selected analysis runs again by itself once an edit of the circuit settles, if it was already run
    bool RerunOnEdit = false;

    bool IsEffectOn(LiveEffect effect) const;
    void SetEffectOn(LiveEffect effect, bool on);

    bool operator==(const Preferences &) const = default;
};

/**
 * @struct  LoadedPreferences
 * @brief   Preferences read from their file, plus a message for each choice that could not be read.
 */
struct LoadedPreferences {
    Preferences Choices;
    std::vector<std::string> Warnings;
};

// Names as menus and the file show them
const char *GetWireColoringName(WireColoring coloring);
const char *GetLiveEffectName(LiveEffect effect);

// File
inline constexpr std::string_view PREFERENCES_FILE_NAME = "preferences.json";
std::string SavePreferences(const Preferences &preferences);
LoadedPreferences LoadPreferences(std::string_view text);

} // namespace GUI

#endif // IMCSIM_PREFERENCES_H
