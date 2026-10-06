/***********************************************************************************************************************
* @file    application.cpp
 * @brief
 * @details
 *
 * @project imcsim
 * @author  ronnymilleo
 * @date    10/6/26
***********************************************************************************************************************/

/***********************************************************************************************************************
 Includes
***********************************************************************************************************************/

#include "application.h"
#include "error_codes.h"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include <SDL3/SDL.h>
#include <cstdio>

/***********************************************************************************************************************
 Method Definitions
***********************************************************************************************************************/

int Application::Init() {
    if (InitSDL())
        return Core::ExitFailure;
    if (InitVulkan())
        return Core::ExitFailure;
    if (InitIMGUI())
        return Core::ExitFailure;
    return Core::ExitSuccess;
}

void Application::AddError(const std::string &message) { m_Errors.push_back(message); }

void Application::DumpErrors() {
    for (const std::string &error : m_Errors)
        fprintf(stderr, "[imcsim] Error: %s\n", error.c_str());
    m_Errors.clear();
}

int Application::InitSDL() {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        AddError(std::string("SDL_Init(): ") + SDL_GetError());
        return Core::ExitFailure;
    }

    // Create window with Vulkan graphics context
    m_SDLWindowScale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    m_SDLWindowFlags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    m_SDLWindow = SDL_CreateWindow("Dear ImGui SDL3+Vulkan example", static_cast<int>(1280 * m_SDLWindowScale),
                                   static_cast<int>(800 * m_SDLWindowScale), m_SDLWindowFlags);

    if (m_SDLWindow == nullptr) {
        AddError(std::string("SDL_CreateWindow(): ") + SDL_GetError());
        return Core::ExitFailure;
    }

    return Core::ExitSuccess;
}

int Application::InitVulkan() {
    if (!m_Vulkan.Init()) {
        AddError(m_Vulkan.LastError());
        return Core::ExitFailure;
    }
    if (!m_Vulkan.InitWindow(m_SDLWindow)) {
        AddError(m_Vulkan.LastError());
        return Core::ExitFailure;
    }

    SDL_SetWindowPosition(m_SDLWindow, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(m_SDLWindow);
    return Core::ExitSuccess;
}

int Application::InitIMGUI() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO &io = ImGui::GetIO();
    (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;     // Enable Docking
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;   // Enable Multi-Viewport

    ImGui::StyleColorsDark();

    // Setup scaling
    ImGuiStyle &style = ImGui::GetStyle();
    style.ScaleAllSizes(m_SDLWindowScale);
    style.FontScaleDpi = m_SDLWindowScale;
    io.ConfigDpiScaleFonts = true;
    io.ConfigDpiScaleViewports = true;

    // When viewports are enabled we tweak WindowRounding/WindowBg so platform
    // windows can look identical to regular ones.
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    // Setup Platform/Renderer backends
    ImGui_ImplSDL3_InitForVulkan(m_SDLWindow);
    m_Vulkan.FillImGuiInitInfo(m_VulkanInitInfo);
    ImGui_ImplVulkan_Init(&m_VulkanInitInfo);
    return Core::ExitSuccess;
}

int Application::Run() {
    // Our state
    bool show_demo_window = true;
    bool show_another_window = false;
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
    ImGuiIO &io = ImGui::GetIO();

    // Main loop
    bool done = false;
    while (!done) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT)
                done = true;
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(m_SDLWindow))
                done = true;
        }

        // [If using SDL_MAIN_USE_CALLBACKS: all code below would likely be your
        // SDL_AppIterate() function]
        if (SDL_GetWindowFlags(m_SDLWindow) & SDL_WINDOW_MINIMIZED) {
            SDL_Delay(10);
            continue;
        }

        m_Vulkan.ResizeIfNeeded(m_SDLWindow);

        // Start the Dear ImGui frame
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        // 1. Show the big demo window (Most of the sample code is in
        // ImGui::ShowDemoWindow()! You can browse its code to learn more about
        // Dear ImGui!).
        if (show_demo_window)
            ImGui::ShowDemoWindow(&show_demo_window);

        // 2. Show a simple window that we create ourselves. We use a Begin/End
        // pair to create a named window.
        {
            static float f = 0.0f;
            static int counter = 0;

            ImGui::Begin("Hello, world!"); // Create a window called "Hello, world!"
            // and append into it.

            ImGui::Text("This is some useful text."); // Display some text (you can
            // use a format strings too)
            ImGui::Checkbox("Demo Window",
                            &show_demo_window); // Edit bools storing our window
            // open/close state
            ImGui::Checkbox("Another Window", &show_another_window);

            ImGui::SliderFloat("float", &f, 0.0f,
                               1.0f); // Edit 1 float using a slider from 0.0f to 1.0f
            ImGui::ColorEdit3("clear color",
                              (float *)&clear_color); // Edit 3 floats representing a color

            if (ImGui::Button("Button")) // Buttons return true when clicked (most
                // widgets return true when edited/activated)
                counter++;
            ImGui::SameLine();
            ImGui::Text("counter = %d", counter);
            ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
            ImGui::End();
        }

        // 3. Show another simple window.
        if (show_another_window) {
            ImGui::Begin("Another Window",
                         &show_another_window); // Pass a pointer to our bool
            // variable (the
            // window will have a closing button that will
            // clear the bool when clicked)
            ImGui::Text("Hello from another window!");
            if (ImGui::Button("Close Me"))
                show_another_window = false;
            ImGui::End();
        }

        // Rendering
        ImGui::Render();
        ImDrawData *main_draw_data = ImGui::GetDrawData();
        const bool main_is_minimized = (main_draw_data->DisplaySize.x <= 0.0f || main_draw_data->DisplaySize.y <= 0.0f);
        m_Vulkan.SetClearColor(clear_color);
        if (!main_is_minimized)
            m_Vulkan.FrameRender(main_draw_data);

        // Update and Render additional Platform Windows
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }

        // Present Main Platform Window
        if (!main_is_minimized)
            m_Vulkan.FramePresent();
    }

    return Core::ExitSuccess;
}

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

/***********************************************************************************************************************
 End of file
***********************************************************************************************************************/
