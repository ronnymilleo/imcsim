/**
 * @file    application.h
 * @brief   Top-level application: owns the window, the Vulkan context, Dear ImGui and the main loop.
 */

#ifndef IMCSIM_APPLICATION_H
#define IMCSIM_APPLICATION_H

#include "analysis_controls.h"
#include "imgui_impl_vulkan.h"
#include "part_editor.h"
#include "schematic.h"
#include "vulkan_context.h"
#include "windows/editor_window.h"
#include "windows/netlist_window.h"
#include "windows/output_window.h"
#include "windows/properties_window.h"
#include "windows/simulation_window.h"

#include <SDL3/SDL_video.h>
#include <string>
#include <vector>

namespace GUI {

/**
 * @class   Application
 * @brief   Owns the SDL window, the Vulkan context and Dear ImGui, and runs the main loop.
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
    VulkanContext m_Vulkan;
    ImGui_ImplVulkan_InitInfo m_VulkanInitInfo{};

    // Application state
    std::vector<std::string> m_Errors;
    bool m_IsOpen{true};
    bool m_ResetLayout{false};
    // imgui.ini in the user's data folder; ImGui keeps only the pointer, so the path lives here
    std::string m_SettingsPath;
    // Last title given to the window, so it is only set again when the file or its state changes
    std::string m_WindowTitle;
    // Result versions already shown, so each new result opens the Output window once
    std::size_t m_ShownOperatingPointVersion = 0;
    std::size_t m_ShownTransientVersion = 0;
    std::size_t m_ShownACSweepVersion = 0;
    std::size_t m_ShownDCSweepVersion = 0;

    // The schematic is declared first, so it exists when the windows that refer to it are built
    Schematic m_Schematic;
    AnalysisControls m_AnalysisControls{m_Schematic};
    PartEditor m_PartEditor{m_Schematic};
    EditorWindow m_EditorWindow{m_Schematic, m_AnalysisControls, m_PartEditor};
    PropertiesWindow m_PropertiesWindow{m_PartEditor};
    NetlistWindow m_NetlistWindow{m_Schematic};
    SimulationWindow m_SimulationWindow{m_Schematic, m_AnalysisControls};
    OutputWindow m_OutputWindow{m_Schematic};

    // Errors
    void AddError(const std::string &message);
    void DumpErrors();

    // Initialization
    int InitSDL();
    int InitVulkan();
    int InitImGui();

    // Frame
    void PollEvents();
    void NewFrame();
    void Render();
    void DrawMainMenuBar();
    void UpdateWindowTitle();
    void ShowNewResults();
    void SetupDefaultLayout(ImGuiID dockspace_id);
    void EndFrame();
};

} // namespace GUI

#endif // IMCSIM_APPLICATION_H
