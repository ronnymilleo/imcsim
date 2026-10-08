/**
 * @file    theme.cpp
 * @brief   Visual theme of the interface: ImGui and ImPlot styles, colors and fonts.
 */

#include "theme.h"

#include "imgui_internal.h"
#include "implot.h"
#include "svg_writer.h"
#include <algorithm>
#include <array>
#include <filesystem>
#include <span>
#include <string_view>

namespace GUI {

namespace {

constexpr ImVec4 Rgb(const int red, const int green, const int blue, const float alpha = 1.0f) {
    return {static_cast<float>(red) / 255.0f, static_cast<float>(green) / 255.0f, static_cast<float>(blue) / 255.0f,
            alpha};
}

constexpr ImU32 Col(const int red, const int green, const int blue, const int alpha = 255) {
    return IM_COL32(red, green, blue, alpha);
}

constexpr auto Themes = std::to_array<ThemePalette>({
    // Warm charcoal surfaces with a single red accent
    {.Name = "Ember",
     .Light = false,
     .Background = Rgb(22, 18, 19),
     .Surface = Rgb(31, 25, 26),
     .Raised = Rgb(46, 36, 38),
     .RaisedHot = Rgb(60, 46, 49),
     .Border = Rgb(66, 52, 55),
     .Text = Rgb(235, 226, 227),
     .TextMuted = Rgb(160, 142, 145),
     .Accent = Rgb(214, 62, 68),
     .AccentHot = Rgb(236, 96, 100),
     .AccentDim = Rgb(156, 42, 49),
     // Errors lean toward vermilion and are brighter, so they do not read as accented text
     .ErrorText = Rgb(255, 92, 64),
     .WarningText = Rgb(255, 204, 102),
     .PlotGrid = Rgb(255, 255, 255, 0.08f),
     .CanvasBackground = Col(18, 15, 16),
     .GridDot = Col(78, 63, 66),
     .Element = Col(220, 220, 220),
     .Wire = Col(158, 192, 120),
     // Selection stays amber so it is not mistaken for the salmon of node 1
     .Selected = Col(255, 200, 80),
     .CanvasText = Col(255, 255, 255),
     .VoltageLabel = Col(255, 220, 120),
     .HeatStops = {Rgb(96, 80, 84), Rgb(156, 42, 49), Rgb(236, 96, 100), Rgb(255, 214, 120)}},
    // Cool blue-gray surfaces with a blue accent; errors stay red, apart from the accent
    {.Name = "Graphite",
     .Light = false,
     .Background = Rgb(22, 25, 30),
     .Surface = Rgb(30, 34, 41),
     .Raised = Rgb(43, 49, 58),
     .RaisedHot = Rgb(56, 63, 74),
     .Border = Rgb(61, 68, 80),
     .Text = Rgb(225, 230, 237),
     .TextMuted = Rgb(139, 148, 160),
     .Accent = Rgb(56, 132, 244),
     .AccentHot = Rgb(96, 160, 255),
     .AccentDim = Rgb(36, 92, 176),
     .ErrorText = Rgb(255, 107, 94),
     .WarningText = Rgb(240, 190, 90),
     .PlotGrid = Rgb(255, 255, 255, 0.08f),
     .CanvasBackground = Col(17, 19, 23),
     .GridDot = Col(58, 65, 77),
     .Element = Col(215, 220, 228),
     .Wire = Col(120, 190, 150),
     .Selected = Col(255, 200, 80),
     .CanvasText = Col(240, 244, 248),
     .VoltageLabel = Col(255, 220, 120),
     .HeatStops = {Rgb(70, 78, 92), Rgb(36, 92, 176), Rgb(96, 160, 255), Rgb(205, 235, 255)}},
    // A green monochrome CRT, after the P39 I variant of the Green Monochrome Monitor CRT Phosphor theme for Zed
    // (MIT): pure black, one green (#00B400) at several intensities over it, and a brighter one (#00FF66) for what
    // stands out. Node and trace colors keep their hues, so plots stay readable
    {.Name = "Phosphor",
     .Light = false,
     .Background = Rgb(0, 0, 0),
     .Surface = Rgb(0, 34, 0),
     .Raised = Rgb(0, 45, 0),
     .RaisedHot = Rgb(0, 68, 0),
     .Border = Rgb(0, 68, 0),
     .Text = Rgb(0, 180, 0),
     .TextMuted = Rgb(0, 145, 0),
     .Accent = Rgb(0, 135, 0),
     .AccentHot = Rgb(0, 255, 102),
     .AccentDim = Rgb(0, 90, 0),
     // Brightness tells problems apart, as on a monochrome screen
     .ErrorText = Rgb(0, 255, 102),
     .WarningText = Rgb(150, 255, 150),
     .PlotGrid = Rgb(0, 180, 0, 0.15f),
     .CanvasBackground = Col(0, 0, 0),
     .GridDot = Col(0, 56, 0),
     .Element = Col(0, 180, 0),
     .Wire = Col(0, 255, 102),
     .Selected = Col(200, 255, 200),
     .CanvasText = Col(0, 180, 0),
     .VoltageLabel = Col(0, 255, 102),
     .HeatStops = {Rgb(0, 45, 0), Rgb(0, 110, 0), Rgb(0, 180, 0), Rgb(0, 255, 102)},
     .BrightTraces = true},
    // Off-white paper with dark ink and a blue accent, like a printed datasheet; the accent fills stay light so
    // dark text reads on them
    {.Name = "Paper",
     .Light = true,
     .Background = Rgb(246, 244, 239),
     .Surface = Rgb(236, 233, 226),
     .Raised = Rgb(222, 218, 209),
     .RaisedHot = Rgb(208, 203, 192),
     .Border = Rgb(196, 190, 178),
     .Text = Rgb(34, 34, 38),
     .TextMuted = Rgb(110, 106, 100),
     .Accent = Rgb(160, 192, 240),
     .AccentHot = Rgb(70, 130, 220),
     .AccentDim = Rgb(196, 215, 245),
     .ErrorText = Rgb(196, 40, 30),
     .WarningText = Rgb(160, 100, 0),
     .PlotGrid = Rgb(0, 0, 0, 0.08f),
     .CanvasBackground = Col(252, 251, 247),
     .GridDot = Col(200, 195, 185),
     .Element = Col(30, 30, 35),
     .Wire = Col(40, 110, 60),
     .Selected = Col(214, 120, 0),
     .CanvasText = Col(20, 20, 20),
     .VoltageLabel = Col(150, 90, 0),
     .HeatStops = {Rgb(215, 210, 200), Rgb(120, 160, 225), Rgb(36, 99, 200), Rgb(20, 40, 110)}},
});

// Section of imgui.ini that keeps the chosen theme
constexpr const char *SettingsTypeName = "Theme";

std::size_t current_theme = 0;

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

void ApplyImGuiSizes() {
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
}

// Colors only, so a theme can change while running without undoing the display scaling of the sizes
void ApplyImGuiColors(const ThemePalette &palette) {
    ImGuiStyle &style = ImGui::GetStyle();
    if (palette.Light) {
        ImGui::StyleColorsLight(&style);
    } else {
        ImGui::StyleColorsDark(&style);
    }
    const ImVec4 translucent_accent = {palette.Accent.x, palette.Accent.y, palette.Accent.z, 0.5f};
    const ImVec4 selected_text = {palette.Accent.x, palette.Accent.y, palette.Accent.z, 0.35f};
    ImVec4 *colors = style.Colors;
    colors[ImGuiCol_Text] = palette.Text;
    colors[ImGuiCol_TextDisabled] = palette.TextMuted;
    colors[ImGuiCol_WindowBg] = palette.Background;
    colors[ImGuiCol_ChildBg] = palette.Surface;
    colors[ImGuiCol_PopupBg] = palette.Surface;
    colors[ImGuiCol_Border] = palette.Border;
    colors[ImGuiCol_FrameBg] = palette.Raised;
    colors[ImGuiCol_FrameBgHovered] = palette.RaisedHot;
    colors[ImGuiCol_FrameBgActive] = palette.RaisedHot;
    colors[ImGuiCol_TitleBg] = palette.Surface;
    colors[ImGuiCol_TitleBgActive] = palette.Surface;
    colors[ImGuiCol_TitleBgCollapsed] = palette.Surface;
    colors[ImGuiCol_MenuBarBg] = palette.Surface;
    colors[ImGuiCol_ScrollbarBg] = palette.Background;
    colors[ImGuiCol_ScrollbarGrab] = palette.Raised;
    colors[ImGuiCol_ScrollbarGrabHovered] = palette.RaisedHot;
    colors[ImGuiCol_ScrollbarGrabActive] = palette.AccentDim;
    colors[ImGuiCol_CheckMark] = palette.AccentHot;
    colors[ImGuiCol_SliderGrab] = palette.Accent;
    colors[ImGuiCol_SliderGrabActive] = palette.AccentHot;
    // Buttons stay neutral; PrimaryButton() fills the main actions with the accent
    colors[ImGuiCol_Button] = palette.Raised;
    colors[ImGuiCol_ButtonHovered] = palette.AccentDim;
    colors[ImGuiCol_ButtonActive] = palette.Accent;
    colors[ImGuiCol_Header] = palette.Raised;
    colors[ImGuiCol_HeaderHovered] = palette.RaisedHot;
    colors[ImGuiCol_HeaderActive] = palette.AccentDim;
    colors[ImGuiCol_Separator] = palette.Border;
    colors[ImGuiCol_SeparatorHovered] = palette.Accent;
    colors[ImGuiCol_SeparatorActive] = palette.AccentHot;
    colors[ImGuiCol_ResizeGrip] = palette.Raised;
    colors[ImGuiCol_ResizeGripHovered] = palette.Accent;
    colors[ImGuiCol_ResizeGripActive] = palette.AccentHot;
    colors[ImGuiCol_InputTextCursor] = palette.Text;
    colors[ImGuiCol_Tab] = palette.Surface;
    colors[ImGuiCol_TabHovered] = palette.Accent;
    colors[ImGuiCol_TabSelected] = palette.AccentDim;
    colors[ImGuiCol_TabSelectedOverline] = palette.AccentHot;
    colors[ImGuiCol_TabDimmed] = palette.Surface;
    colors[ImGuiCol_TabDimmedSelected] = palette.Raised;
    colors[ImGuiCol_TabDimmedSelectedOverline] = palette.Border;
    colors[ImGuiCol_DockingPreview] = translucent_accent;
    colors[ImGuiCol_DockingEmptyBg] = palette.Background;
    colors[ImGuiCol_TableHeaderBg] = palette.Raised;
    colors[ImGuiCol_TableBorderStrong] = palette.Border;
    colors[ImGuiCol_TableBorderLight] = palette.Raised;
    colors[ImGuiCol_TextLink] = palette.AccentHot;
    colors[ImGuiCol_TextSelectedBg] = selected_text;
    colors[ImGuiCol_DragDropTarget] = palette.AccentHot;
    colors[ImGuiCol_NavCursor] = palette.AccentHot;
    colors[ImGuiCol_ModalWindowDimBg] = Rgb(0, 0, 0, 0.55f);
}

void ApplyImPlotColors(const ThemePalette &palette) {
    ImPlotStyle &style = ImPlot::GetStyle();
    style.Colors[ImPlotCol_PlotBg] = palette.Background;
    style.Colors[ImPlotCol_PlotBorder] = palette.Border;
    style.Colors[ImPlotCol_FrameBg] = palette.Surface;
    style.Colors[ImPlotCol_LegendBg] = {palette.Surface.x, palette.Surface.y, palette.Surface.z, 0.9f};
    style.Colors[ImPlotCol_LegendBorder] = palette.Border;
    style.Colors[ImPlotCol_LegendText] = palette.Text;
    style.Colors[ImPlotCol_TitleText] = palette.Text;
    style.Colors[ImPlotCol_AxisGrid] = palette.PlotGrid;
    style.Colors[ImPlotCol_AxisText] = palette.TextMuted;
    style.Colors[ImPlotCol_AxisBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    style.Colors[ImPlotCol_InlayText] = palette.TextMuted;
    style.Colors[ImPlotCol_Crosshairs] = palette.Text;
}

void ApplyColors(const ThemePalette &palette) {
    ApplyImGuiColors(palette);
    ApplyImPlotColors(palette);
}

} // namespace

/**
 * @brief   Applies the spacing, rounding and the colors of the current theme to ImGui and ImPlot.
 * @note    Call after both contexts exist and before scaling the style for the display DPI.
 */
void ApplyTheme() {
    ApplyImGuiSizes();
    ImPlotStyle &plot_style = ImPlot::GetStyle();
    plot_style.PlotPadding = ImVec2(12.0f, 12.0f);
    plot_style.PlotBorderSize = 1.0f;
    ApplyColors(GetPalette());
}

/**
 * @brief   Keeps the chosen theme in imgui.ini, by name, and applies it when the file is read.
 * @note    Call once after the ImGui context exists and before the first frame.
 */
void RegisterThemeSettingsHandler() {
    ImGuiSettingsHandler handler;
    handler.TypeName = SettingsTypeName;
    handler.TypeHash = ImHashStr(SettingsTypeName);
    handler.ReadOpenFn = [](ImGuiContext *, ImGuiSettingsHandler *, const char *) -> void * {
        // Any non-null pointer tells ImGui to pass the lines of the section to ReadLineFn
        return &current_theme;
    };
    handler.ReadLineFn = [](ImGuiContext *, ImGuiSettingsHandler *, void *, const char *line) {
        const std::string_view text = line;
        constexpr std::string_view NameKey = "Name=";
        if (!text.starts_with(NameKey)) {
            return;
        }
        const auto themes = GetThemes();
        const auto theme = std::ranges::find(themes, text.substr(NameKey.size()), &ThemePalette::Name);
        if (theme != themes.end()) {
            SetTheme(static_cast<std::size_t>(theme - themes.begin()));
        }
    };
    handler.WriteAllFn = [](ImGuiContext *, ImGuiSettingsHandler *, ImGuiTextBuffer *buffer) {
        buffer->appendf("[%s][Current]\n", SettingsTypeName);
        buffer->appendf("Name=%s\n\n", GetPalette().Name);
    };
    ImGui::AddSettingsHandler(&handler);
}

/**
 * @brief   Lists the themes the View menu offers.
 * @return  Every theme, the default first.
 */
std::span<const ThemePalette> GetThemes() {
    return Themes;
}

/**
 * @brief   Returns the position of the current theme in GetThemes().
 * @return  Index of the current theme.
 */
std::size_t GetThemeIndex() {
    return current_theme;
}

/**
 * @brief   Switches to another theme and applies its colors to ImGui and ImPlot right away.
 * @param[in] index  Position in GetThemes(); out of range indices are ignored.
 * @note    Sizes are left alone, so it can be called while running, after the style was scaled for the display.
 */
void SetTheme(const std::size_t index) {
    if (index >= Themes.size()) {
        return;
    }
    current_theme = index;
    ApplyColors(Themes[index]);
}

/**
 * @brief   Returns the colors of the current theme.
 * @return  The palette, valid for the whole program.
 */
const ThemePalette &GetPalette() {
    return Themes[current_theme];
}

/**
 * @brief   Makes a trace or probe color readable on the background of the current theme.
 * @param[in] color  Color picked for a dark background, such as a node or trace color.
 * @return  The same color on a dark theme, scaled up until its strongest channel is full on a theme with
 *          BrightTraces; on a light theme, darkened as for print.
 */
ImU32 AdaptToBackground(const ImU32 color) {
    const ThemePalette &palette = GetPalette();
    if (palette.BrightTraces) {
        const ImVec4 rgba = ImGui::ColorConvertU32ToFloat4(color);
        const float strongest = std::max({rgba.x, rgba.y, rgba.z});
        if (strongest <= 0.0f) {
            return color;
        }
        return ImGui::ColorConvertFloat4ToU32({rgba.x / strongest, rgba.y / strongest, rgba.z / strongest, rgba.w});
    }
    if (!palette.Light) {
        return color;
    }
    const ExportColor adjusted = AdjustColorForPrint({static_cast<std::uint8_t>((color >> IM_COL32_R_SHIFT) & 0xFF),
                                                      static_cast<std::uint8_t>((color >> IM_COL32_G_SHIFT) & 0xFF),
                                                      static_cast<std::uint8_t>((color >> IM_COL32_B_SHIFT) & 0xFF)});
    return IM_COL32(adjusted.Red, adjusted.Green, adjusted.Blue, (color >> IM_COL32_A_SHIFT) & 0xFF);
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
    return GetPalette().Background;
}

/**
 * @brief   Returns the color of error messages.
 * @return  Error text color as RGBA.
 */
ImVec4 GetErrorTextColor() {
    return GetPalette().ErrorText;
}

/**
 * @brief   Returns the color of warning messages.
 * @return  Warning text color as RGBA.
 */
ImVec4 GetWarningTextColor() {
    return GetPalette().WarningText;
}

/**
 * @brief   Returns the color of what is about to be placed or drawn on the schematic: parts and wires in progress.
 * @return  The bright accent, partly transparent.
 */
ImU32 GetPreviewColor() {
    ImVec4 color = GetPalette().AccentHot;
    color.w = 0.67f;
    return ImGui::ColorConvertFloat4ToU32(color);
}

/**
 * @brief   Returns the color that outlines what a click would act on, such as the item under the probe.
 * @return  The bright accent.
 */
ImU32 GetHighlightColor() {
    return ImGui::ColorConvertFloat4ToU32(GetPalette().AccentHot);
}

/**
 * @brief   Draws a button filled with the accent color, for the main action of a window or dialog.
 * @param[in] label  Button label, as in ImGui::Button().
 * @param[in] size   Button size, as in ImGui::Button(); zero fits the label.
 * @return  Whether the button was clicked.
 */
bool PrimaryButton(const char *label, const ImVec2 size) {
    const ThemePalette &palette = GetPalette();
    ImGui::PushStyleColor(ImGuiCol_Button, palette.AccentDim);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, palette.Accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, palette.AccentHot);
    const bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    return clicked;
}

} // namespace GUI
