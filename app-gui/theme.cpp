/**
 * @file    theme.cpp
 * @brief   Visual theme of the interface: ImGui and ImPlot styles, colors and fonts.
 */

#include "theme.h"

#include "implot.h"
#include <array>
#include <filesystem>
#include <span>

namespace GUI {

namespace {

constexpr ImVec4 Rgb(const int red, const int green, const int blue, const float alpha = 1.0f) {
    return {static_cast<float>(red) / 255.0f, static_cast<float>(green) / 255.0f, static_cast<float>(blue) / 255.0f,
            alpha};
}

// Warm charcoal surfaces with a single red accent
constexpr ImVec4 Background = Rgb(22, 18, 19);
constexpr ImVec4 Surface = Rgb(31, 25, 26);
constexpr ImVec4 Raised = Rgb(46, 36, 38);
constexpr ImVec4 RaisedHot = Rgb(60, 46, 49);
constexpr ImVec4 Border = Rgb(66, 52, 55);
constexpr ImVec4 Text = Rgb(235, 226, 227);
constexpr ImVec4 TextMuted = Rgb(160, 142, 145);
constexpr ImVec4 Accent = Rgb(214, 62, 68);
constexpr ImVec4 AccentHot = Rgb(236, 96, 100);
constexpr ImVec4 AccentDim = Rgb(156, 42, 49);
// Errors lean toward vermilion and are brighter, so they do not read as accented text
constexpr ImVec4 ErrorText = Rgb(255, 92, 64);
constexpr ImVec4 WarningText = Rgb(255, 204, 102);

constexpr float FontSize = 17.0f;

// Debian/Ubuntu and Arch install the same fonts under different folders
constexpr auto FontCandidates = std::to_array<const char *>({
    "/usr/share/fonts/truetype/inter/Inter-Regular.ttf",
    "/usr/share/fonts/inter/Inter-Regular.ttf",
    "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
    "/usr/share/fonts/noto/NotoSans-Regular.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
});

constexpr auto MonospaceFontCandidates = std::to_array<const char *>({
    "/usr/share/fonts/truetype/jetbrains-mono/JetBrainsMono-Regular.ttf",
    "/usr/share/fonts/TTF/JetBrainsMono-Regular.ttf",
    "/usr/share/fonts/TTF/JetBrainsMonoNerdFont-Regular.ttf",
    "/usr/share/fonts/truetype/noto/NotoSansMono-Regular.ttf",
    "/usr/share/fonts/noto/NotoSansMono-Regular.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
    "/usr/share/fonts/liberation/LiberationMono-Regular.ttf",
});

ImFont *monospace_font = nullptr;

ImFont *AddFirstFont(const std::span<const char *const> candidates) {
    ImGuiIO &io = ImGui::GetIO();
    for (const char *path : candidates) {
        if (!std::filesystem::exists(path)) {
            continue;
        }
        if (ImFont *font = io.Fonts->AddFontFromFileTTF(path, FontSize)) {
            return font;
        }
    }
    return nullptr;
}

void ApplyImGuiStyle() {
    ImGuiStyle &style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(16.0f, 14.0f);
    style.FramePadding = ImVec2(10.0f, 6.0f);
    style.ItemSpacing = ImVec2(10.0f, 9.0f);
    style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
    style.ScrollbarSize = 13.0f;
    style.GrabMinSize = 12.0f;
    style.WindowRounding = 0.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 5.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding = 5.0f;
    style.TabRounding = 5.0f;
    style.WindowBorderSize = 0.0f;
    style.FrameBorderSize = 0.0f;
    style.PopupBorderSize = 1.0f;
    style.SeparatorTextBorderSize = 1.0f;
    style.SeparatorTextPadding = ImVec2(18.0f, 6.0f);

    ImVec4 *colors = style.Colors;
    colors[ImGuiCol_Text] = Text;
    colors[ImGuiCol_TextDisabled] = TextMuted;
    colors[ImGuiCol_WindowBg] = Background;
    colors[ImGuiCol_ChildBg] = Surface;
    colors[ImGuiCol_PopupBg] = Surface;
    colors[ImGuiCol_Border] = Border;
    colors[ImGuiCol_FrameBg] = Raised;
    colors[ImGuiCol_FrameBgHovered] = RaisedHot;
    colors[ImGuiCol_FrameBgActive] = RaisedHot;
    colors[ImGuiCol_TitleBg] = Surface;
    colors[ImGuiCol_TitleBgActive] = Surface;
    colors[ImGuiCol_TitleBgCollapsed] = Surface;
    colors[ImGuiCol_MenuBarBg] = Surface;
    colors[ImGuiCol_ScrollbarBg] = Background;
    colors[ImGuiCol_ScrollbarGrab] = Raised;
    colors[ImGuiCol_ScrollbarGrabHovered] = RaisedHot;
    colors[ImGuiCol_ScrollbarGrabActive] = AccentDim;
    colors[ImGuiCol_CheckMark] = AccentHot;
    colors[ImGuiCol_SliderGrab] = Accent;
    colors[ImGuiCol_SliderGrabActive] = AccentHot;
    // Buttons stay neutral; PrimaryButton() fills the main actions with the accent
    colors[ImGuiCol_Button] = Raised;
    colors[ImGuiCol_ButtonHovered] = AccentDim;
    colors[ImGuiCol_ButtonActive] = Accent;
    colors[ImGuiCol_Header] = Raised;
    colors[ImGuiCol_HeaderHovered] = RaisedHot;
    colors[ImGuiCol_HeaderActive] = AccentDim;
    colors[ImGuiCol_Separator] = Border;
    colors[ImGuiCol_SeparatorHovered] = Accent;
    colors[ImGuiCol_SeparatorActive] = AccentHot;
    colors[ImGuiCol_ResizeGrip] = Raised;
    colors[ImGuiCol_ResizeGripHovered] = Accent;
    colors[ImGuiCol_ResizeGripActive] = AccentHot;
    colors[ImGuiCol_InputTextCursor] = Text;
    colors[ImGuiCol_Tab] = Surface;
    colors[ImGuiCol_TabHovered] = Accent;
    colors[ImGuiCol_TabSelected] = AccentDim;
    colors[ImGuiCol_TabSelectedOverline] = AccentHot;
    colors[ImGuiCol_TabDimmed] = Surface;
    colors[ImGuiCol_TabDimmedSelected] = Raised;
    colors[ImGuiCol_TabDimmedSelectedOverline] = Border;
    colors[ImGuiCol_DockingPreview] = Rgb(214, 62, 68, 0.5f);
    colors[ImGuiCol_DockingEmptyBg] = Background;
    colors[ImGuiCol_TableHeaderBg] = Raised;
    colors[ImGuiCol_TableBorderStrong] = Border;
    colors[ImGuiCol_TableBorderLight] = Raised;
    colors[ImGuiCol_TextLink] = AccentHot;
    colors[ImGuiCol_TextSelectedBg] = Rgb(214, 62, 68, 0.35f);
    colors[ImGuiCol_DragDropTarget] = AccentHot;
    colors[ImGuiCol_NavCursor] = AccentHot;
    colors[ImGuiCol_ModalWindowDimBg] = Rgb(0, 0, 0, 0.55f);
}

void ApplyImPlotStyle() {
    ImPlotStyle &style = ImPlot::GetStyle();
    style.PlotPadding = ImVec2(12.0f, 12.0f);
    style.PlotBorderSize = 1.0f;
    style.Colors[ImPlotCol_PlotBg] = Background;
    style.Colors[ImPlotCol_PlotBorder] = Border;
    style.Colors[ImPlotCol_FrameBg] = Surface;
    style.Colors[ImPlotCol_LegendBg] = Rgb(31, 25, 26, 0.9f);
    style.Colors[ImPlotCol_LegendBorder] = Border;
    style.Colors[ImPlotCol_AxisGrid] = Rgb(255, 255, 255, 0.08f);
    style.Colors[ImPlotCol_AxisText] = TextMuted;
    style.Colors[ImPlotCol_AxisBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    style.Colors[ImPlotCol_InlayText] = TextMuted;
}

} // namespace

/**
 * @brief   Applies the spacing, rounding and red palette to ImGui and ImPlot.
 * @note    Call after both contexts exist and before scaling the style for the display DPI.
 */
void ApplyTheme() {
    ImGui::StyleColorsDark();
    ApplyImGuiStyle();
    ApplyImPlotStyle();
}

/**
 * @brief   Loads the first system UI font and monospace font found.
 * @note    Without a UI font, ImGui's built-in font is added first so it stays the default.
 */
void LoadThemeFonts() {
    if (AddFirstFont(FontCandidates) == nullptr) {
        ImGui::GetIO().Fonts->AddFontDefault();
    }
    monospace_font = AddFirstFont(MonospaceFontCandidates);
}

/**
 * @brief   Returns the monospace font loaded by LoadThemeFonts(), for code-like text.
 * @return  The font, or nullptr when none is installed; PushFont(nullptr, ...) keeps the current one.
 */
ImFont *GetMonospaceFont() {
    return monospace_font;
}

/**
 * @brief   Returns the window background color, also used to clear the Vulkan frame.
 * @return  Background color as RGBA.
 */
ImVec4 GetThemeBackground() {
    return Background;
}

/**
 * @brief   Returns the color of error messages.
 * @return  Error text color as RGBA.
 */
ImVec4 GetErrorTextColor() {
    return ErrorText;
}

/**
 * @brief   Returns the color of warning messages.
 * @return  Warning text color as RGBA.
 */
ImVec4 GetWarningTextColor() {
    return WarningText;
}

/**
 * @brief   Draws a button filled with the accent color, for the main action of a window or dialog.
 * @param[in] label  Button label, as in ImGui::Button().
 * @param[in] size   Button size, as in ImGui::Button(); zero fits the label.
 * @return  Whether the button was clicked.
 */
bool PrimaryButton(const char *label, const ImVec2 size) {
    ImGui::PushStyleColor(ImGuiCol_Button, AccentDim);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentHot);
    const bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    return clicked;
}

} // namespace GUI
