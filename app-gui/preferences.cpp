/**
 * @file    preferences.cpp
 * @brief   The user's own choices, apart from any schematic, and the file that keeps them.
 */

#include "preferences.h"

#include "nlohmann/json.hpp"
#include <algorithm>
#include <format>
#include <optional>

namespace GUI {

namespace {

// Null-terminated literals, so they can go to ImGui as they are
constexpr auto WIRE_COLORING_NAMES = std::to_array<const char *>({"Plain", "Nodes", "Voltage", "Current", "Power"});
constexpr auto LIVE_EFFECT_NAMES = std::to_array<const char *>({"Current Flow", "LED Glow", "Heat Glow"});
static_assert(WIRE_COLORING_NAMES.size() == static_cast<std::size_t>(WireColoring::COUNT));
static_assert(LIVE_EFFECT_NAMES.size() == NUMBER_OF_LIVE_EFFECTS);

constexpr int FORMAT_VERSION = 1;

// The position of a name in a list of names, or no value when it is not there
template <std::size_t Count>
std::optional<std::size_t> FindName(const std::array<const char *, Count> &names, const std::string_view name) {
    const auto found = std::ranges::find(names, name);
    if (found == names.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(found - names.begin());
}

/**
 * @class   ChoiceReader
 * @brief   Reads the choices of the file one key at a time, keeping the default of any that is missing or not
 *          understood, with a warning for the latter.
 */
class ChoiceReader {
public:
    ChoiceReader(const nlohmann::json &document, std::vector<std::string> &warnings)
        : m_Document(document), m_Warnings(warnings) {}

    // Reads a key of the given JSON type and hands its value to apply, which tells whether it understood it
    template <typename Value, typename Apply>
    void Read(const char *key, const nlohmann::json::value_t type, const Apply &apply) {
        const auto entry = m_Document.find(key);
        if (entry == m_Document.end()) {
            return;
        }
        if (entry->type() != type || !apply(entry->template get<Value>())) {
            Warn(key);
        }
    }

    // A number may be written whole or not, which JSON tells apart as different types
    template <typename Apply> void ReadNumber(const char *key, const Apply &apply) {
        const auto entry = m_Document.find(key);
        if (entry == m_Document.end()) {
            return;
        }
        if (!entry->is_number() || !apply(entry->get<double>())) {
            Warn(key);
        }
    }

private:
    const nlohmann::json &m_Document;
    std::vector<std::string> &m_Warnings;

    void Warn(const char *key) {
        m_Warnings.push_back(std::format("The preference {} was not understood and keeps its default", key));
    }
};

} // namespace

/**
 * @brief   Tells whether a live effect is turned on.
 * @param[in] effect  Any effect but COUNT.
 * @return  True when the user turned it on.
 */
bool Preferences::IsEffectOn(const LiveEffect effect) const {
    return LiveEffects[static_cast<std::size_t>(effect)];
}

/**
 * @brief   Turns a live effect on or off.
 * @param[in] effect  Any effect but COUNT.
 * @param[in] on      Whether it is drawn.
 */
void Preferences::SetEffectOn(const LiveEffect effect, const bool on) {
    LiveEffects[static_cast<std::size_t>(effect)] = on;
}

/**
 * @brief   Names a wire coloring.
 * @param[in] coloring  Any coloring but COUNT.
 * @return  Such as "Voltage".
 */
const char *GetWireColoringName(const WireColoring coloring) {
    return WIRE_COLORING_NAMES[static_cast<std::size_t>(coloring)];
}

/**
 * @brief   Names a live effect.
 * @param[in] effect  Any effect but COUNT.
 * @return  Such as "LED Glow".
 */
const char *GetLiveEffectName(const LiveEffect effect) {
    return LIVE_EFFECT_NAMES[static_cast<std::size_t>(effect)];
}

/**
 * @brief   Writes the preferences as JSON.
 * @param[in] preferences  Choices to keep.
 * @return  The file contents, with each choice by name, so the file reads and edits by hand.
 */
std::string SavePreferences(const Preferences &preferences) {
    nlohmann::json live_effects = nlohmann::json::array();
    for (std::size_t index = 0; index < NUMBER_OF_LIVE_EFFECTS; ++index) {
        if (preferences.LiveEffects[index]) {
            live_effects.push_back(LIVE_EFFECT_NAMES[index]);
        }
    }
    const nlohmann::json document = {
        {"version", FORMAT_VERSION},
        {"theme", preferences.Theme},
        {"symbols", preferences.Symbols == SymbolStyle::ANSI ? "ANSI" : "IEC"},
        {"terminal_numbers", preferences.ShowTerminalNumbers},
        {"wire_colors", GetWireColoringName(preferences.WireColors)},
        {"live_effects", live_effects},
        {"value_series", Core::GetSeriesName(preferences.ValueSeries)},
        {"bring_output_to_front", preferences.BringOutputToFront},
        {"rerun_on_edit", preferences.RerunOnEdit},
        {"rerun_delay_seconds", preferences.RerunDelaySeconds},
    };
    return document.dump(2) + "\n";
}

/**
 * @brief   Reads preferences written by SavePreferences().
 * @param[in] text  Contents of the file.
 * @return  The choices, each at its default when it is missing or not understood, and a warning for each one that
 *          was not understood; text that is not a JSON object gives every default and one warning.
 */
LoadedPreferences LoadPreferences(const std::string_view text) {
    LoadedPreferences loaded;
    const nlohmann::json document = nlohmann::json::parse(text, nullptr, false);
    if (!document.is_object()) {
        loaded.Warnings.emplace_back("The preferences file is not a JSON object; every preference keeps its default");
        return loaded;
    }
    Preferences &choices = loaded.Choices;
    ChoiceReader reader(document, loaded.Warnings);
    using Type = nlohmann::json::value_t;
    reader.Read<std::string>("theme", Type::string, [&choices](const std::string &name) {
        if (!FindTheme(name)) {
            return false;
        }
        choices.Theme = name;
        return true;
    });
    reader.Read<std::string>("symbols", Type::string, [&choices](const std::string &name) {
        if (name != "IEC" && name != "ANSI") {
            return false;
        }
        choices.Symbols = name == "ANSI" ? SymbolStyle::ANSI : SymbolStyle::IEC;
        return true;
    });
    reader.Read<bool>("terminal_numbers", Type::boolean, [&choices](const bool on) {
        choices.ShowTerminalNumbers = on;
        return true;
    });
    reader.Read<std::string>("wire_colors", Type::string, [&choices](const std::string &name) {
        const std::optional<std::size_t> index = FindName(WIRE_COLORING_NAMES, name);
        if (!index) {
            return false;
        }
        choices.WireColors = static_cast<WireColoring>(*index);
        return true;
    });
    reader.Read<std::vector<nlohmann::json>>("live_effects", Type::array, [&choices](const auto &names) {
        std::array<bool, NUMBER_OF_LIVE_EFFECTS> effects = {};
        for (const nlohmann::json &name : names) {
            const std::optional<std::size_t> index =
                name.is_string() ? FindName(LIVE_EFFECT_NAMES, name.get<std::string>()) : std::nullopt;
            if (!index) {
                return false;
            }
            effects[*index] = true;
        }
        choices.LiveEffects = effects;
        return true;
    });
    reader.Read<std::string>("value_series", Type::string, [&choices](const std::string &name) {
        const std::optional<Core::ValueSeries> series = Core::ParseSeriesName(name);
        if (!series) {
            return false;
        }
        choices.ValueSeries = *series;
        return true;
    });
    reader.Read<bool>("bring_output_to_front", Type::boolean, [&choices](const bool on) {
        choices.BringOutputToFront = on;
        return true;
    });
    reader.Read<bool>("rerun_on_edit", Type::boolean, [&choices](const bool on) {
        choices.RerunOnEdit = on;
        return true;
    });
    // Only a delay within the range the window offers, so a hand-edited file cannot make it run on every frame
    reader.ReadNumber("rerun_delay_seconds", [&choices](const double seconds) {
        if (seconds < MIN_RERUN_DELAY_SECONDS || seconds > MAX_RERUN_DELAY_SECONDS) {
            return false;
        }
        choices.RerunDelaySeconds = seconds;
        return true;
    });
    return loaded;
}

} // namespace GUI
