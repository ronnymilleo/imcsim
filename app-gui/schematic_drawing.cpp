/**
 * @file    schematic_drawing.cpp
 * @brief   Schematic drawing shared by the editor and the SVG export: colors, probe markers and the export itself.
 */

#include "schematic_drawing.h"

#include "current_flow.h"
#include "node_colors.h"
#include "probing.h"
#include "svg_writer.h"
#include "theme.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <format>
#include <set>
#include <vector>

namespace GUI {

namespace {

// Space between a measurement marker and the near edge of its label, in pixels
constexpr float MARKER_LABEL_GAP = 4.0f;
// Voltage dots follow the zoom like the rest of the drawing: 4 pixels at the default zoom
constexpr float VOLTAGE_DOT_SCALE = 0.2f;
constexpr float MIN_VOLTAGE_DOT_RADIUS = 2.5f;
// Current arrows lie on the lead, in grid units from the terminal: from the base of the head to its tip
constexpr float ARROW_START = 0.2f;
constexpr float ARROW_TIP = 0.75f;
constexpr float ARROW_HALF_WIDTH = 0.25f;
// Flowing-current arrows: equal heads spaced this far apart along the wires, in grid units, so they follow the zoom.
// The arrows only show direction, so they all have the same size and scroll at one speed; the magnitude stays on the
// heat color. The speed is in grid units per second
constexpr float FLOW_ARROW_SPACING = 0.60f;
constexpr float FLOW_ARROW_LENGTH = 0.20f;
constexpr float FLOW_ARROW_HALF_WIDTH = 0.10f;
constexpr float FLOW_SPEED = 1.5f;
// Terminal numbers are smaller than part labels and muted, so they do not compete with names and values. They sit
// just outside the terminal, in grid units along the line of its lead, where neither the symbol nor probes draw
constexpr float TERMINAL_NUMBER_SCALE = 0.8f;
constexpr float TERMINAL_NUMBER_OUTSIDE = 0.3f;
constexpr float TERMINAL_NUMBER_GAP = 3.0f;
// Each numbered terminal also gets a small open ring, the usual mark of a connection point; open so it does not read
// as a junction or a voltage probe, which are filled dots. Exported images are usually viewed enlarged, so their ring
// is smaller than the one on screen, relative to the zoom
constexpr float EXPORT_TERMINAL_RING_SCALE = 0.125f;
constexpr float MIN_TERMINAL_RING_RADIUS = 2.5f;
constexpr float TERMINAL_RING_THICKNESS = 1.5f;

// The export draws at the default zoom of the editor, so it looks like the circuit on screen; the image is vector,
// so it scales from there
constexpr float EXPORT_ZOOM = 20.0f;
// Room around the circuit, in pixels
constexpr float EXPORT_MARGIN = 16.0f;
// Junction dots as the editor draws them at the default zoom
constexpr float JUNCTION_RADIUS = 3.0f;
// Symbols and wires are black on white for print; probes keep the colors of their traces
constexpr ImU32 PRINT_LINE_COLOR = IM_COL32(30, 30, 30, 255);
constexpr ExportColor PRINT_BACKGROUND = {255, 255, 255};

ExportColor ToExportColor(const ImU32 color) {
    return {static_cast<std::uint8_t>((color >> IM_COL32_R_SHIFT) & 0xFF),
            static_cast<std::uint8_t>((color >> IM_COL32_G_SHIFT) & 0xFF),
            static_cast<std::uint8_t>((color >> IM_COL32_B_SHIFT) & 0xFF)};
}

// Wires, junctions, parts with their labels, terminal numbers and probes, at the export zoom from the origin
void DrawWholeSchematic(SvgCanvas &canvas, Schematic &schematic, const SymbolStyle style, const bool terminal_numbers,
                        const ImU32 element_color, const ImU32 wire_color) {
    const ViewTransform view({0.0f, 0.0f}, {0.0f, 0.0f}, EXPORT_ZOOM);
    for (const UIWire &wire : schematic.GetWires()) {
        wire.Draw(canvas, view, wire_color);
    }
    for (const GridPoint junction : schematic.GetConnectivity().GetJunctions()) {
        canvas.AddCircleFilled(view.ToScreen(ToVec2(junction)), JUNCTION_RADIUS, wire_color);
    }
    for (const auto &element : schematic.GetElements()) {
        element->Draw(canvas, view, element_color, style);
    }
    if (terminal_numbers) {
        DrawTerminalNumbers(canvas, view, schematic, EXPORT_TERMINAL_RING_SCALE);
    }
    DrawMeasurementMarkers(canvas, view, schematic);
}

// Pixels per grid unit
float GetZoom(const ViewTransform &view) {
    return view.ToScreen({1.0f, 0.0f}).x - view.ToScreen({0.0f, 0.0f}).x;
}

// Labels take the size of the part labels, so they follow the zoom
float GetLabelFontSize(const ViewTransform &view) {
    return std::max(GetZoom(view) * LABEL_SCALE, MIN_LABEL_SIZE);
}

// Top left corner of a text of the given size beside a line, centered on a point of it: above or left of the line
// before it, below or right after it
ImVec2 PlaceBesideLine(const ImVec2 point, const ImVec2 size, const bool horizontal_line, const bool before_line,
                       const float gap) {
    if (horizontal_line) {
        return {point.x - size.x / 2.0f, before_line ? point.y - gap - size.y : point.y + gap};
    }
    return {before_line ? point.x - gap - size.x : point.x + gap, point.y - size.y / 2.0f};
}

// Marker labels sit beside the line their marker is on, centered on the marker, so they never run along it into a
// corner. Voltages go above or left of their line and currents below or right, so a voltage marked next to a
// measured terminal does not cover the current label. The marker extent is how far the marker reaches from the line
void DrawMarkerLabel(SchematicCanvas &canvas, const ViewTransform &view, const ImVec2 point, const bool horizontal_line,
                     const bool before_line, const float marker_extent, const std::string &text, const ImU32 color) {
    const float font_size = GetLabelFontSize(view);
    ImFont *font = ImGui::GetFont();
    const ImVec2 size = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, text.c_str());
    const ImVec2 position =
        PlaceBesideLine(point, size, horizontal_line, before_line, marker_extent + MARKER_LABEL_GAP);
    canvas.AddText(font, font_size, position, color, text.c_str());
}

} // namespace

/**
 * @brief   Marks what the plots measure: a dot on the first wire of each measured node and, for each measured
 *          current, an arrow on the lead of its terminal pointing into the part, which is the direction the current
 *          flows when its trace is positive. Each marker is labeled and in the color of its plot.
 * @param[in,out] canvas    Where the markers are drawn: the editor window or an exported image.
 * @param[in] view          Transform from world units to the canvas.
 * @param[in] schematic     Schematic whose measurements are marked.
 * @note    Needs an ImPlot context, since current colors come from its colormap.
 */
void DrawMeasurementMarkers(SchematicCanvas &canvas, const ViewTransform &view, Schematic &schematic) {
    const float zoom = GetZoom(view);
    const float dot_radius = std::max(zoom * VOLTAGE_DOT_SCALE, MIN_VOLTAGE_DOT_RADIUS);
    const Connectivity &connectivity = schematic.GetConnectivity();
    std::set<int> marked_nodes;
    for (const UIWire &wire : schematic.GetWires()) {
        const int node = connectivity.GetNode(wire.GetStart()).value_or(0);
        if (node == 0 || !schematic.IsVoltageMeasured(node) || !marked_nodes.insert(node).second) {
            continue;
        }
        const ImVec2 point = view.ToScreen((ToVec2(wire.GetStart()) + ToVec2(wire.GetEnd())) / 2.0f);
        canvas.AddCircleFilled(point, dot_radius, GetNodeColor(node));
        DrawMarkerLabel(canvas, view, point, wire.GetStart().Y == wire.GetEnd().Y, true, dot_radius,
                        GetVoltageLabel(connectivity, node), GetNodeColor(node));
    }

    // Currents keep the colors of their plots, which follow their order in the circuit
    std::size_t current_index = 0;
    for (const auto &element : schematic.GetElements()) {
        const std::vector<std::string> currents = Core::GetCurrentNames(element->GetComponent());
        const std::vector<std::size_t> current_terminals = Core::GetCurrentTerminals(element->GetComponent());
        const std::vector<GridPoint> terminals = element->GetTerminals();
        for (std::size_t index = 0; index < currents.size(); ++index, ++current_index) {
            if (!schematic.IsCurrentMeasured(currents[index])) {
                continue;
            }
            // Results report a current as positive into a terminal: the first one of a two-terminal part or a
            // supply, and its own terminal for each current of a transistor or another part with probes
            const ImVec2 terminal = ToVec2(terminals[current_terminals[index]]);
            const ImVec2 inward = ToVec2(element->GetTerminalInward(current_terminals[index]));
            const ImVec2 across = {-inward.y, inward.x};
            const ImVec2 base = terminal + inward * ARROW_START;
            const ImU32 color = GetCurrentColor(current_index);
            canvas.AddTriangleFilled(view.ToScreen(terminal + inward * ARROW_TIP),
                                     view.ToScreen(base + across * ARROW_HALF_WIDTH),
                                     view.ToScreen(base - across * ARROW_HALF_WIDTH), color);
            const ImVec2 middle = view.ToScreen(terminal + inward * ((ARROW_START + ARROW_TIP) / 2.0f));
            DrawMarkerLabel(canvas, view, middle, inward.y == 0.0f, false, zoom * ARROW_HALF_WIDTH,
                            std::format("I({})", currents[index]), color);
        }
    }
}

/**
 * @brief   Marks the terminals of every part with more than one with a small ring and numbers them, in the order
 *          the netlist lists them.
 * @param[in,out] canvas    Where the numbers are drawn: the editor window or an exported image.
 * @param[in] view          Transform from world units to the canvas.
 * @param[in] schematic     Schematic whose parts are numbered.
 * @param[in] ring_scale    Radius of the terminal rings relative to the zoom (pixels per grid unit).
 * @note    Terminal 1 of a two-terminal part is the one a positive current enters. Each number sits just outside
 *          its terminal, above or left of the line of the lead: inside, symbols draw signs and arrows beside their
 *          leads, and probes draw current arrows on them.
 */
void DrawTerminalNumbers(SchematicCanvas &canvas, const ViewTransform &view, const Schematic &schematic,
                         const float ring_scale) {
    const float font_size = GetLabelFontSize(view) * TERMINAL_NUMBER_SCALE;
    const float ring_radius = std::max(GetZoom(view) * ring_scale, MIN_TERMINAL_RING_RADIUS);
    ImFont *font = ImGui::GetFont();
    const ImU32 color = ImGui::ColorConvertFloat4ToU32(GetPalette().TextMuted);
    for (const auto &element : schematic.GetElements()) {
        const std::vector<GridPoint> terminals = element->GetTerminals();
        if (terminals.size() < 2) {
            continue;
        }
        for (std::size_t index = 0; index < terminals.size(); ++index) {
            const ImVec2 inward = ToVec2(element->GetTerminalInward(index));
            canvas.AddCircle(view.ToScreen(ToVec2(terminals[index])), ring_radius, color, 0, TERMINAL_RING_THICKNESS);
            const ImVec2 outside = view.ToScreen(ToVec2(terminals[index]) - inward * TERMINAL_NUMBER_OUTSIDE);
            const std::string text = std::to_string(index + 1);
            const ImVec2 size = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, text.c_str());
            const ImVec2 position = PlaceBesideLine(outside, size, inward.y == 0.0f, true, TERMINAL_NUMBER_GAP);
            canvas.AddText(font, font_size, position, color, text.c_str());
        }
    }
}

/**
 * @brief   Draws the live current as arrows flowing along every piece of wire, in the direction the current goes.
 * @param[in,out] canvas     Where the arrows are drawn: the editor window or an exported image.
 * @param[in] view           Transform from world units to the canvas.
 * @param[in] schematic      Schematic whose parts and wires carry the currents.
 * @param[in] operating_point  The currents to show: an operating point, or a transient read at one instant.
 * @param[in] scale          Heat scale the colors of the arrows follow.
 * @param[in] time           Seconds since an arbitrary start; the arrows scroll with it, so a caller that does not
 *                           animate, such as an export, passes a fixed value.
 * @details Arrows are spaced along each wire and scroll with time; they all look alike, since they only show the
 *          direction, and their color follows the heat scale of the current. Their positions are anchored to the
 *          grid, so arrows line up across collinear pieces instead of jumping where wires join. Pieces whose
 *          current is below the heat scale, down to none at all, are left out: they are noise next to the rest, and
 *          in a transient their direction would flicker.
 */
void DrawLiveCurrent(SchematicCanvas &canvas, const ViewTransform &view, const Schematic &schematic,
                     const Core::OperatingPoint &operating_point, const HeatScale &scale, const double time) {
    const double largest = scale.LargestCurrent;
    if (largest <= 0.0) {
        return;
    }
    for (const WireCurrent &piece :
         ComputeWireCurrents(schematic.GetElements(), schematic.GetWires(), operating_point.Currents)) {
        const ImVec2 start = ToVec2(piece.Start);
        const ImVec2 end = ToVec2(piece.End);
        const ImVec2 delta = end - start;
        const float length = std::sqrt(delta.x * delta.x + delta.y * delta.y);
        const float level = CurrentLevel(piece.Current, largest);
        if (level < 0.0f || length < FLOW_ARROW_LENGTH) {
            continue;
        }
        // Unit vector along the wire, turned to point the way the current goes
        const ImVec2 along = delta / length;
        const ImVec2 flow = piece.Current >= 0.0 ? along : -along;
        const ImVec2 across = {-flow.y, flow.x};
        // Projection of a point onto the flow direction: an arrow sits wherever it is the scrolling phase plus a
        // multiple of the spacing, so the arrows stay aligned to the grid across the whole schematic. Both ends are
        // projected so the arrows stay on the wire whether the current runs with its start-to-end order or against it
        const float phase = std::fmod(static_cast<float>(time) * FLOW_SPEED, FLOW_ARROW_SPACING);
        const float start_projection = start.x * flow.x + start.y * flow.y;
        const float end_projection = end.x * flow.x + end.y * flow.y;
        const float low = std::min(start_projection, end_projection) + FLOW_ARROW_LENGTH * 0.5f;
        const float high = std::max(start_projection, end_projection) - FLOW_ARROW_LENGTH * 0.5f;
        const ImU32 color = GetHeatColor(std::min(level, 1.0f));
        for (int arrow = static_cast<int>(std::ceil((low - phase) / FLOW_ARROW_SPACING));
             arrow <= static_cast<int>(std::floor((high - phase) / FLOW_ARROW_SPACING)); ++arrow) {
            const float distance = phase + static_cast<float>(arrow) * FLOW_ARROW_SPACING - start_projection;
            const ImVec2 center = start + flow * distance;
            const ImVec2 tip = center + flow * (FLOW_ARROW_LENGTH * 0.5f);
            const ImVec2 back = center - flow * (FLOW_ARROW_LENGTH * 0.5f);
            canvas.AddTriangleFilled(view.ToScreen(tip), view.ToScreen(back + across * FLOW_ARROW_HALF_WIDTH),
                                     view.ToScreen(back - across * FLOW_ARROW_HALF_WIDTH), color);
        }
    }
}

/**
 * @brief   Measures the area a schematic covers when drawn, so a view can frame its labels too.
 * @param[in] schematic         Schematic to measure.
 * @param[in] style             Drawing standard of the symbols, as the editor shows them.
 * @param[in] terminal_numbers  Whether the terminal numbers are shown.
 * @return  The area in grid units, or no value for an empty schematic.
 * @note    Labels scale with the zoom, so the area holds at any zoom where they are drawn. Needs ImGui and ImPlot
 *          contexts, as RenderSchematicSvg() does.
 */
std::optional<SchematicBounds> MeasureSchematic(Schematic &schematic, const SymbolStyle style,
                                                const bool terminal_numbers) {
    if (schematic.GetElements().empty() && schematic.GetWires().empty()) {
        return std::nullopt;
    }
    SvgWriter svg;
    SvgCanvas canvas(svg, false);
    DrawWholeSchematic(canvas, schematic, style, terminal_numbers, IM_COL32_WHITE, IM_COL32_WHITE);
    return SchematicBounds{.Min = canvas.GetMin() / EXPORT_ZOOM, .Max = canvas.GetMax() / EXPORT_ZOOM};
}

/**
 * @brief   Draws the schematic as an SVG image, to go next to its exported plots.
 * @param[in] schematic Schematic to draw: parts with their names and values, wires, junctions and probes.
 * @param[in] style     Drawing standard of the symbols, as the editor shows them.
 * @param[in] terminal_numbers  Numbers the terminals of each part, as the editor does when asked.
 * @param[in] dark      Uses the colors of the editor in the current theme; otherwise black on white for print, with the
 * probes darkened the same way as the traces of exported plots, so both figures keep matching colors.
 * @return  The SVG document, cropped to the circuit, or the reason it cannot be drawn.
 * @note    Needs ImGui and ImPlot contexts: labels are measured with the ImGui font, and current colors come from
 *          the ImPlot colormap. The grid, selection and operating point colors of the editor are left out.
 */
std::expected<std::string, std::string> RenderSchematicSvg(Schematic &schematic, const SymbolStyle style,
                                                           const bool terminal_numbers, const bool dark) {
    if (schematic.GetElements().empty() && schematic.GetWires().empty()) {
        return std::unexpected("The schematic is empty");
    }
    const ThemePalette &palette = GetPalette();
    SvgWriter svg;
    SvgCanvas canvas(svg, !dark);
    DrawWholeSchematic(canvas, schematic, style, terminal_numbers, dark ? palette.Element : PRINT_LINE_COLOR,
                       dark ? palette.Wire : PRINT_LINE_COLOR);

    const ImVec2 min = canvas.GetMin() - ImVec2(EXPORT_MARGIN, EXPORT_MARGIN);
    const ImVec2 max = canvas.GetMax() + ImVec2(EXPORT_MARGIN, EXPORT_MARGIN);
    return svg.Finish({min.x, min.y}, max.x - min.x, max.y - min.y,
                      dark ? ToExportColor(palette.CanvasBackground) : PRINT_BACKGROUND);
}

} // namespace GUI
