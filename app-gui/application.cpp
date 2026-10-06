/**
 * @file    application.cpp
 * @brief   Top-level application: owns the window, the Vulkan context, Dear ImGui and the main loop.
 */

#include "application.h"

#include "error_codes.h"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include <SDL3/SDL.h>
#include <cstdio>

namespace GUI {

/**
 * @brief   Initializes SDL, Vulkan and Dear ImGui, in that order.
 * @return  Core::ExitSuccess on success, Core::ExitFailure if any step fails.
 */
int Application::Init() {
    if (InitSDL() != Core::ExitSuccess) {
        return Core::ExitFailure;
    }
    if (InitVulkan() != Core::ExitSuccess) {
        return Core::ExitFailure;
    }
    if (InitImGui() != Core::ExitSuccess) {
        return Core::ExitFailure;
    }
    return Core::ExitSuccess;
}

/**
 * @brief   Runs the main loop until the user closes the window.
 * @return  Core::ExitSuccess when the loop ends normally.
 */
int Application::Run() {
    const ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
    const ImGuiIO &io = ImGui::GetIO();

    bool done = false;
    while (!done) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) {
                done = true;
            }
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                event.window.windowID == SDL_GetWindowID(m_SDLWindow)) {
                done = true;
            }
        }

        if (SDL_GetWindowFlags(m_SDLWindow) & SDL_WINDOW_MINIMIZED) {
            SDL_Delay(10);
            continue;
        }

        m_Vulkan.ResizeIfNeeded(m_SDLWindow);

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        m_Editor.Draw();
        ImGui::ShowDemoWindow();

        ImGui::Render();
        ImDrawData *main_draw_data = ImGui::GetDrawData();
        const bool main_is_minimized = (main_draw_data->DisplaySize.x <= 0.0f || main_draw_data->DisplaySize.y <= 0.0f);
        m_Vulkan.SetClearColor(clear_color);
        if (!main_is_minimized) {
            m_Vulkan.FrameRender(main_draw_data);
        }

        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }

        if (!main_is_minimized) {
            m_Vulkan.FramePresent();
        }
    }

    return Core::ExitSuccess;
}

/**
 * @brief   Releases every resource created by Init() and prints the collected errors.
 * @note    Safe to call after a partial Init().
 */
void Application::Shutdown() {
    m_Vulkan.WaitIdle();

    if (ImGui::GetCurrentContext() != nullptr) {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
    }

    m_Vulkan.Shutdown();

    if (m_SDLWindow != nullptr) {
        SDL_DestroyWindow(m_SDLWindow);
        m_SDLWindow = nullptr;
    }
    SDL_Quit();

    DumpErrors();
}

void Application::AddError(const std::string &message) {
    m_Errors.push_back(message);
}

void Application::DumpErrors() {
    for (const std::string &error : m_Errors) {
        fprintf(stderr, "[imcsim] Error: %s\n", error.c_str());
    }
    m_Errors.clear();
}

int Application::InitSDL() {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        AddError(std::string("SDL_Init(): ") + SDL_GetError());
        return Core::ExitFailure;
    }

    m_SDLWindowScale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    m_SDLWindowFlags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    m_SDLWindow = SDL_CreateWindow("imcsim", static_cast<int>(1280 * m_SDLWindowScale),
                                   static_cast<int>(800 * m_SDLWindowScale), m_SDLWindowFlags);
    if (m_SDLWindow == nullptr) {
        AddError(std::string("SDL_CreateWindow(): ") + SDL_GetError());
        return Core::ExitFailure;
    }

    return Core::ExitSuccess;
}

int Application::InitVulkan() {
    if (m_Vulkan.Init() != Core::ExitSuccess) {
        AddError(m_Vulkan.LastError());
        return Core::ExitFailure;
    }
    if (m_Vulkan.InitWindow(m_SDLWindow) != Core::ExitSuccess) {
        AddError(m_Vulkan.LastError());
        return Core::ExitFailure;
    }

    SDL_SetWindowPosition(m_SDLWindow, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(m_SDLWindow);
    return Core::ExitSuccess;
}

int Application::InitImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    ImGui::StyleColorsDark();

    ImGuiStyle &style = ImGui::GetStyle();
    style.ScaleAllSizes(m_SDLWindowScale);
    style.FontScaleDpi = m_SDLWindowScale;
    io.ConfigDpiScaleFonts = true;
    io.ConfigDpiScaleViewports = true;

    // With viewports enabled, platform windows should look identical to regular ones
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    ImGui_ImplSDL3_InitForVulkan(m_SDLWindow);
    m_Vulkan.FillImGuiInitInfo(m_VulkanInitInfo);
    ImGui_ImplVulkan_Init(&m_VulkanInitInfo);
    return Core::ExitSuccess;
}

} // namespace GUI
