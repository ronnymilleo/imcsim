/**
 * @file    application.cpp
 * @brief   Top-level application: owns the window, the GPU device, Dear ImGui and the main loop.
 */

#include "application.h"

#include "examples.h"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"
#include "imgui_internal.h"
#include "implot.h"
#include "ngspice_library.h"
#include "shortcuts.h"
#include "simulator.h"
#include "theme.h"
#include <SDL3/SDL.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <string_view>
#include <utility>

namespace GUI {

namespace {

// Wrap width of the example descriptions, in characters of the current font size
constexpr float EXAMPLE_TOOLTIP_WIDTH = 30.0f;

// Debug builds ask for the validation layers of the GPU API; SDL runs without them when they are not installed
#ifdef NDEBUG
constexpr bool GPU_DEBUG_MODE = false;
#else
constexpr bool GPU_DEBUG_MODE = true;
#endif

// Where a build that ships digital.cm keeps it: beside the program on Windows, in the PlugIns folder of the macOS
// bundle (SDL gives its Resources folder), and in lib/imcsim of the Linux layout, as the AppImage does
std::filesystem::path GetCodeModelDirectory(const std::filesystem::path &base_path) {
#if defined(_WIN32)
    return base_path;
#elif defined(__APPLE__)
    return base_path / ".." / "PlugIns";
#else
    return base_path / ".." / "lib" / "imcsim";
#endif
}

} // namespace

/**
 * @brief   Initializes SDL, the GPU device and Dear ImGui, in that order.
 * @return  EXIT_SUCCESS on success, EXIT_FAILURE if any step fails.
 * @note    The simulator is told where the program keeps the digital code models, for a build that ships them.
 */
int Application::Init() {
    if (InitSDL() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    if (const char *base_path = SDL_GetBasePath()) {
        Core::SetCodeModelDirectory(GetCodeModelDirectory(base_path));
    }
    if (InitGPU() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    if (InitImGui() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

/**
 * @brief   Runs the main loop until the user closes the window.
 * @return  EXIT_SUCCESS when the loop ends normally.
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

    return EXIT_SUCCESS;
}

/**
 * @brief   Releases every resource created by Init() and prints the collected errors.
 * @note    Safe to call after a partial Init().
 */
void Application::Shutdown() {
    if (m_GPUDevice != nullptr) {
        SDL_WaitForGPUIdle(m_GPUDevice);
    }

    m_GlowRenderer.Shutdown();
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
    if (m_Errors.empty()) {
        return;
    }
    std::string message;
    for (const std::string &error : m_Errors) {
        fprintf(stderr, "[imcsim] Error: %s\n", error.c_str());
        message += error + "\n";
    }
    // A program started from a file manager or the Windows release build has no console to show stderr
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "imcsim", message.c_str(), nullptr);
    m_Errors.clear();
}

int Application::InitSDL() {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        AddError(std::string("SDL_Init(): ") + SDL_GetError());
        return EXIT_FAILURE;
    }

    m_SDLWindowScale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    m_SDLWindowFlags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    m_SDLWindow = SDL_CreateWindow("imcsim", static_cast<int>(1280 * m_SDLWindowScale),
                                   static_cast<int>(800 * m_SDLWindowScale), m_SDLWindowFlags);
    if (m_SDLWindow == nullptr) {
        AddError(std::string("SDL_CreateWindow(): ") + SDL_GetError());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

// SDL picks the native API of the system (Direct3D 12 on Windows, Vulkan on Linux) among the shader formats the
// Dear ImGui backend ships
int Application::InitGPU() {
    m_GPUDevice = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL |
                                          SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_METALLIB,
                                      GPU_DEBUG_MODE, nullptr);
    if (m_GPUDevice == nullptr) {
        AddError(std::string("SDL_CreateGPUDevice(): ") + SDL_GetError());
        return EXIT_FAILURE;
    }
    if (!SDL_ClaimWindowForGPUDevice(m_GPUDevice, m_SDLWindow)) {
        AddError(std::string("SDL_ClaimWindowForGPUDevice(): ") + SDL_GetError());
        return EXIT_FAILURE;
    }
    SDL_SetGPUSwapchainParameters(m_GPUDevice, m_SDLWindow, SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
                                  SDL_GPU_PRESENTMODE_VSYNC);

    SDL_SetWindowPosition(m_SDLWindow, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(m_SDLWindow);
    return EXIT_SUCCESS;
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
    // Windows float and dock inside the main window only: multi-viewports (windows dragged out as OS windows)
    // stall the main swap chain on Windows while a window moves across the edge of the screen
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ApplyTheme();
    LoadThemeFonts();
    m_EditorWindow.RegisterSettingsHandler();
    m_OutputWindow.RegisterSettingsHandler();
    RegisterThemeSettingsHandler();

    ImGuiStyle &style = ImGui::GetStyle();
    style.ScaleAllSizes(m_SDLWindowScale);
    style.FontScaleDpi = m_SDLWindowScale;
    io.ConfigDpiScaleFonts = true;

    ImGui_ImplSDL3_InitForSDLGPU(m_SDLWindow);
    ImGui_ImplSDLGPU3_InitInfo init_info{};
    init_info.Device = m_GPUDevice;
    init_info.ColorTargetFormat = SDL_GetGPUSwapchainTextureFormat(m_GPUDevice, m_SDLWindow);
    ImGui_ImplSDLGPU3_Init(&init_info);
    // The glows are an effect the editor can do without, so a failure is reported but does not stop the program
    if (m_GlowRenderer.Init(m_GPUDevice, init_info.ColorTargetFormat) != EXIT_SUCCESS) {
        AddError(std::string("Glow renderer: ") + SDL_GetError());
    }
    return EXIT_SUCCESS;
}

void Application::PollEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL3_ProcessEvent(&event);
        const bool main_window_closed =
            event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(m_SDLWindow);
        // A schematic file dropped on the window joins the schematic, like File > Import
        if (event.type == SDL_EVENT_DROP_FILE && event.drop.data != nullptr) {
            m_EditorWindow.RequestImport(std::filesystem::path(event.drop.data));
        }
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
    // Takes in the result of a run that ended, so ShowNewResults() sees it in the same frame
    m_AnalysisControls.Update();
    // The window of points that the plot shows and the schematic plays follows a run whichever window is in front. A
    // live run is seen a stop time at a time, however many points that is
    const std::optional<Core::Transient> &transient = m_Schematic.GetTransient();
    m_Playback.Window.SetSpan(m_AnalysisControls.IsLiveResult()
                                  ? std::optional(m_Schematic.GetSimulationSettings().Transient.StopTime)
                                  : std::nullopt);
    m_Playback.Window.Update(transient ? &*transient : nullptr, m_Schematic.GetTransientVersion(),
                             m_AnalysisControls.IsRunning());
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

    // Runs even while the editor tab is hidden, so quitting, the menus and the shortcuts work from any window
    m_EditorWindow.Update();
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
        // The same list twice: Examples opens one in place of the schematic, Insert Example adds one to it
        const auto draw_examples = [this](const bool insert) {
            for (const char *group : GetExampleGroups()) {
                if (!ImGui::BeginMenu(group)) {
                    continue;
                }
                for (const Example &example : GetExamples()) {
                    if (std::string_view(example.Group) != group) {
                        continue;
                    }
                    if (ImGui::MenuItem(example.Title)) {
                        if (insert) {
                            m_EditorWindow.RequestInsertExample(example);
                        } else {
                            m_EditorWindow.RequestExample(example);
                        }
                    }
                    if (ImGui::BeginItemTooltip()) {
                        ImGui::PushTextWrapPos(ImGui::GetFontSize() * EXAMPLE_TOOLTIP_WIDTH);
                        ImGui::TextUnformatted(example.Description);
                        ImGui::PopTextWrapPos();
                        ImGui::EndTooltip();
                    }
                }
                ImGui::EndMenu();
            }
        };
        if (ImGui::BeginMenu("Examples")) {
            draw_examples(false);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Insert Example")) {
            draw_examples(true);
            ImGui::EndMenu();
        }
        ImGui::Separator();
        // Quitting asks the editor first, like closing the window
        if (ImGui::MenuItem("Quit", IMCSIM_SHORTCUT "Q")) {
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
        m_OutputWindow.DrawViewMenuItems();
        ImGui::Separator();
        // Render() applies it right after the menu; the windows keep their open state and dock back in place
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

// A new result of any analysis opens the Output window on its tab, in front unless the user turned that off; the
// operating point and the transient also show on the schematic, so running one is enough to see it, and a new transient
// plays from its start. Versions also change when results are dropped, which opens nothing
void Application::ShowNewResults() {
    if (m_Schematic.GetTransientVersion() != m_ShownTransientVersion) {
        m_Playback.Reset();
    }
    // A live run is played on the schematic, so its results never take the front from it
    const auto show_if_new = [this](std::size_t &shown, const std::size_t version, const bool has_result,
                                    const PlotTab tab) {
        if (std::exchange(shown, version) != version && has_result) {
            m_OutputWindow.ShowTab(tab, !m_AnalysisControls.IsLive());
        }
    };
    show_if_new(m_ShownOperatingPointVersion, m_Schematic.GetOperatingPointVersion(),
                m_Schematic.GetOperatingPoint().has_value(), PlotTab::OPERATING_POINT);
    show_if_new(m_ShownTransientVersion, m_Schematic.GetTransientVersion(), m_Schematic.GetTransient().has_value(),
                PlotTab::TRANSIENT);
    show_if_new(m_ShownACSweepVersion, m_Schematic.GetACSweepVersion(), m_Schematic.GetACSweep().has_value(),
                PlotTab::AC_SWEEP);
    show_if_new(m_ShownDCSweepVersion, m_Schematic.GetDCSweepVersion(), m_Schematic.GetDCSweep().has_value(),
                PlotTab::DC_SWEEP);
    show_if_new(m_ShownNoiseVersion, m_Schematic.GetNoiseVersion(), m_Schematic.GetNoise().has_value(), PlotTab::NOISE);
    show_if_new(m_ShownTransferFunctionVersion, m_Schematic.GetTransferFunctionVersion(),
                m_Schematic.GetTransferFunction().has_value(), PlotTab::TRANSFER_FUNCTION);
    show_if_new(m_ShownTruthTableVersion, m_Schematic.GetTruthTableVersion(), m_Schematic.GetTruthTable().has_value(),
                PlotTab::TRUTH_TABLE);
}

// The editor, Output, Analysis Settings and Netlist share the main node as tabs, and Properties sits on the right.
// Only the editor starts open: the View menu opens the others in their place, and each result brings Output to the
// front.
// Must run before the windows are drawn. A split returns the new node in the given direction and leaves the rest
// in its last argument
void Application::SetupDefaultLayout(ImGuiID dockspace_id) {
    ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->WorkSize);
    ImGuiID main_id = dockspace_id;
    const ImGuiID properties_id = ImGui::DockBuilderSplitNode(main_id, ImGuiDir_Right, 0.25f, nullptr, &main_id);
    // The other windows share the main node as tabs
    const std::array<const AppWindow *, 4> tabbed_windows{&m_EditorWindow, &m_OutputWindow, &m_SimulationWindow,
                                                          &m_NetlistWindow};
    for (const AppWindow *window : tabbed_windows) {
        ImGui::DockBuilderDockWindow(window->GetWindowTitle().c_str(), main_id);
    }
    ImGui::DockBuilderDockWindow(m_PropertiesWindow.GetWindowTitle().c_str(), properties_id);
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
        m_GlowRenderer.SetRenderPass(render_pass);
        ImGui_ImplSDLGPU3_RenderDrawData(draw_data, command_buffer, render_pass);
        m_GlowRenderer.SetRenderPass(nullptr);
        SDL_EndGPURenderPass(render_pass);
    }

    SDL_SubmitGPUCommandBuffer(command_buffer);
}

} // namespace GUI
