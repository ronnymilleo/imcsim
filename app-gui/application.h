/**
 * @file    application.h
 * @brief   Top-level application: owns the window, the GPU device, Dear ImGui and the main loop.
 */

#ifndef IMCSIM_APPLICATION_H
#define IMCSIM_APPLICATION_H

#include "analysis_controls.h"
#include "analysis_settings_panel.h"
#include "auto_rerun.h"
#include "glow_renderer.h"
#include "part_editor.h"
#include "playback.h"
#include "preferences.h"
#include "schematic.h"
#include "windows/editor_window.h"
#include "windows/netlist_window.h"
#include "windows/output_window.h"
#include "windows/preferences_window.h"
#include "windows/properties_window.h"
#include "windows/simulation_window.h"

#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_video.h>
#include <string>
#include <vector>

namespace GUI {

/**
 * @class   Application
 * @brief   Owns the SDL window, the SDL GPU device and Dear ImGui, and runs the main loop.
 * @details Call Init() once, then Run() if it succeeded, and always finish with Shutdown().
 *          Errors collected during the session are printed to stderr by Shutdown().
 */
class Application {
public:
    Application() = default;
    ~Application() = default;

    int Init();
    int Run();
    void Shutdown();

private:
    // Platform and rendering
    float m_SDLWindowScale{};
    SDL_WindowFlags m_SDLWindowFlags{};
    SDL_Window *m_SDLWindow = nullptr;
    SDL_GPUDevice *m_GPUDevice = nullptr;
    // Declared before the editor, which draws with it
    GlowRenderer m_GlowRenderer;

    // Application state
    std::vector<std::string> m_Errors;
    bool m_IsOpen{true};
    bool m_ResetLayout{false};
    // imgui.ini in the user's data folder; ImGui keeps only the pointer, so the path lives here
    std::string m_SettingsPath;
    // The user's choices, in a file of their own beside imgui.ini; empty when the data folder is not known
    std::string m_PreferencesPath;
    // What the file holds, so the preferences are written only when they change
    Preferences m_SavedPreferences;
    // Last title given to the window, so it is only set again when the file or its state changes
    std::string m_WindowTitle;
    // Result versions already shown, so each new result opens the Output window once
    std::size_t m_ShownOperatingPointVersion = 0;
    std::size_t m_ShownTransientVersion = 0;
    std::size_t m_ShownACSweepVersion = 0;
    std::size_t m_ShownDCSweepVersion = 0;
    std::size_t m_ShownNoiseVersion = 0;
    std::size_t m_ShownTransferFunctionVersion = 0;
    std::size_t m_ShownTruthTableVersion = 0;
    // Runs the selected analysis again after edits, when the preference asks for it
    AutoRerun m_AutoRerun;

    // The schematic and the preferences are declared first, so they exist when the windows that refer to them are
    // built
    Schematic m_Schematic;
    Preferences m_Preferences;
    AnalysisControls m_AnalysisControls{m_Schematic};
    AnalysisSettingsPanel m_AnalysisSettings{m_Schematic};
    PartEditor m_PartEditor{m_Schematic, m_Preferences};
    TransientPlayback m_Playback;
    EditorWindow m_EditorWindow{m_Schematic,    m_AnalysisControls, m_AnalysisSettings, m_PartEditor,
                                m_GlowRenderer, m_Playback,         m_Preferences};
    PropertiesWindow m_PropertiesWindow{m_PartEditor};
    NetlistWindow m_NetlistWindow{m_Schematic};
    SimulationWindow m_SimulationWindow{m_Schematic, m_AnalysisControls, m_AnalysisSettings};
    OutputWindow m_OutputWindow{m_Schematic, m_AnalysisControls, m_Playback, m_Preferences};
    PreferencesWindow m_PreferencesWindow{m_Preferences};

    // Errors
    void AddError(const std::string &message);
    void DumpErrors();

    // Initialization
    int InitSDL();
    int InitGPU();
    int InitImGui();

    // Preferences
    void LoadPreferencesFile();
    void ApplyPreferences();

    // Frame
    void PollEvents();
    void NewFrame();
    void Render();
    void DrawMainMenuBar();
    void UpdateWindowTitle();
    void ShowNewResults();
    void RerunAfterEdits();
    void SetupDefaultLayout(ImGuiID dockspace_id);
    void EndFrame();
};

} // namespace GUI

#endif // IMCSIM_APPLICATION_H
