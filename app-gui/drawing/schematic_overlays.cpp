/**
 * @file    schematic_overlays.cpp
 * @brief   What the editor draws over the schematic from a result: heat colors, glows, node voltages and the legend.
 */

#include "drawing/schematic_overlays.h"

#include "analysis/logic_outputs.h"
#include "components/subcircuit.h"
#include "drawing/current_flow.h"
#include "drawing/heat_glow.h"
#include "drawing/led_glow.h"
#include "drawing/node_colors.h"
#include "drawing/schematic_drawing.h"
#include "drawing/theme.h"
#include "editing/live_input.h"
#include "spice/spice_value.h"
#include "ui_elements/ui_logic_output.h"
#include <algorithm>
#include <cmath>
#include <format>
#include <string>
#include <utility>

namespace GUI {

namespace {

/// Junction dot radius relative to the zoom.
constexpr float JUNCTION_RADIUS_SCALE = 0.15f;
/// Smallest junction dot radius, so it stays visible when zoomed out.
constexpr float MIN_JUNCTION_RADIUS = 2.5f;
/// Voltage labels sit just above and right of their point, clear of the line.
constexpr ImVec2 VOLTAGE_LABEL_OFFSET = {4.0f, -16.0f};
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
    state.LogicReadings.resize(elements.size());
    for (std::size_t index = 0; index < elements.size(); ++index) {
        if (elements[index]->GetComponent().GetType() == Core::ComponentType::LOGIC_OUTPUT) {
            state.LogicReadings[index] = ReadLogicOutput(*elements[index], connectivity, state.Point);
        }
    }
    m_Displayed = std::move(state);
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
            if (ArePartsHeatColored()) {
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
 * @brief   Draws one voltage label per node, on its first wire or, for terminals joined without wires, on a terminal.
 * @param[in] draw_list  Where to draw.
 * @param[in] view       Pan and zoom of the canvas.
 * @param[in] schematic  The schematic being edited.
 * @details Ground is always 0 V, so it is left out.
 */
void SchematicOverlays::DrawNodeVoltages(ImDrawList *draw_list, const ViewTransform &view, Schematic &schematic) const {
    if (!m_Displayed) {
        return;
    }
    const Connectivity &connectivity = schematic.GetConnectivity();
    const std::vector<double> &voltages = m_Displayed->Point.NodeVoltages;
    // The theme color of the labels is yellow, which gets lost over the yellow of the heat scale
    const ImU32 label_color = AreWiresHeatColored() ? GetPalette().CanvasText : GetPalette().VoltageLabel;
    std::vector<bool> labeled(voltages.size(), false);
    const auto draw_label = [&](const GridPoint point, const ImVec2 world_pos) {
        const std::optional<int> node = connectivity.GetNode(point);
        if (!node || *node <= 0 || static_cast<std::size_t>(*node) >= voltages.size()) {
            return;
        }
        const auto index = static_cast<std::size_t>(*node);
        if (labeled[index]) {
            return;
        }
        labeled[index] = true;
        const std::string text = std::format("{}V", Core::FormatValue(voltages[index]));
        draw_list->AddText(view.ToScreen(world_pos) + VOLTAGE_LABEL_OFFSET, label_color, text.c_str());
    };

    for (const UIWire &wire : schematic.GetWires()) {
        draw_label(wire.GetStart(), (ToVec2(wire.GetStart()) + ToVec2(wire.GetEnd())) / 2.0f);
    }
    for (const auto &element : schematic.GetElements()) {
        for (const GridPoint terminal : element->GetTerminals()) {
            draw_label(terminal, ToVec2(terminal));
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
    ImGui::SetTooltip("%s", text.c_str());
}

/**
 * @brief   Draws a gradient with the values at its ends at the bottom left of the canvas, or a reminder to run an
 *          operating point or a transient.
 * @param[in] draw_list  Where to draw.
 * @param[in] origin     Top left corner of the canvas, in screen pixels.
 * @param[in] size       Size of the canvas, in pixels.
 * @note    Nothing shows unless the preferences color by voltage, current or power.
 */
void SchematicOverlays::DrawColorLegend(ImDrawList *draw_list, const ImVec2 origin, const ImVec2 size) const {
    if (m_Preferences.WireColors != WireColoring::VOLTAGE && m_Preferences.WireColors != WireColoring::CURRENT &&
        m_Preferences.WireColors != WireColoring::POWER) {
        return;
    }
    const float font_size = ImGui::GetFontSize();
    const ImVec2 corner = {origin.x + font_size, origin.y + size.y - font_size * 2.0f};
    if (!m_Displayed) {
        draw_list->AddText(corner, GetPalette().CanvasText,
                           "Run an operating point (.op) or a transient to color by voltage, current or power");
        return;
    }

    const HeatScale &scale = m_Displayed->Scale;
    std::string low_label;
    std::string high_label;
    if (m_Preferences.WireColors == WireColoring::VOLTAGE) {
        low_label = std::format("{}V", Core::FormatValue(scale.LowestVoltage));
        high_label = std::format("{}V", Core::FormatValue(scale.HighestVoltage));
    } else if (m_Preferences.WireColors == WireColoring::CURRENT) {
        low_label = std::format("<{}A", Core::FormatValue(scale.LargestCurrent * std::pow(10.0, -CURRENT_DECADES)));
        high_label = std::format("{}A", Core::FormatValue(scale.LargestCurrent));
    } else {
        low_label = std::format("<{}W", Core::FormatValue(scale.LargestPower * std::pow(10.0, -POWER_DECADES)));
        high_label = std::format("{}W", Core::FormatValue(scale.LargestPower));
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
    if (schematic.IsElementSelected(index) && !ArePartsHeatColored()) {
        return GetPalette().Selected;
    }
    const bool by_current = m_Preferences.WireColors == WireColoring::CURRENT;
    if ((!by_current && m_Preferences.WireColors != WireColoring::POWER) || !m_Displayed) {
        return GetPalette().Element;
    }
    const UIElement &element = *schematic.GetElements()[index];
    const Core::Component &component = element.GetComponent();
    // Ground, net labels, logic outputs and subcircuit pin markers carry no current or power of their own
    const Core::ComponentType type = component.GetType();
    if (type == Core::ComponentType::GROUND || type == Core::ComponentType::NET_LABEL ||
        type == Core::ComponentType::LOGIC_OUTPUT || Core::IsSubcircuitPin(type)) {
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
 * @brief   Shows what a logic output reads, lit when it reads 1 and its live effect is on; nothing for the other parts
 *          and before there is a result.
 */
void SchematicOverlays::DrawLogicReading(SchematicCanvas &canvas, const ViewTransform &view, const UIElement &element,
                                         const std::size_t index) const {
    if (!m_Displayed || index >= m_Displayed->LogicReadings.size()) {
        return;
    }
    const std::optional<LogicState> reading = m_Displayed->LogicReadings[index];
    if (!reading) {
        return;
    }
    const LogicState state = *reading;
    const bool is_lit = state == LogicState::HIGH && m_Preferences.IsEffectOn(LiveEffect::LOGIC_LIGHTS);
    static_cast<const UILogicOutput &>(element).DrawReading(canvas, view, state, GetPalette().CanvasText, is_lit);
}

/**
 * @brief   Voltage and Current color the wires with the heat scale, once there is a result to color them by.
 */
bool SchematicOverlays::AreWiresHeatColored() const {
    return m_Displayed &&
           (m_Preferences.WireColors == WireColoring::VOLTAGE || m_Preferences.WireColors == WireColoring::CURRENT);
}

/**
 * @brief   Current and Power color the parts with the heat scale, once there is a result to color them by.
 */
bool SchematicOverlays::ArePartsHeatColored() const {
    return m_Displayed &&
           (m_Preferences.WireColors == WireColoring::CURRENT || m_Preferences.WireColors == WireColoring::POWER);
}

} // namespace GUI
