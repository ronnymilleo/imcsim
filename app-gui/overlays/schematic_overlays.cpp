/**
 * @file    schematic_overlays.cpp
 * @brief   What the editor draws over the schematic from a result: heat colors, glows, node voltages and the legend.
 */

#include "overlays/schematic_overlays.h"

#include "base/node_colors.h"
#include "base/theme.h"
#include "circuit/components/logic_input.h"
#include "overlays/color_legend.h"
#include "overlays/heat_glow.h"
#include "overlays/hovered_value.h"
#include "overlays/led_glow.h"
#include "overlays/live_current.h"
#include "overlays/node_readouts.h"
#include "overlays/part_heat.h"
#include "overlays/readout_placement.h"
#include "results/ac_sensitivity_view.h"
#include "results/current_flow.h"
#include "results/sensitivity_view.h"
#include "schematic/live_input.h"
#include "spice/spice_value.h"
#include "ui_elements/ui_fuse.h"
#include "ui_elements/ui_logic_input.h"
#include "ui_elements/ui_logic_output.h"
#include "ui_elements/ui_seven_segment.h"
#include <algorithm>
#include <cfloat>
#include <format>
#include <optional>
#include <string>
#include <vector>

namespace GUI {

namespace {

/// Junction dot radius relative to the zoom.
constexpr float JUNCTION_RADIUS_SCALE = 0.15f;
/// Smallest junction dot radius, so it stays visible when zoomed out.
constexpr float MIN_JUNCTION_RADIUS = 2.5f;

// Voltage readouts, in font sizes
/// Space around the text of a readout, across.
constexpr float READOUT_PADDING_X = 0.35f;
/// Space around the text of a readout, up and down.
constexpr float READOUT_PADDING_Y = 0.1f;
constexpr float READOUT_DOT_RADIUS = 0.22f;
/// Space between a wire and its readout.
constexpr float READOUT_GAP = 0.3f;

// Heat legend, in font sizes, at the bottom left of the canvas
constexpr float LEGEND_WIDTH = 10.0f;
constexpr float LEGEND_HEIGHT = 0.6f;
/// Bands of the gradient of the legend.
constexpr int LEGEND_STEPS = 32;
/// Radius of the glow of a dim LED, in grid units, over its symbol (4 units long).
constexpr float GLOW_MIN_RADIUS = 2.0f;
/// Radius of the glow of a fully lit LED, beyond its symbol. Additive light can be stronger than a glow blended over a
/// light background, which would hide the symbol.
constexpr float GLOW_MAX_RADIUS = 3.5f;
/// Hot resistors glow smaller than LEDs, around their symbol.
constexpr float HEAT_GLOW_MIN_RADIUS = 1.5f;
constexpr float HEAT_GLOW_MAX_RADIUS = 2.5f;
/// The warm orange of a hot resistor.
constexpr ImVec4 HEAT_LIGHT = {1.0f, 0.45f, 0.1f, 1.0f};
constexpr float ADDITIVE_GLOW_INTENSITY = 0.9f;
constexpr float BLENDED_GLOW_INTENSITY = 0.5f;
/// A selected wire keeps its heat color and gets a halo behind it this much wider, in pixels.
constexpr float SELECTION_HALO_EXTRA = 6.0f;
/// The halo is partly see-through.
constexpr ImU32 SELECTION_HALO_ALPHA = 150;
/// During a live run the parts the user can set are outlined faintly, so they stand out from the parts whose edits end
/// the run without hiding the heat colors.
constexpr ImU32 LIVE_PART_OUTLINE_ALPHA = 110;
/// The part under the cursor or held is outlined fully.
constexpr ImU32 LIVE_PART_HOVERED_ALPHA = 255;

/**
 * @brief   Light of an LED color, kept bright so it reads on any background; an LED with custom parameters glows warm
 *          white.
 */
ImVec4 GetLEDLight(const Core::LEDColor color) {
    switch (color) {
    case Core::LEDColor::RED:
        return {1.0f, 0.15f, 0.08f, 1.0f};
    case Core::LEDColor::GREEN:
        return {0.2f, 1.0f, 0.25f, 1.0f};
    case Core::LEDColor::BLUE:
        return {0.2f, 0.4f, 1.0f, 1.0f};
    case Core::LEDColor::NONE:
    default:
        break;
    }
    return {1.0f, 0.9f, 0.7f, 1.0f};
}

/**
 * @struct  ReadoutStyle
 * @brief   Sizes and colors of the voltage readouts of a frame, which follow their font size.
 */
struct ReadoutStyle {
    ImFont *Font = nullptr;
    float FontSize = 0.0f;
    /// Space around the text, across and up and down.
    ImVec2 Padding;
    float DotRadius = 0.0f;
    /// Space between a wire and its readout.
    float Gap = 0.0f;
    float Rounding = 0.0f;
    ImU32 Fill = 0;
    ImU32 Border = 0;
    ImU32 Text = 0;
};

/**
 * @brief   The style of the readouts at a font size: a pill of the surface color with a border, in the text color of
 *          the voltage labels.
 */
ReadoutStyle MakeReadoutStyle(const float font_size) {
    const ThemePalette &palette = GetPalette();
    return ReadoutStyle{
        .Font = ImGui::GetFont(),
        .FontSize = font_size,
        .Padding = {font_size * READOUT_PADDING_X, font_size * READOUT_PADDING_Y},
        .DotRadius = font_size * READOUT_DOT_RADIUS,
        .Gap = font_size * READOUT_GAP,
        // The corners shrink with the pill, so a small one stays a pill and not a circle
        .Rounding = ImGui::GetStyle().FrameRounding * font_size / ImGui::GetFontSize(),
        .Fill = ImGui::ColorConvertFloat4ToU32(palette.Surface),
        .Border = ImGui::ColorConvertFloat4ToU32(palette.Border),
        .Text = palette.VoltageLabel,
    };
}

/**
 * @brief   The size of the pill of a readout: the text, led by the dot, with padding around them.
 */
ImVec2 MeasureReadout(const ReadoutStyle &style, const std::string &text) {
    const ImVec2 text_size = style.Font->CalcTextSizeA(style.FontSize, FLT_MAX, 0.0f, text.c_str());
    return {style.Padding.x * 3.0f + style.DotRadius * 2.0f + text_size.x, style.Padding.y * 2.0f + text_size.y};
}

/**
 * @brief   Draws a readout: its pill with a border, a dot of the color of the node and the text.
 * @param[in,out] draw_list   Where to draw.
 * @param[in]     style       Sizes and colors of the readouts.
 * @param[in]     text        The voltage, such as "4.5V".
 * @param[in]     node_color  Color of the node, for the dot.
 * @param[in]     corner      Top left corner of the pill on screen, in pixels.
 */
void DrawReadout(ImDrawList *draw_list,
                 const ReadoutStyle &style,
                 const std::string &text,
                 const ImU32 node_color,
                 const ImVec2 corner) {
    const ImVec2 size = MeasureReadout(style, text);
    draw_list->AddRectFilled(corner, corner + size, style.Fill, style.Rounding);
    draw_list->AddRect(corner, corner + size, style.Border, style.Rounding);
    const ImVec2 dot_center = {corner.x + style.Padding.x + style.DotRadius, corner.y + size.y / 2.0f};
    draw_list->AddCircleFilled(dot_center, style.DotRadius, node_color);
    draw_list->AddText(style.Font,
                       style.FontSize,
                       {dot_center.x + style.DotRadius + style.Padding.x, corner.y + style.Padding.y},
                       style.Text,
                       text.c_str());
}

} // namespace

/**
 * @brief   Creates the overlays of the editor.
 * @param[in] glow_renderer  Draws the glows of lit LEDs and hot resistors; it must outlive the overlays.
 * @param[in] playback       Instant of the transient to show, and the window of points that plays; it must outlive the
 *                           overlays.
 * @param[in] preferences    How the schematic is colored; they must outlive the overlays.
 */
SchematicOverlays::SchematicOverlays(GlowRenderer &glow_renderer,
                                     const TransientPlayback &playback,
                                     const Preferences &preferences)
    : m_GlowRenderer(glow_renderer), m_Preferences(preferences), m_Resolver(playback) {
}

/**
 * @brief   Resolves what the schematic shows this frame, and tells each fuse whether it is open and how strong the
 *          flash of its blow still is.
 * @param[in,out] schematic  The schematic being edited, whose parts and nodes the results are shown on, and whose
 *                           fuses are told what they show.
 * @param[in]     results    The results to show; the drawing methods read them too, so they must outlive the frame.
 * @param[in]     controls   Runs of the analyses, which tell which result came last.
 * @note    Call it once per frame, before drawing.
 */
void SchematicOverlays::Update(Schematic &schematic, const ResultStore &results, const AnalysisControls &controls) {
    m_Results = &results;
    m_Resolver.Resolve(schematic, results, controls.GetLastRun());
    const bool is_flash_on = m_Preferences.IsEffectOn(LiveEffect::FUSE);
    const auto &elements = schematic.GetElements();
    const std::vector<std::optional<FuseDisplay>> &fuses = m_Resolver.GetFuseDisplays();
    for (std::size_t index = 0; index < elements.size() && index < fuses.size(); ++index) {
        if (const std::optional<FuseDisplay> &fuse = fuses[index]) {
            static_cast<UIFuse &>(*elements[index])
                .SetDisplayState(fuse->IsBlown, is_flash_on ? fuse->FlashLevel : 0.0f);
        }
    }
}

/**
 * @brief   Shows nothing until the next Update(), as when the editor switches to another schematic.
 */
void SchematicOverlays::Reset() {
    m_Resolver.Reset();
}

/**
 * @brief   What the schematic shows this frame.
 * @return  The values on display, or no value before any operating point or transient.
 */
const std::optional<DisplayedState> &SchematicOverlays::GetDisplayed() const {
    return m_Resolver.GetDisplayed();
}

/**
 * @brief   Draws the wires and their junctions in the colors the preferences ask for.
 * @param[in,out] canvas     Where to draw.
 * @param[in]     view       Pan and zoom of the canvas.
 * @param[in]     schematic  The schematic being edited.
 * @param[in]     zoom       Pixels per grid unit, which sizes the junction dots.
 * @details Voltage, Current and Power need a result on display; without one, wires stay plain and the legend asks for
 *          it. Wires carry no power, so they stay plain by power. Over heat colors a selected wire is marked by a halo
 *          instead of a color of its own, which could pass for a value on the scale.
 */
void SchematicOverlays::DrawWires(SchematicCanvas &canvas,
                                  const ViewTransform &view,
                                  Schematic &schematic,
                                  const float zoom) const {
    const Connectivity &connectivity = schematic.GetConnectivity();
    const auto &wires = schematic.GetWires();
    const std::optional<DisplayedState> &displayed = GetDisplayed();
    const bool drawn_by_current = m_Preferences.WireColors == WireColoring::CURRENT && displayed;
    DrawWireSelectionHalos(canvas, view, schematic);
    if (drawn_by_current) {
        DrawWireCurrents(canvas, view, schematic);
    }

    for (std::size_t index = 0; index < wires.size(); ++index) {
        const UIWire &wire = wires[index];
        const bool selected = schematic.IsWireSelected(index) && !AreWiresHeatColored();
        if (drawn_by_current && !selected) {
            continue;
        }
        // Every wire end is a connection point, so it always has a node
        const int node = connectivity.GetNode(wire.GetStart()).value_or(0);
        wire.Draw(canvas, view, selected ? GetPalette().Selected : GetWireColor(node));
    }

    const float junction_radius = std::max(zoom * JUNCTION_RADIUS_SCALE, MIN_JUNCTION_RADIUS);
    for (const GridPoint junction : connectivity.GetJunctions()) {
        const int node = connectivity.GetNode(junction).value_or(0);
        canvas.AddCircleFilled(view.ToScreen(ToVec2(junction)), junction_radius, GetWireColor(node));
    }

    if (m_Preferences.IsEffectOn(LiveEffect::CURRENT_FLOW) && displayed) {
        DrawLiveCurrent(canvas, view, schematic, displayed->Point, displayed->Scale, ImGui::GetTime());
    }

    if (m_Preferences.WireColors == WireColoring::NODES) {
        DrawNodeNumbers(canvas, view, schematic);
    }
}

/**
 * @brief   Draws the parts, selected or by heat; over heat colors a selected part is outlined, as its color stands for
 *          its power.
 * @param[in,out] canvas            Where to draw.
 * @param[in]     view              Pan and zoom of the canvas.
 * @param[in]     schematic         The schematic being edited.
 * @param[in]     marks_live_parts  Whether to outline the parts the user sets during a live run.
 * @param[in]     lit_live_part     The live part under the cursor or held, outlined more strongly, or none.
 */
void SchematicOverlays::DrawElements(SchematicCanvas &canvas,
                                     const ViewTransform &view,
                                     const Schematic &schematic,
                                     const bool marks_live_parts,
                                     const std::optional<std::size_t> lit_live_part) const {
    const auto &elements = schematic.GetElements();
    for (std::size_t index = 0; index < elements.size(); ++index) {
        elements[index]->Draw(canvas, view, GetElementColor(schematic, index), m_Preferences.Symbols);
        DrawLogicState(canvas, view, *elements[index], index);
        if (schematic.IsElementSelected(index)) {
            if (ArePartsHeatColored()) {
                elements[index]->DrawOutline(canvas, view, GetPalette().Selected);
            }
        } else if (marks_live_parts && GetLiveAction(elements[index]->GetComponent().GetType()) != LiveAction::NONE) {
            const ImU32 alpha = index == lit_live_part ? LIVE_PART_HOVERED_ALPHA : LIVE_PART_OUTLINE_ALPHA;
            elements[index]->DrawOutline(canvas, view, ReplaceAlpha(GetHighlightColor(), alpha));
        }
    }
}

/**
 * @brief   Every lit LED glows in its color, bigger and stronger with its current, and every resistor that dissipates
 *          power glows orange with it.
 * @param[in] draw_list  Where to draw, over the symbols, so a lit LED looks lit through its own lines.
 * @param[in] view       Pan and zoom of the canvas.
 * @param[in] schematic  The schematic being edited.
 * @param[in] zoom       Pixels per grid unit, which sizes the glows.
 * @details Additive light needs a dark background, so light themes blend the glows instead.
 */
void SchematicOverlays::DrawGlows(ImDrawList *draw_list,
                                  const ViewTransform &view,
                                  const Schematic &schematic,
                                  const float zoom) const {
    const std::optional<DisplayedState> &displayed = GetDisplayed();
    const bool leds = m_Preferences.IsEffectOn(LiveEffect::LED_GLOW);
    const bool resistors = m_Preferences.IsEffectOn(LiveEffect::HEAT_GLOW);
    if (!displayed || (!leds && !resistors)) {
        return;
    }
    const bool additive = !GetPalette().Light;
    const float intensity = additive ? ADDITIVE_GLOW_INTENSITY : BLENDED_GLOW_INTENSITY;
    std::vector<Glow> glows;
    const auto add_glow = [&](const GridPoint center,
                              ImVec4 light,
                              const float brightness,
                              const float min_radius,
                              const float max_radius) {
        light.w = intensity * brightness;
        const float radius = min_radius + (max_radius - min_radius) * brightness;
        glows.push_back({view.ToScreen(ToVec2(center)), radius * zoom, ImGui::ColorConvertFloat4ToU32(light)});
    };
    if (leds) {
        for (const LEDGlow &led : ComputeLEDGlows(schematic.GetElements(), displayed->Point.Currents)) {
            add_glow(led.Center, GetLEDLight(led.Color), led.Brightness, GLOW_MIN_RADIUS, GLOW_MAX_RADIUS);
        }
    }
    if (resistors) {
        for (const HeatGlow &resistor :
             ComputeHeatGlows(schematic.GetElements(), displayed->Powers, displayed->Scale)) {
            add_glow(resistor.Center, HEAT_LIGHT, resistor.Brightness, HEAT_GLOW_MIN_RADIUS, HEAT_GLOW_MAX_RADIUS);
        }
    }
    m_GlowRenderer.AddGlows(draw_list, glows, additive);
}

/**
 * @brief   Draws one voltage readout per node beside its first wire or, for terminals joined without wires, beside a
 *          terminal: on a pill of the surface color with a border, led by a dot of the node color, so it reads on any
 *          wire color and theme.
 * @param[in] draw_list  Where to draw.
 * @param[in] view       Pan and zoom of the canvas.
 * @param[in] schematic  The schematic being edited.
 * @param[in] zoom       Pixels per grid unit, which sizes the readouts.
 * @details Ground is always 0 V, so it is left out. The pill sits off the wire, under a level one and right of an
 *          upright one, so it covers neither the wire, its node dot nor the probe name over it. It takes the size of
 *          the part labels, so it follows the zoom, and is left out when zoomed out too far to read.
 */
void SchematicOverlays::DrawNodeVoltages(ImDrawList *draw_list,
                                         const ViewTransform &view,
                                         Schematic &schematic,
                                         const float zoom) const {
    const std::optional<DisplayedState> &displayed = GetDisplayed();
    const std::optional<float> font_size = GetReadoutFontSize(zoom);
    if (!displayed || !font_size) {
        return;
    }
    const std::vector<double> &voltages = displayed->Point.NodeVoltages;
    const ReadoutStyle style = MakeReadoutStyle(*font_size);
    const std::vector<ReadoutAnchor> anchors = ListReadoutAnchors(schematic.GetWires(), schematic.GetElements());
    for (const NodeReadout &readout : ChooseNodeReadouts(anchors, schematic.GetConnectivity(), voltages.size())) {
        const std::string text = std::format("{}V", Core::FormatMeasurement(voltages[readout.Node]));
        const ImVec2 corner = PlaceReadout(view.ToScreen(readout.Anchor.Start),
                                           view.ToScreen(readout.Anchor.End),
                                           MeasureReadout(style, text),
                                           style.Gap);
        DrawReadout(draw_list, style, text, GetNodeColor(static_cast<int>(readout.Node)), corner);
    }
}

/**
 * @brief   Shows, in a tooltip, the value on display of the wire or the part under the cursor, and the power of a part.
 * @param[in] target     What is under the cursor.
 * @param[in] schematic  The schematic being edited, for the names of nets.
 * @note    Nothing shows before a result, or for a value the result lacks.
 */
void SchematicOverlays::DrawHoveredValue(const MeasurementTarget &target, Schematic &schematic) const {
    const std::optional<DisplayedState> &displayed = GetDisplayed();
    if (!displayed) {
        return;
    }
    const std::optional<std::string> text =
        DescribeHoveredValue(target, schematic.GetConnectivity(), schematic.GetElements(), *displayed);
    if (text) {
        ImGui::SetTooltip("%s", text->c_str());
    }
}

/**
 * @brief   Draws a gradient with the values at its ends at the bottom left of the canvas, or a reminder to run an
 *          operating point, a transient or a sensitivity analysis.
 * @param[in] draw_list  Where to draw.
 * @param[in] origin     Top left corner of the canvas, in screen pixels.
 * @param[in] size       Size of the canvas, in pixels.
 * @param[in,out] schematic  Whose nets name the output of the sensitivity coloring.
 * @note    Nothing shows unless the preferences color by voltage, current, power or sensitivity.
 */
void SchematicOverlays::DrawColorLegend(ImDrawList *draw_list,
                                        const ImVec2 origin,
                                        const ImVec2 size,
                                        Schematic &schematic) const {
    if (!HasLegend(m_Preferences.WireColors)) {
        return;
    }
    const float font_size = ImGui::GetFontSize();
    const ImVec2 corner = {origin.x + font_size, origin.y + size.y - font_size * 2.0f};
    const std::optional<LegendLabels> labels = GetLegendLabels(schematic);
    if (!labels) {
        const bool is_sensitivity = m_Preferences.WireColors == WireColoring::SENSITIVITY;
        draw_list->AddText(corner,
                           GetPalette().CanvasText,
                           is_sensitivity
                               ? "Run a sensitivity analysis to color the parts by their influence on its output"
                               : "Run an operating point (.op) or a transient to color by voltage, current or power");
        return;
    }
    const std::string &low_label = labels->Low;
    const std::string &high_label = labels->High;
    if (!labels->Title.empty()) {
        draw_list->AddText({corner.x, corner.y - font_size - 2.0f}, GetPalette().CanvasText, labels->Title.c_str());
    }
    const ImVec2 bar_size = {font_size * LEGEND_WIDTH, font_size * LEGEND_HEIGHT};
    for (int step = 0; step < LEGEND_STEPS; ++step) {
        const float left = corner.x + bar_size.x * static_cast<float>(step) / LEGEND_STEPS;
        const float right = corner.x + bar_size.x * static_cast<float>(step + 1) / LEGEND_STEPS;
        draw_list->AddRectFilled({left, corner.y},
                                 {right, corner.y + bar_size.y},
                                 GetHeatColor((static_cast<float>(step) + 0.5f) / LEGEND_STEPS));
    }
    const float text_y = corner.y + bar_size.y + 2.0f;
    draw_list->AddText({corner.x, text_y}, GetPalette().CanvasText, low_label.c_str());
    const float high_width = ImGui::CalcTextSize(high_label.c_str()).x;
    draw_list->AddText({corner.x + bar_size.x - high_width, text_y}, GetPalette().CanvasText, high_label.c_str());
}

/**
 * @brief   Over heat colors a selected wire has a halo behind it.
 */
void SchematicOverlays::DrawWireSelectionHalos(SchematicCanvas &canvas,
                                               const ViewTransform &view,
                                               const Schematic &schematic) const {
    if (!AreWiresHeatColored()) {
        return;
    }
    for (const std::size_t selected : schematic.GetSelection().Wires) {
        const UIWire &wire = schematic.GetWires()[selected];
        canvas.AddLine(view.ToScreen(ToVec2(wire.GetStart())),
                       view.ToScreen(ToVec2(wire.GetEnd())),
                       ReplaceAlpha(GetPalette().Selected, SELECTION_HALO_ALPHA),
                       LINE_THICKNESS + SELECTION_HALO_EXTRA);
    }
}

/**
 * @brief   Each piece of wire takes the heat color of its own current, so the path of the current shows along the
 *          wires.
 */
void SchematicOverlays::DrawWireCurrents(SchematicCanvas &canvas,
                                         const ViewTransform &view,
                                         const Schematic &schematic) const {
    const std::optional<DisplayedState> &displayed = GetDisplayed();
    if (!displayed) {
        return;
    }
    for (const WireCurrent &piece :
         ComputeWireCurrents(schematic.GetElements(), schematic.GetWires(), displayed->Point.Currents)) {
        UIWire(piece.Start, piece.End)
            .Draw(canvas, view, GetHeatColor(CurrentLevel(piece.Current, displayed->Scale.LargestCurrent)));
    }
}

/**
 * @brief   The number of the node on the middle of each wire.
 */
void SchematicOverlays::DrawNodeNumbers(SchematicCanvas &canvas,
                                        const ViewTransform &view,
                                        Schematic &schematic) const {
    const Connectivity &connectivity = schematic.GetConnectivity();
    for (const UIWire &wire : schematic.GetWires()) {
        const int node = connectivity.GetNode(wire.GetStart()).value_or(0);
        const ImVec2 middle = view.ToScreen((ToVec2(wire.GetStart()) + ToVec2(wire.GetEnd())) / 2.0f);
        canvas.AddText(middle, GetPalette().CanvasText, std::format("{}", node).c_str());
    }
}

/**
 * @brief   The color of the wires of a node: its own by node, its heat color by voltage, otherwise the color of the
 *          theme.
 */
ImU32 SchematicOverlays::GetWireColor(const int node) const {
    if (m_Preferences.WireColors == WireColoring::NODES) {
        return GetNodeColor(node);
    }
    const std::optional<DisplayedState> &displayed = GetDisplayed();
    const bool by_voltage = m_Preferences.WireColors == WireColoring::VOLTAGE && displayed;
    if (by_voltage && static_cast<std::size_t>(node) < displayed->Point.NodeVoltages.size()) {
        const double voltage = displayed->Point.NodeVoltages[static_cast<std::size_t>(node)];
        return GetHeatColor(VoltageLevel(voltage, displayed->Scale));
    }
    return GetPalette().Wire;
}

/**
 * @brief   Selection wins, unless the parts are heat colored, where an outline marks it instead; coloring by current
 *          heats each part by the largest current through its terminals, and coloring by power by the power it absorbs.
 */
ImU32 SchematicOverlays::GetElementColor(const Schematic &schematic, const std::size_t index) const {
    if (schematic.IsElementSelected(index) && !ArePartsHeatColored()) {
        return GetPalette().Selected;
    }
    if (m_Preferences.WireColors == WireColoring::SENSITIVITY) {
        return GetSensitivityColor(schematic, index);
    }
    const std::optional<DisplayedState> &displayed = GetDisplayed();
    const bool by_current = m_Preferences.WireColors == WireColoring::CURRENT;
    if ((!by_current && m_Preferences.WireColors != WireColoring::POWER) || !displayed) {
        return GetPalette().Element;
    }
    const UIElement &element = *schematic.GetElements()[index];
    if (!CarriesCurrentOrPower(element.GetComponent().GetType())) {
        return GetPalette().Element;
    }
    if (!by_current) {
        const auto power = FindPower(displayed->Powers, element.GetComponent().GetName());
        return power ? GetHeatColor(PowerLevel(power->Power, displayed->Scale)) : GetPalette().Element;
    }
    const double current = GetLargestTerminalCurrent(element, displayed->Point.Currents);
    return GetHeatColor(CurrentLevel(current, displayed->Scale.LargestCurrent));
}

/**
 * @brief   A part takes the heat color of its influence on the output of the last sensitivity analysis; a part the
 *          analysis does not list, such as a diode or a ground, keeps the color of the theme.
 */
ImU32 SchematicOverlays::GetSensitivityColor(const Schematic &schematic, const std::size_t index) const {
    const std::string &part_name = schematic.GetElements()[index]->GetComponent().GetName();
    if (const auto &ac_sensitivity = m_Results->GetACSensitivity()) {
        const std::optional<float> level =
            FindACSensitivityLevel(*ac_sensitivity, part_name, m_Results->GetACSensitivityFrequency());
        return level ? GetHeatColor(*level) : GetPalette().Element;
    }
    const auto &sensitivity = m_Results->GetSensitivity();
    if (!sensitivity) {
        return GetPalette().Element;
    }
    const std::optional<float> level = FindSensitivityLevel(*sensitivity, part_name);
    return level ? GetHeatColor(*level) : GetPalette().Element;
}

/**
 * @brief   Shows what a logic output reads, lit when it reads 1 and the Logic Lights effect is on, and the segments of
 *          a 7-segment display that read 1, and lights a logic input that is high with that effect; nothing for the
 *          other parts, nor for a display before there is a result.
 */
void SchematicOverlays::DrawLogicState(SchematicCanvas &canvas,
                                       const ViewTransform &view,
                                       const UIElement &element,
                                       const std::size_t index) const {
    const bool are_lights_on = m_Preferences.IsEffectOn(LiveEffect::LOGIC_LIGHTS);
    if (element.GetComponent().GetType() == Core::ComponentType::LOGIC_INPUT) {
        if (are_lights_on && static_cast<const Core::LogicInput &>(element.GetComponent()).IsHigh()) {
            static_cast<const UILogicInput &>(element).DrawLit(canvas, view, "1");
        }
        return;
    }
    const std::optional<DisplayedState> &displayed = GetDisplayed();
    if (!displayed || index >= displayed->LogicReadings.size() || displayed->LogicReadings[index].empty()) {
        return;
    }
    const std::vector<std::optional<LogicState>> &readings = displayed->LogicReadings[index];
    const ImU32 color = GetPalette().CanvasText;
    if (element.GetComponent().GetType() == Core::ComponentType::SEVEN_SEGMENT) {
        static_cast<const UISevenSegment &>(element).DrawReading(canvas, view, readings, color, are_lights_on);
        return;
    }
    if (!readings.front()) {
        return;
    }
    const LogicState state = *readings.front();
    const bool is_lit = state == LogicState::HIGH && are_lights_on;
    static_cast<const UILogicOutput &>(element).DrawReading(canvas, view, state, color, is_lit);
}

/**
 * @brief   The values at the two ends of the legend, in the unit of what the schematic is colored by.
 * @param[in,out] schematic  Whose nets name the output of the sensitivity coloring.
 * @return  The labels, or no value while there is no result to color by.
 */
std::optional<LegendLabels> SchematicOverlays::GetLegendLabels(Schematic &schematic) const {
    if (m_Preferences.WireColors == WireColoring::SENSITIVITY) {
        return GetSensitivityLegend(*m_Results, schematic.GetConnectivity());
    }
    const std::optional<DisplayedState> &displayed = GetDisplayed();
    if (!displayed) {
        return std::nullopt;
    }
    return GetHeatLegend(m_Preferences.WireColors, displayed->Scale);
}

/**
 * @brief   Voltage and Current color the wires with the heat scale, once there is a result to color them by.
 */
bool SchematicOverlays::AreWiresHeatColored() const {
    return GetDisplayed()
        && (m_Preferences.WireColors == WireColoring::VOLTAGE || m_Preferences.WireColors == WireColoring::CURRENT);
}

/**
 * @brief   Current and Power color the parts with the heat scale, once there is a result to color them by, and
 *          Sensitivity once there is a sensitivity analysis.
 */
bool SchematicOverlays::ArePartsHeatColored() const {
    if (m_Preferences.WireColors == WireColoring::SENSITIVITY) {
        return m_Results->GetSensitivity().has_value() || m_Results->GetACSensitivity().has_value();
    }
    return GetDisplayed()
        && (m_Preferences.WireColors == WireColoring::CURRENT || m_Preferences.WireColors == WireColoring::POWER);
}

} // namespace GUI
