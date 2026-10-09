/**
 * @file    application.cpp
 * @brief   Top-level application: owns the window, the GPU device, Dear ImGui and the main loop.
 */

#include "application.h"

#include "error_codes.h"
#include "examples.h"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"
#include "imgui_internal.h"
#include "implot.h"
#include "theme.h"
#include <SDL3/SDL.h>
#include <array>
#include <cstdio>
#include <format>
#include <utility>

namespace GUI {

namespace {

// Wrap width of the example descriptions, in characters of the current font size
constexpr float ExampleTooltipWidth = 30.0f;

// Debug builds ask for the validation layers of the GPU API; SDL runs without them when they are not installed
#ifdef NDEBUG
constexpr bool GPUDebugMode = false;
#else
constexpr bool GPUDebugMode = true;
#endif

} // namespace

/**
 * @brief   Initializes SDL, the GPU device and Dear ImGui, in that order.
 * @return  Core::ExitSuccess on success, Core::ExitFailure if any step fails.
 */
int Application::Init() {
    if (InitSDL() != Core::ExitSuccess) {
        return Core::ExitFailure;
    }
    if (InitGPU() != Core::ExitSuccess) {
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
    while (m_IsOpen) {
        PollEvents();

        if (SDL_GetWindowFlags(m_SDLWindow) & SDL_WINDOW_MINIMIZED) {
            SDL_Delay(10);
            continue;
        }

        NewFrame();
        Render();
        EndFrame();
    }

    return Core::ExitSuccess;
}

/**
 * @brief   Releases every resource created by Init() and prints the collected errors.
 * @note    Safe to call after a partial Init().
 */
void Application::Shutdown() {
    if (m_GPUDevice != nullptr) {
        SDL_WaitForGPUIdle(m_GPUDevice);
    }

    if (ImGui::GetCurrentContext() != nullptr) {
        ImGui_ImplSDLGPU3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImPlot::DestroyContext();
        ImGui::DestroyContext();
    }

    if (m_GPUDevice != nullptr) {
        SDL_ReleaseWindowFromGPUDevice(m_GPUDevice, m_SDLWindow);
        SDL_DestroyGPUDevice(m_GPUDevice);
        m_GPUDevice = nullptr;
    }

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
    m_SDLWindowFlags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    m_SDLWindow = SDL_CreateWindow("imcsim", static_cast<int>(1280 * m_SDLWindowScale),
                                   static_cast<int>(800 * m_SDLWindowScale), m_SDLWindowFlags);
    if (m_SDLWindow == nullptr) {
        AddError(std::string("SDL_CreateWindow(): ") + SDL_GetError());
        return Core::ExitFailure;
    }

    return Core::ExitSuccess;
}

// SDL picks the native API of the system (Direct3D 12 on Windows, Vulkan on Linux) among the shader formats the
// Dear ImGui backend ships
int Application::InitGPU() {
    m_GPUDevice = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL |
                                          SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_METALLIB,
                                      GPUDebugMode, nullptr);
    if (m_GPUDevice == nullptr) {
        AddError(std::string("SDL_CreateGPUDevice(): ") + SDL_GetError());
        return Core::ExitFailure;
    }
    if (!SDL_ClaimWindowForGPUDevice(m_GPUDevice, m_SDLWindow)) {
        AddError(std::string("SDL_ClaimWindowForGPUDevice(): ") + SDL_GetError());
        return Core::ExitFailure;
    }
    SDL_SetGPUSwapchainParameters(m_GPUDevice, m_SDLWindow, SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
                                  SDL_GPU_PRESENTMODE_VSYNC);

    SDL_SetWindowPosition(m_SDLWindow, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(m_SDLWindow);
    return Core::ExitSuccess;
}

int Application::InitImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();

    ImGuiIO &io = ImGui::GetIO();
    // The layout and preferences go to the user's data folder (~/.local/share/imcsim on Linux, or under
    // XDG_DATA_HOME), so they do not depend on where the program starts or need a writable install folder
    if (char *pref_path = SDL_GetPrefPath(nullptr, "imcsim")) {
        m_SettingsPath = std::string(pref_path) + "imgui.ini";
        SDL_free(pref_path);
        io.IniFilename = m_SettingsPath.c_str();
    }
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    ApplyTheme();
    LoadThemeFonts();
    m_EditorWindow.RegisterSettingsHandler();
    RegisterThemeSettingsHandler();

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

    ImGui_ImplSDL3_InitForSDLGPU(m_SDLWindow);
    ImGui_ImplSDLGPU3_InitInfo init_info{};
    init_info.Device = m_GPUDevice;
    init_info.ColorTargetFormat = SDL_GetGPUSwapchainTextureFormat(m_GPUDevice, m_SDLWindow);
    // The other platform windows get swap chains like the main one
    init_info.SwapchainComposition = SDL_GPU_SWAPCHAINCOMPOSITION_SDR;
    init_info.PresentMode = SDL_GPU_PRESENTMODE_VSYNC;
    ImGui_ImplSDLGPU3_Init(&init_info);
    return Core::ExitSuccess;
}

void Application::PollEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL3_ProcessEvent(&event);
        const bool main_window_closed =
            event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(m_SDLWindow);
        if (event.type == SDL_EVENT_QUIT || main_window_closed) {
            // The editor asks before unsaved changes are lost and Render() quits once it confirms. Minimized
            // windows draw no frames, so the window is restored for the question to show up
            m_EditorWindow.RequestQuit();
            if (SDL_GetWindowFlags(m_SDLWindow) & SDL_WINDOW_MINIMIZED) {
                SDL_RestoreWindow(m_SDLWindow);
            }
        }
    }
}

void Application::NewFrame() {
    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void Application::Render() {
    // The menu bar goes first so the dockspace fills the space left below it
    DrawMainMenuBar();
    UpdateWindowTitle();
    ShowNewResults();

    // A fixed ID lets the layout be rebuilt before the dockspace is submitted this frame
    const ImGuiID dockspace_id = ImGui::GetID("MainDockSpace");
    if (m_ResetLayout) {
        ImGui::DockBuilderRemoveNode(dockspace_id);
        m_ResetLayout = false;
    }
    // No node means imgui.ini has no saved layout, or it was just reset, so the default one is built
    if (ImGui::DockBuilderGetNode(dockspace_id) == nullptr) {
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
        SetupDefaultLayout(dockspace_id);
    }
    ImGui::DockSpaceOverViewport(dockspace_id, ImGui::GetMainViewport());

    m_EditorWindow.Render();
    m_PropertiesWindow.Render();
    m_NetlistWindow.Render();
    m_SimulationWindow.Render();
    m_OutputWindow.Render();
    // A held mouse button or a focused text field is an interaction in progress, which becomes one undo step
    // once it ends
    if (!ImGui::IsAnyItemActive()) {
        m_Schematic.CommitUndoStep();
    }
    if (m_EditorWindow.IsQuitConfirmed()) {
        m_IsOpen = false;
    }
    ImGui::Render();
}

void Application::DrawMainMenuBar() {
    if (!ImGui::BeginMainMenuBar()) {
        return;
    }
    if (ImGui::BeginMenu("File")) {
        m_EditorWindow.DrawFileMenuItems();
        ImGui::Separator();
        if (ImGui::BeginMenu("Examples")) {
            for (const Example &example : GetExamples()) {
                if (ImGui::MenuItem(example.Title)) {
                    m_EditorWindow.RequestExample(example);
                }
                if (ImGui::BeginItemTooltip()) {
                    ImGui::PushTextWrapPos(ImGui::GetFontSize() * ExampleTooltipWidth);
                    ImGui::TextUnformatted(example.Description);
                    ImGui::PopTextWrapPos();
                    ImGui::EndTooltip();
                }
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        // Quitting asks the editor first, like closing the window
        if (ImGui::MenuItem("Quit", "Ctrl+Q")) {
            m_EditorWindow.RequestQuit();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        m_EditorWindow.DrawEditMenuItems();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        m_EditorWindow.DrawViewMenuItems();
        if (ImGui::BeginMenu("Theme")) {
            const auto themes = GetThemes();
            for (std::size_t index = 0; index < themes.size(); ++index) {
                if (ImGui::MenuItem(themes[index].Name, nullptr, index == GetThemeIndex())) {
                    SetTheme(index);
                    ImGui::MarkIniSettingsDirty();
                }
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        // The editor cannot be closed, so only the side windows are listed
        const std::array<AppWindow *, 4> closable_windows = {&m_PropertiesWindow, &m_NetlistWindow, &m_SimulationWindow,
                                                             &m_OutputWindow};
        for (AppWindow *window : closable_windows) {
            bool open = window->IsOpen();
            if (ImGui::MenuItem(window->GetWindowTitle().c_str(), nullptr, &open)) {
                window->SetOpen(open);
            }
        }
        ImGui::Separator();
        // Render() applies it right after the menu; the side windows keep their open state and dock back in place,
        // and Output floats outside the layout
        if (ImGui::MenuItem("Reset Layout")) {
            m_ResetLayout = true;
        }
        ImGui::EndMenu();
    }
    // The version sits at the right end of the bar, out of the way
    const std::string version = std::format("imcsim {}", IMCSIM_VERSION);
    ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize(version.c_str()).x -
                    ImGui::GetStyle().ItemSpacing.x * 2.0f);
    ImGui::TextDisabled("%s", version.c_str());
    ImGui::EndMainMenuBar();
}

// The window title names the schematic, with an asterisk while it has unsaved changes, as editors do
void Application::UpdateWindowTitle() {
    const auto &file_path = m_Schematic.GetFilePath();
    const std::string title = std::format("{}{} - imcsim", file_path ? file_path->filename().string() : "Untitled",
                                          m_Schematic.IsModified() ? " *" : "");
    if (title != m_WindowTitle) {
        SDL_SetWindowTitle(m_SDLWindow, title.c_str());
        m_WindowTitle = title;
    }
}

// A new result of any analysis opens the Output window on its tab; the operating point also shows on the schematic, so
// running one is enough to see it. Versions also change when results are dropped, which opens nothing
void Application::ShowNewResults() {
    const auto show_if_new = [this](std::size_t &shown, const std::size_t version, const bool has_result,
                                    const PlotTab tab) {
        if (std::exchange(shown, version) != version && has_result) {
            m_OutputWindow.ShowTab(tab);
        }
    };
    show_if_new(m_ShownOperatingPointVersion, m_Schematic.GetOperatingPointVersion(),
                m_Schematic.GetOperatingPoint().has_value(), PlotTab::OperatingPoint);
    show_if_new(m_ShownTransientVersion, m_Schematic.GetTransientVersion(), m_Schematic.GetTransient().has_value(),
                PlotTab::Transient);
    show_if_new(m_ShownACSweepVersion, m_Schematic.GetACSweepVersion(), m_Schematic.GetACSweep().has_value(),
                PlotTab::ACSweep);
    show_if_new(m_ShownDCSweepVersion, m_Schematic.GetDCSweepVersion(), m_Schematic.GetDCSweep().has_value(),
                PlotTab::DCSweep);
}

// The editor fills the window; Properties, Analysis Settings and Netlist start closed and dock below it as tabs
// when opened, and Output floats.
// Must run before the windows are drawn. A split returns the new node in the given direction and leaves the rest
// in its last argument
void Application::SetupDefaultLayout(ImGuiID dockspace_id) {
    ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->WorkSize);
    ImGuiID editor_id = dockspace_id;
    const ImGuiID properties_id = ImGui::DockBuilderSplitNode(editor_id, ImGuiDir_Down, 0.25f, nullptr, &editor_id);
    ImGui::DockBuilderDockWindow(m_EditorWindow.GetWindowTitle().c_str(), editor_id);
    ImGui::DockBuilderDockWindow(m_PropertiesWindow.GetWindowTitle().c_str(), properties_id);
    ImGui::DockBuilderDockWindow(m_SimulationWindow.GetWindowTitle().c_str(), properties_id);
    ImGui::DockBuilderDockWindow(m_NetlistWindow.GetWindowTitle().c_str(), properties_id);
    ImGui::DockBuilderFinish(dockspace_id);
}

void Application::EndFrame() {
    SDL_GPUCommandBuffer *command_buffer = SDL_AcquireGPUCommandBuffer(m_GPUDevice);
    if (command_buffer == nullptr) {
        return;
    }

    ImDrawData *draw_data = ImGui::GetDrawData();
    SDL_GPUTexture *swapchain_texture = nullptr;
    const bool is_minimized = draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f;
    if (!is_minimized &&
        SDL_WaitAndAcquireGPUSwapchainTexture(command_buffer, m_SDLWindow, &swapchain_texture, nullptr, nullptr) &&
        swapchain_texture != nullptr) {
        // Uploads the vertex and index buffers, which cannot happen inside a render pass
        ImGui_ImplSDLGPU3_PrepareDrawData(draw_data, command_buffer);

        const ImVec4 background = GetThemeBackground();
        SDL_GPUColorTargetInfo target_info{};
        target_info.texture = swapchain_texture;
        target_info.clear_color = SDL_FColor{background.x, background.y, background.z, background.w};
        target_info.load_op = SDL_GPU_LOADOP_CLEAR;
        target_info.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass *render_pass = SDL_BeginGPURenderPass(command_buffer, &target_info, 1, nullptr);
        ImGui_ImplSDLGPU3_RenderDrawData(draw_data, command_buffer, render_pass);
        SDL_EndGPURenderPass(render_pass);
    }

    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }

    SDL_SubmitGPUCommandBuffer(command_buffer);
}

} // namespace GUI
