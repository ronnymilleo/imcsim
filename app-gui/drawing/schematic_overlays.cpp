/**
 * @file    schematic_overlays.cpp
 * @brief   What the editor draws over the schematic from a result: heat colors, glows, node voltages and the legend.
 */

#include "drawing/schematic_overlays.h"

#include "analysis/fuse_readings.h"
#include "analysis/logic_outputs.h"
#include "analysis/sensitivity_view.h"
#include "components/logic_input.h"
#include "components/subcircuit.h"
#include "drawing/current_flow.h"
#include "drawing/fuse_flash.h"
#include "drawing/heat_glow.h"
#include "drawing/led_glow.h"
#include "drawing/node_colors.h"
#include "drawing/readout_placement.h"
#include "drawing/schematic_drawing.h"
#include "drawing/theme.h"
#include "editing/live_input.h"
#include "spice/spice_value.h"
#include "ui_elements/ui_fuse.h"
#include "ui_elements/ui_logic_input.h"
#include "ui_elements/ui_logic_output.h"
#include "ui_elements/ui_seven_segment.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <format>
#include <optional>
#include <string>
#include <utility>

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
/// How often the heat scale of a transient that is still being made is worked out again, in seconds.
constexpr double SCALE_REFRESH_SECONDS = 0.5;
/// A selected wire keeps its heat color and gets a halo behind it this much wider, in pixels.
constexpr float SELECTION_HALO_EXTRA = 6.0f;
/// The halo is partly see-through.
constexpr ImU32 SELECTION_HALO_ALPHA = 150;
/// During a live run the parts the user can set are outlined faintly, so they stand out from the parts whose edits end
/// the run without hiding the heat colors.
constexpr ImU32 LIVE_PART_OUTLINE_ALPHA = 110;
/// The part under the cursor or held is outlined fully.
constexpr ImU32 LIVE_PART_HOVERED_ALPHA = 255;

ImU32 WithAlpha(const ImU32 color, const ImU32 alpha) {
    return (color & ~IM_COL32_A_MASK) | (alpha << IM_COL32_A_SHIFT);
}

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
 * @brief   Power a part absorbs, looked up by the name of the part; a current name such as "Q1.C" belongs to part "Q1".
 */
std::optional<ComponentPower> FindPower(const std::vector<ComponentPower> &powers, const std::string &name) {
    const std::string part = name.substr(0, name.find('.'));
    const auto found = std::ranges::find(powers, part, &ComponentPower::Name);
    return found != powers.end() ? std::optional(*found) : std::nullopt;
}

} // namespace

/**
 * @brief   Creates the overlays of the editor.
 * @param[in] glow_renderer  Draws the glows of lit LEDs and hot resistors; it must outlive the overlays.
 * @param[in] playback       Instant of the transient to show, and the window of points that plays; it must outlive the
 *                           overlays.
 * @param[in] preferences    How the schematic is colored; they must outlive the overlays.
 */
SchematicOverlays::SchematicOverlays(GlowRenderer &glow_renderer, const TransientPlayback &playback,
                                     const Preferences &preferences)
    : m_GlowRenderer(glow_renderer), m_Playback(playback), m_Preferences(preferences) {
}

/**
 * @brief   Resolves what the schematic shows this frame: the last operating point, unless a transient was run after it,
 *          then the transient at the instant of the playback, on a scale that covers the window that plays, so a value
 *          keeps its color while the time moves.
 * @param[in] schematic  The schematic being edited, whose results are shown.
 * @param[in] controls   Runs of the analyses, which tell which result came last.
 * @note    Call it once per frame, before drawing.
 */
void SchematicOverlays::Update(Schematic &schematic, const AnalysisControls &controls) {
    m_Displayed.reset();
    const auto &operating_point = schematic.GetOperatingPoint();
    const auto &transient = schematic.GetTransient();
    const bool show_transient = transient && !transient->Times.empty() &&
                                (!operating_point || controls.GetLastRun() != Analysis::OPERATING_POINT);
    UpdateFuses(schematic, show_transient ? &*transient : nullptr,
                show_transient ? std::optional(m_Playback.Time) : std::nullopt);
    if (!show_transient && !operating_point) {
        return;
    }
    const auto &elements = schematic.GetElements();
    const Connectivity &connectivity = schematic.GetConnectivity();
    DisplayedState state;
    if (show_transient) {
        // The scale goes over the window that plays, so a window of any size costs the same; while chunks keep arriving
        // it is refreshed a few times a second only
        const PointWindow window = m_Playback.Window.GetWindow();
        const bool is_stale = m_ScaledTransientVersion != schematic.GetTransientDataVersion() ||
                              m_ScaledWindow.First != window.First || m_ScaledWindow.Count != window.Count;
        const bool may_refresh = transient->IsComplete || !m_ScaledTransientVersion.has_value() ||
                                 ImGui::GetTime() - m_LastScaleTime >= SCALE_REFRESH_SECONDS;
        if (is_stale && may_refresh) {
            m_TransientScale = ScaleOf(SliceTransient(*transient, window), elements, connectivity);
            m_ScaledTransientVersion = schematic.GetTransientDataVersion();
            m_ScaledWindow = window;
            m_LastScaleTime = ImGui::GetTime();
        }
        state.Point = SampleTransient(*transient, m_Playback.Time);
        state.Scale = m_TransientScale;
        state.Time = m_Playback.Time;
    } else {
        state.Point = *operating_point;
        state.Scale = ScaleOf(state.Point, elements, connectivity);
    }
    state.Powers = ComputePowers(elements, connectivity, state.Point);
    state.FuseBlowTimes = m_FuseBlowTimes;
    state.LogicReadings.resize(elements.size());
    for (std::size_t index = 0; index < elements.size(); ++index) {
        state.LogicReadings[index] = ReadLogicDisplay(*elements[index], connectivity, state.Point);
    }
    m_Displayed = std::move(state);
}

/**
 * @brief   Tells each fuse whether the result on display shows it open, and how strong the flash of its blow still is.
 * @param[in,out] schematic  The schematic being edited, whose fuses are told.
 * @param[in]     shown      The transient on display, or none when the schematic shows an operating point or nothing,
 *                           which see every fuse whole.
 * @param[in]     instant    Instant of the transient on display.
 * @details The flash starts when a fuse is seen going from whole to open between two frames, so a result that arrives
 *          with a fuse already blown does not flash, and it lasts FUSE_FLASH_SECONDS of the screen clock, whatever the
 *          speed of the playback.
 */
void SchematicOverlays::UpdateFuses(Schematic &schematic, const Core::Transient *shown,
                                    const std::optional<double> instant) {
    const auto &elements = schematic.GetElements();
    if (shown == nullptr) {
        m_FuseBlowTimes.clear();
        m_FuseBlowTimesVersion.reset();
    } else if (m_FuseBlowTimesVersion != schematic.GetTransientDataVersion() ||
               m_FuseBlowTimes.size() != elements.size()) {
        m_FuseBlowTimes = FindFuseBlowTimes(elements, schematic.GetConnectivity(), *shown);
        m_FuseBlowTimesVersion = schematic.GetTransientDataVersion();
    }
    const bool has_previous_frame = shown != nullptr && m_WasFuseBlown.size() == elements.size();
    std::vector<bool> is_blown(elements.size(), false);
    m_FuseBlownSince.resize(elements.size(), 0.0);
    const bool is_flash_on = m_Preferences.IsEffectOn(LiveEffect::FUSE);
    const double now = ImGui::GetTime();
    for (std::size_t index = 0; index < elements.size(); ++index) {
        if (elements[index]->GetComponent().GetType() != Core::ComponentType::FUSE) {
            continue;
        }
        is_blown[index] = shown != nullptr && IsFuseBlownAt(m_FuseBlowTimes[index], instant);
        if (is_blown[index] && has_previous_frame && !m_WasFuseBlown[index]) {
            m_FuseBlownSince[index] = now;
        }
        const float flash_level =
            is_flash_on && is_blown[index] ? GetFuseFlashLevel(now - m_FuseBlownSince[index]) : 0.0f;
        static_cast<UIFuse &>(*elements[index]).SetDisplayState(is_blown[index], flash_level);
    }
    m_WasFuseBlown = shown != nullptr ? std::move(is_blown) : std::vector<bool>();
}

/**
 * @brief   Shows nothing until the next Update(), as when the editor switches to another schematic.
 */
void SchematicOverlays::Reset() {
    m_Displayed.reset();
}

/**
 * @brief   What the schematic shows this frame.
 * @return  The values on display, or no value before any operating point or transient.
 */
const std::optional<DisplayedState> &SchematicOverlays::GetDisplayed() const {
    return m_Displayed;
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
void SchematicOverlays::DrawWires(SchematicCanvas &canvas, const ViewTransform &view, Schematic &schematic,
                                  const float zoom) const {
    const Connectivity &connectivity = schematic.GetConnectivity();
    const auto &wires = schematic.GetWires();
    const bool drawn_by_current = m_Preferences.WireColors == WireColoring::CURRENT && m_Displayed;
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

    if (m_Preferences.IsEffectOn(LiveEffect::CURRENT_FLOW) && m_Displayed) {
        DrawLiveCurrent(canvas, view, schematic, m_Displayed->Point, m_Displayed->Scale, ImGui::GetTime());
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
void SchematicOverlays::DrawElements(SchematicCanvas &canvas, const ViewTransform &view, const Schematic &schematic,
                                     const bool marks_live_parts,
                                     const std::optional<std::size_t> lit_live_part) const {
    const auto &elements = schematic.GetElements();
    for (std::size_t index = 0; index < elements.size(); ++index) {
        elements[index]->Draw(canvas, view, GetElementColor(schematic, index), m_Preferences.Symbols);
        DrawLogicReading(canvas, view, *elements[index], index);
        if (schematic.IsElementSelected(index)) {
            if (ArePartsHeatColored(schematic)) {
                elements[index]->DrawOutline(canvas, view, GetPalette().Selected);
            }
        } else if (marks_live_parts && GetLiveAction(elements[index]->GetComponent().GetType()) != LiveAction::NONE) {
            const ImU32 alpha = index == lit_live_part ? LIVE_PART_HOVERED_ALPHA : LIVE_PART_OUTLINE_ALPHA;
            elements[index]->DrawOutline(canvas, view, WithAlpha(GetHighlightColor(), alpha));
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
void SchematicOverlays::DrawGlows(ImDrawList *draw_list, const ViewTransform &view, const Schematic &schematic,
                                  const float zoom) const {
    const bool leds = m_Preferences.IsEffectOn(LiveEffect::LED_GLOW);
    const bool resistors = m_Preferences.IsEffectOn(LiveEffect::HEAT_GLOW);
    if (!m_Displayed || (!leds && !resistors)) {
        return;
    }
    const bool additive = !GetPalette().Light;
    const float intensity = additive ? ADDITIVE_GLOW_INTENSITY : BLENDED_GLOW_INTENSITY;
    std::vector<Glow> glows;
    const auto add_glow = [&](const GridPoint center, ImVec4 light, const float brightness, const float min_radius,
                              const float max_radius) {
        light.w = intensity * brightness;
        const float radius = min_radius + (max_radius - min_radius) * brightness;
        glows.push_back({view.ToScreen(ToVec2(center)), radius * zoom, ImGui::ColorConvertFloat4ToU32(light)});
    };
    if (leds) {
        for (const LEDGlow &led : ComputeLEDGlows(schematic.GetElements(), m_Displayed->Point.Currents)) {
            add_glow(led.Center, GetLEDLight(led.Color), led.Brightness, GLOW_MIN_RADIUS, GLOW_MAX_RADIUS);
        }
    }
    if (resistors) {
        for (const HeatGlow &resistor :
             ComputeHeatGlows(schematic.GetElements(), m_Displayed->Powers, m_Displayed->Scale)) {
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
 * @details Ground is always 0 V, so it is left out. The pill sits off the wire, under a level one and right of an
 *          upright one, so it covers neither the wire, its node dot nor the probe name over it. It takes the size of
 * the part labels, so it follows the zoom, and is left out when zoomed out too far to read.
 */
void SchematicOverlays::DrawNodeVoltages(ImDrawList *draw_list, const ViewTransform &view, Schematic &schematic) const {
    if (!m_Displayed) {
        return;
    }
    const Connectivity &connectivity = schematic.GetConnectivity();
    const std::vector<double> &voltages = m_Displayed->Point.NodeVoltages;
    const ThemePalette &palette = GetPalette();
    const ImU32 fill = ImGui::ColorConvertFloat4ToU32(palette.Surface);
    const ImU32 border = ImGui::ColorConvertFloat4ToU32(palette.Border);
    const float zoom = view.ToScreen({1.0f, 0.0f}).x - view.ToScreen({0.0f, 0.0f}).x;
    const std::optional<float> readout_font_size = GetReadoutFontSize(zoom);
    if (!readout_font_size) {
        return;
    }
    const float font_size = *readout_font_size;
    ImFont *font = ImGui::GetFont();
    const ImVec2 padding = {font_size * READOUT_PADDING_X, font_size * READOUT_PADDING_Y};
    const float dot_radius = font_size * READOUT_DOT_RADIUS;
    const float gap = font_size * READOUT_GAP;
    // The corners shrink with the pill, so a small one stays a pill and not a circle
    const float rounding = ImGui::GetStyle().FrameRounding * font_size / ImGui::GetFontSize();
    std::vector<bool> labeled(voltages.size(), false);
    const auto draw_readout = [&](const GridPoint point, const ImVec2 world_start, const ImVec2 world_end) {
        const std::optional<int> node = connectivity.GetNode(point);
        if (!node || *node <= 0 || static_cast<std::size_t>(*node) >= voltages.size()) {
            return;
        }
        const auto index = static_cast<std::size_t>(*node);
        if (labeled[index]) {
            return;
        }
        labeled[index] = true;
        const std::string text = std::format("{}V", Core::FormatMeasurement(voltages[index]));
        const ImVec2 text_size = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, text.c_str());
        const ImVec2 size = {padding.x * 3.0f + dot_radius * 2.0f + text_size.x, padding.y * 2.0f + text_size.y};
        const ImVec2 corner = PlaceReadout(view.ToScreen(world_start), view.ToScreen(world_end), size, gap);
        draw_list->AddRectFilled(corner, corner + size, fill, rounding);
        draw_list->AddRect(corner, corner + size, border, rounding);
        const ImVec2 dot_center = {corner.x + padding.x + dot_radius, corner.y + size.y / 2.0f};
        draw_list->AddCircleFilled(dot_center, dot_radius, GetNodeColor(*node));
        draw_list->AddText(font, font_size, {dot_center.x + dot_radius + padding.x, corner.y + padding.y},
                           palette.VoltageLabel, text.c_str());
    };

    for (const UIWire &wire : schematic.GetWires()) {
        draw_readout(wire.GetStart(), ToVec2(wire.GetStart()), ToVec2(wire.GetEnd()));
    }
    for (const auto &element : schematic.GetElements()) {
        for (const GridPoint terminal : element->GetTerminals()) {
            draw_readout(terminal, ToVec2(terminal), ToVec2(terminal));
        }
    }
}

/**
 * @brief   Shows, in a tooltip, the value on display of the wire or the part under the cursor, and the power of a part.
 * @param[in] target     What is under the cursor.
 * @param[in] schematic  The schematic being edited, for the names of nets.
 * @note    Nothing shows before a result, or for a value the result lacks.
 */
void SchematicOverlays::DrawHoveredValue(const MeasurementTarget &target, Schematic &schematic) const {
    if (!m_Displayed || !FindMeasuredValue(target, m_Displayed->Point)) {
        return;
    }
    std::string text = DescribeMeasurement(target, schematic.GetConnectivity(), &m_Displayed->Point);
    if (!target.Node) {
        if (const std::optional<ComponentPower> power = FindPower(m_Displayed->Powers, target.Current)) {
            text += std::format("\nP({}) = {}W", power->Name, Core::FormatValue(power->Power));
        }
    }
    if (!target.Node) {
        const auto &elements = schematic.GetElements();
        for (std::size_t index = 0; index < elements.size() && index < m_Displayed->FuseBlowTimes.size(); ++index) {
            const std::optional<double> blow_time = m_Displayed->FuseBlowTimes[index];
            if (blow_time && elements[index]->GetComponent().GetName() == target.Current &&
                IsFuseBlownAt(blow_time, m_Displayed->Time)) {
                text += std::format("\n{} blew at t = {}s", target.Current, Core::FormatValue(*blow_time));
            }
        }
    }
    ImGui::SetTooltip("%s", text.c_str());
}

/**
 * @brief   Draws a gradient with the values at its ends at the bottom left of the canvas, or a reminder to run an
 *          operating point, a transient or a sensitivity analysis.
 * @param[in] draw_list  Where to draw.
 * @param[in] origin     Top left corner of the canvas, in screen pixels.
 * @param[in] size       Size of the canvas, in pixels.
 * @param[in,out] schematic  Whose sensitivity result the legend of the Sensitivity coloring reads, and whose nets name
 *                          its output.
 * @note    Nothing shows unless the preferences color by voltage, current, power or sensitivity.
 */
void SchematicOverlays::DrawColorLegend(ImDrawList *draw_list, const ImVec2 origin, const ImVec2 size,
                                        Schematic &schematic) const {
    if (m_Preferences.WireColors != WireColoring::VOLTAGE && m_Preferences.WireColors != WireColoring::CURRENT &&
        m_Preferences.WireColors != WireColoring::POWER && m_Preferences.WireColors != WireColoring::SENSITIVITY) {
        return;
    }
    const float font_size = ImGui::GetFontSize();
    const ImVec2 corner = {origin.x + font_size, origin.y + size.y - font_size * 2.0f};
    const std::optional<LegendLabels> labels = GetLegendLabels(schematic);
    if (!labels) {
        const bool is_sensitivity = m_Preferences.WireColors == WireColoring::SENSITIVITY;
        draw_list->AddText(corner, GetPalette().CanvasText,
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
        draw_list->AddRectFilled({left, corner.y}, {right, corner.y + bar_size.y},
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
void SchematicOverlays::DrawWireSelectionHalos(SchematicCanvas &canvas, const ViewTransform &view,
                                               const Schematic &schematic) const {
    if (!AreWiresHeatColored()) {
        return;
    }
    for (const std::size_t selected : schematic.GetSelectedWires()) {
        const UIWire &wire = schematic.GetWires()[selected];
        canvas.AddLine(view.ToScreen(ToVec2(wire.GetStart())), view.ToScreen(ToVec2(wire.GetEnd())),
                       WithAlpha(GetPalette().Selected, SELECTION_HALO_ALPHA), LINE_THICKNESS + SELECTION_HALO_EXTRA);
    }
}

/**
 * @brief   Each piece of wire takes the heat color of its own current, so the path of the current shows along the
 *          wires.
 */
void SchematicOverlays::DrawWireCurrents(SchematicCanvas &canvas, const ViewTransform &view,
                                         const Schematic &schematic) const {
    if (!m_Displayed) {
        return;
    }
    for (const WireCurrent &piece :
         ComputeWireCurrents(schematic.GetElements(), schematic.GetWires(), m_Displayed->Point.Currents)) {
        UIWire(piece.Start, piece.End)
            .Draw(canvas, view, GetHeatColor(CurrentLevel(piece.Current, m_Displayed->Scale.LargestCurrent)));
    }
}

/**
 * @brief   The number of the node on the middle of each wire.
 */
void SchematicOverlays::DrawNodeNumbers(SchematicCanvas &canvas, const ViewTransform &view,
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
    const bool by_voltage = m_Preferences.WireColors == WireColoring::VOLTAGE && m_Displayed;
    if (by_voltage && static_cast<std::size_t>(node) < m_Displayed->Point.NodeVoltages.size()) {
        const double voltage = m_Displayed->Point.NodeVoltages[static_cast<std::size_t>(node)];
        return GetHeatColor(VoltageLevel(voltage, m_Displayed->Scale));
    }
    return GetPalette().Wire;
}

/**
 * @brief   Selection wins, unless the parts are heat colored, where an outline marks it instead; coloring by current
 *          heats each part by the largest current through its terminals, and coloring by power by the power it absorbs.
 */
ImU32 SchematicOverlays::GetElementColor(const Schematic &schematic, const std::size_t index) const {
    if (schematic.IsElementSelected(index) && !ArePartsHeatColored(schematic)) {
        return GetPalette().Selected;
    }
    if (m_Preferences.WireColors == WireColoring::SENSITIVITY) {
        return GetSensitivityColor(schematic, index);
    }
    const bool by_current = m_Preferences.WireColors == WireColoring::CURRENT;
    if ((!by_current && m_Preferences.WireColors != WireColoring::POWER) || !m_Displayed) {
        return GetPalette().Element;
    }
    const UIElement &element = *schematic.GetElements()[index];
    const Core::Component &component = element.GetComponent();
    // Ground, net labels, logic displays and subcircuit pin markers carry no current or power of their own
    const Core::ComponentType type = component.GetType();
    if (type == Core::ComponentType::GROUND || type == Core::ComponentType::NET_LABEL || Core::IsLogicDisplay(type) ||
        Core::IsSubcircuitPin(type)) {
        return GetPalette().Element;
    }
    if (!by_current) {
        const auto power = FindPower(m_Displayed->Powers, component.GetName());
        return power ? GetHeatColor(PowerLevel(power->Power, m_Displayed->Scale)) : GetPalette().Element;
    }
    double current = 0.0;
    for (std::size_t terminal = 0; terminal < element.GetTerminals().size(); ++terminal) {
        current = std::max(
            current, std::abs(GetTerminalCurrent(component, terminal, m_Displayed->Point.Currents).value_or(0.0)));
    }
    return GetHeatColor(CurrentLevel(current, m_Displayed->Scale.LargestCurrent));
}

/**
 * @brief   A part takes the heat color of its influence on the output of the last sensitivity analysis; a part the
 *          analysis does not list, such as a diode or a ground, keeps the color of the theme.
 */
ImU32 SchematicOverlays::GetSensitivityColor(const Schematic &schematic, const std::size_t index) const {
    const auto &sensitivity = schematic.GetSensitivity();
    if (!sensitivity) {
        return GetPalette().Element;
    }
    const std::optional<float> level =
        FindSensitivityLevel(*sensitivity, schematic.GetElements()[index]->GetComponent().GetName());
    return level ? GetHeatColor(*level) : GetPalette().Element;
}

/**
 * @brief   Shows what a logic output reads, lit when it reads 1 and the Logic Lights effect is on, and the segments of
 *          a 7-segment display that read 1, and lights a logic input that is high with that effect; nothing for the
 *          other parts, nor for a display before there is a result.
 */
void SchematicOverlays::DrawLogicReading(SchematicCanvas &canvas, const ViewTransform &view, const UIElement &element,
                                         const std::size_t index) const {
    const bool are_lights_on = m_Preferences.IsEffectOn(LiveEffect::LOGIC_LIGHTS);
    if (element.GetComponent().GetType() == Core::ComponentType::LOGIC_INPUT) {
        if (are_lights_on && static_cast<const Core::LogicInput &>(element.GetComponent()).IsHigh()) {
            static_cast<const UILogicInput &>(element).DrawLit(canvas, view, "1");
        }
        return;
    }
    if (!m_Displayed || index >= m_Displayed->LogicReadings.size() || m_Displayed->LogicReadings[index].empty()) {
        return;
    }
    const std::vector<std::optional<LogicState>> &readings = m_Displayed->LogicReadings[index];
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
 * @param[in,out] schematic  Whose sensitivity result the Sensitivity coloring reads, and whose nets name its output.
 * @return  The labels, or no value while there is no result to color by.
 */
std::optional<SchematicOverlays::LegendLabels> SchematicOverlays::GetLegendLabels(Schematic &schematic) const {
    if (m_Preferences.WireColors == WireColoring::SENSITIVITY) {
        const auto &sensitivity = schematic.GetSensitivity();
        if (!sensitivity) {
            return std::nullopt;
        }
        // The legend reads the change for 1 % of the value, as the table does
        const double largest_per_percent = GetLargestSensitivity(*sensitivity) / SENSITIVITY_PERCENT;
        return LegendLabels{
            .Low = std::format("<{}V", Core::FormatValue(largest_per_percent * std::pow(10.0, -SENSITIVITY_DECADES))),
            .High = std::format("{}V", Core::FormatValue(largest_per_percent)),
            .Title = std::format("Change of {} per 1 %",
                                 GetVoltageLabel(schematic.GetConnectivity(), sensitivity->OutputNode))};
    }
    if (!m_Displayed) {
        return std::nullopt;
    }
    const HeatScale &scale = m_Displayed->Scale;
    if (m_Preferences.WireColors == WireColoring::VOLTAGE) {
        return LegendLabels{.Low = std::format("{}V", Core::FormatValue(scale.LowestVoltage)),
                            .High = std::format("{}V", Core::FormatValue(scale.HighestVoltage))};
    }
    if (m_Preferences.WireColors == WireColoring::CURRENT) {
        return LegendLabels{
            .Low = std::format("<{}A", Core::FormatValue(scale.LargestCurrent * std::pow(10.0, -CURRENT_DECADES))),
            .High = std::format("{}A", Core::FormatValue(scale.LargestCurrent))};
    }
    return LegendLabels{.Low =
                            std::format("<{}W", Core::FormatValue(scale.LargestPower * std::pow(10.0, -POWER_DECADES))),
                        .High = std::format("{}W", Core::FormatValue(scale.LargestPower))};
}

/**
 * @brief   Voltage and Current color the wires with the heat scale, once there is a result to color them by.
 */
bool SchematicOverlays::AreWiresHeatColored() const {
    return m_Displayed &&
           (m_Preferences.WireColors == WireColoring::VOLTAGE || m_Preferences.WireColors == WireColoring::CURRENT);
}

/**
 * @brief   Current and Power color the parts with the heat scale, once there is a result to color them by, and
 *          Sensitivity once there is a sensitivity analysis.
 */
bool SchematicOverlays::ArePartsHeatColored(const Schematic &schematic) const {
    if (m_Preferences.WireColors == WireColoring::SENSITIVITY) {
        return schematic.GetSensitivity().has_value();
    }
    return m_Displayed &&
           (m_Preferences.WireColors == WireColoring::CURRENT || m_Preferences.WireColors == WireColoring::POWER);
}

} // namespace GUI
