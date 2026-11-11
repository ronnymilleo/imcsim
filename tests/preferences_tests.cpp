/**
 * @file    preferences_tests.cpp
 * @brief   Tests for the user's own choices and the file that keeps them.
 */

#include "preferences.h"

#include "drawing/theme.h"
#include <catch2/catch_test_macros.hpp>
#include <string>

TEST_CASE("Preferences load back unchanged from the file they save", "[preferences]") {
    GUI::Preferences preferences;
    preferences.Theme = GUI::GetThemes().back().Name;
    preferences.Symbols = GUI::SymbolStyle::ANSI;
    preferences.ShowTerminalNumbers = true;
    preferences.WireColors = GUI::WireColoring::SENSITIVITY;
    preferences.SetEffectOn(GUI::LiveEffect::LED_GLOW, true);
    preferences.SetEffectOn(GUI::LiveEffect::HEAT_GLOW, true);
    preferences.SetEffectOn(GUI::LiveEffect::LOGIC_LIGHTS, true);
    preferences.ValueSeries = Core::ValueSeries::E24;
    preferences.BringOutputToFront = false;
    preferences.Engine = GUI::SimulationEngine::NODALYS;
    preferences.RerunOnEdit = true;
    preferences.RerunDelaySeconds = 1.5;
    preferences.HarmonicAmplitudes = GUI::HarmonicAmplitudeUnit::RMS;

    const GUI::LoadedPreferences loaded = GUI::LoadPreferences(GUI::SavePreferences(preferences));
    CHECK(loaded.Warnings.empty());
    CHECK(loaded.Choices == preferences);
    CHECK_FALSE(loaded.Choices.IsEffectOn(GUI::LiveEffect::CURRENT_FLOW));
}

TEST_CASE("A new user gets the first theme, plain wires, no effects but the fuse flash and any value",
          "[preferences]") {
    const GUI::Preferences preferences;
    CHECK(preferences.Theme == GUI::GetThemes().front().Name);
    CHECK(preferences.WireColors == GUI::WireColoring::PLAIN);
    CHECK_FALSE(preferences.IsEffectOn(GUI::LiveEffect::CURRENT_FLOW));
    CHECK(preferences.ValueSeries == Core::ValueSeries::FREE);
    CHECK(preferences.BringOutputToFront);
    CHECK_FALSE(preferences.RerunOnEdit);
    CHECK(preferences.RerunDelaySeconds == GUI::DEFAULT_RERUN_DELAY_SECONDS);
    // A file with none of the keys, as one from a later version that renamed them all, keeps every default
    const GUI::LoadedPreferences loaded = GUI::LoadPreferences(R"({"version": 9, "unknown": true})");
    CHECK(loaded.Warnings.empty());
    CHECK(loaded.Choices == preferences);
}

TEST_CASE("A preference that is not understood keeps its default with a warning, and the others still load",
          "[preferences]") {
    const std::string text = R"({"theme": "No such theme", "symbols": "DIN", "terminal_numbers": "yes",)"
                             R"("wire_colors": "Voltage", "live_effects": ["LED Glow", "Sparks"],)"
                             R"("value_series": "E7", "bring_output_to_front": false, "rerun_delay_seconds": 60})";
    const GUI::LoadedPreferences loaded = GUI::LoadPreferences(text);
    CHECK(loaded.Warnings.size() == 6);
    const GUI::Preferences defaults;
    CHECK(loaded.Choices.Theme == defaults.Theme);
    CHECK(loaded.Choices.Symbols == defaults.Symbols);
    CHECK(loaded.Choices.ShowTerminalNumbers == defaults.ShowTerminalNumbers);
    // A list of effects with one unknown name is not taken in part
    CHECK(loaded.Choices.LiveEffects == defaults.LiveEffects);
    CHECK(loaded.Choices.ValueSeries == defaults.ValueSeries);
    // A delay outside the range the window offers
    CHECK(loaded.Choices.RerunDelaySeconds == defaults.RerunDelaySeconds);
    CHECK(loaded.Choices.WireColors == GUI::WireColoring::VOLTAGE);
    CHECK_FALSE(loaded.Choices.BringOutputToFront);
}

TEST_CASE("The delay before running again reads whole numbers too", "[preferences]") {
    const GUI::LoadedPreferences loaded = GUI::LoadPreferences(R"({"rerun_delay_seconds": 2})");
    CHECK(loaded.Warnings.empty());
    CHECK(loaded.Choices.RerunDelaySeconds == 2.0);
}

TEST_CASE("A preferences file that is not a JSON object gives the defaults with one warning", "[preferences]") {
    for (const char *text : {"", "not json", "[1, 2]"}) {
        const GUI::LoadedPreferences loaded = GUI::LoadPreferences(text);
        CHECK(loaded.Warnings.size() == 1);
        CHECK(loaded.Choices == GUI::Preferences{});
    }
}

TEST_CASE("Wire colorings and live effects have the names menus show", "[preferences]") {
    CHECK(std::string(GUI::GetWireColoringName(GUI::WireColoring::NODES)) == "Nodes");
    CHECK(std::string(GUI::GetWireColoringName(GUI::WireColoring::SENSITIVITY)) == "Sensitivity");
    CHECK(std::string(GUI::GetLiveEffectName(GUI::LiveEffect::CURRENT_FLOW)) == "Current Flow");
    CHECK(std::string(GUI::GetHarmonicAmplitudeUnitName(GUI::HarmonicAmplitudeUnit::PEAK)) == "Peak");
    CHECK(std::string(GUI::GetHarmonicAmplitudeUnitName(GUI::HarmonicAmplitudeUnit::RMS)) == "RMS");
}

TEST_CASE("An unknown harmonic amplitude unit keeps Peak with a warning", "[preferences]") {
    const GUI::LoadedPreferences loaded = GUI::LoadPreferences(R"({"harmonic_amplitudes": "Average"})");
    CHECK(loaded.Warnings.size() == 1);
    CHECK(loaded.Choices.HarmonicAmplitudes == GUI::HarmonicAmplitudeUnit::PEAK);
}

TEST_CASE("The simulation engine is kept by the name its backend gives itself, and an unknown one is refused",
          "[preferences]") {
    CHECK(std::string(GUI::GetSimulationEngineName(GUI::SimulationEngine::NGSPICE)) == "ngspice");
    CHECK(std::string(GUI::GetSimulationEngineName(GUI::SimulationEngine::NODALYS)) == "Nodalys");

    const GUI::LoadedPreferences nodalys = GUI::LoadPreferences(R"({"simulation_backend": "Nodalys"})");
    CHECK(nodalys.Warnings.empty());
    CHECK(nodalys.Choices.Engine == GUI::SimulationEngine::NODALYS);

    const GUI::LoadedPreferences unknown = GUI::LoadPreferences(R"({"simulation_backend": "Xyce"})");
    CHECK(unknown.Warnings.size() == 1);
    CHECK(unknown.Choices.Engine == GUI::SimulationEngine::NGSPICE);
}
