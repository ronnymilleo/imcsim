/**
 * @file    application.h
 * @brief   Top-level application: owns the window, the Vulkan context, Dear ImGui and the main loop.
 */

#ifndef IMCSIM_APPLICATION_H
#define IMCSIM_APPLICATION_H

#include "editor.h"
#include "imgui_impl_vulkan.h"
#include "vulkan_context.h"
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
    float m_SDLWindowScale{};
    SDL_WindowFlags m_SDLWindowFlags{};
    SDL_Window *m_SDLWindow = nullptr;
    VulkanContext m_Vulkan;
    ImGui_ImplVulkan_InitInfo m_VulkanInitInfo{};
    std::vector<std::string> m_Errors;
    Editor m_Editor{};

    void AddError(const std::string &message);
    void DumpErrors();

    int InitSDL();
    int InitVulkan();
    int InitImGui();
};

} // namespace GUI

#endif // IMCSIM_APPLICATION_H
