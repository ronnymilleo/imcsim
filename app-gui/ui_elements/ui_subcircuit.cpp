/**
 * @file    ui_subcircuit.cpp
 * @brief   Rectangular symbol of a project-local subcircuit instance.
 */

#include "ui_elements/ui_subcircuit.h"

#include "components/subcircuit.h"
#include <algorithm>
#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace GUI {

namespace {

/// Pins sit two units apart, so their labels have room.
constexpr int PIN_SPACING = 2;
/// Terminals sit one unit outside the box.
constexpr int LEAD_LENGTH = 1;
/// The smallest box is four units wide.
constexpr int MIN_HALF_WIDTH = 2;
/// The smallest box is two units high.
constexpr int MIN_HALF_HEIGHT = 1;
constexpr int VERTICAL_NAME_ROOM = 1;
/// Pin names sit just inside the box.
constexpr float PIN_LABEL_INSET = 0.2f;
constexpr float NAME_LABEL_GAP = 0.5f;

const Core::SubcircuitInstance &Instance(const Core::Component &component) {
    return static_cast<const Core::SubcircuitInstance &>(component);
}

int CountPins(const Core::SubcircuitInstance &instance, const Core::PinSide side) {
    return static_cast<int>(std::ranges::count_if(
        instance.GetPins(), [&](const std::string &pin) { return instance.GetPinSide(pin) == side; }));
}

/// The half sizes of the box, in whole units so the terminals land on the grid: tall enough for the pins of the fuller
/// of the left and right edges, wide enough for those of the top and bottom.
struct BoxSize {
    int HalfWidth = MIN_HALF_WIDTH;
    int HalfHeight = MIN_HALF_HEIGHT;
    bool HasVerticalPins = false;
};

BoxSize MeasureBox(const Core::SubcircuitInstance &instance) {
    const int rows = std::max(CountPins(instance, Core::PinSide::LEFT), CountPins(instance, Core::PinSide::RIGHT));
    const int columns = std::max(CountPins(instance, Core::PinSide::TOP), CountPins(instance, Core::PinSide::BOTTOM));
    // With pins on the sides and on top or below, a unit more of height keeps the names of the two apart
    const int name_room = rows > 0 && columns > 0 ? VERTICAL_NAME_ROOM : 0;
    return {.HalfWidth = std::max(MIN_HALF_WIDTH, columns * PIN_SPACING / 2),
            .HalfHeight = std::max(MIN_HALF_HEIGHT, rows * PIN_SPACING / 2) + name_room,
            .HasVerticalPins = columns > 0};
}

/**
 * @brief   Where along its edge the pin of an index goes, the pins of the edge centered on it.
 */
int PinOffset(const int index, const int edge_count) {
    return PIN_SPACING * index - PIN_SPACING * (edge_count - 1) / 2;
}

/**
 * @brief   The point on the box where the lead of a terminal meets it, one unit in from the terminal.
 */
ImVec2 GetEdgePoint(const GridPoint terminal, const BoxSize box) {
    if (terminal.X < -box.HalfWidth) {
        return {static_cast<float>(-box.HalfWidth), static_cast<float>(terminal.Y)};
    }
    if (terminal.X > box.HalfWidth) {
        return {static_cast<float>(box.HalfWidth), static_cast<float>(terminal.Y)};
    }
    return {static_cast<float>(terminal.X), static_cast<float>(terminal.Y < 0 ? -box.HalfHeight : box.HalfHeight)};
}

} // namespace

/**
 * @brief   Creates the symbol of an instance of no definition yet, with no pins.
 * @param[in] position  Grid point of the middle of the body.
 * @param[in] rotation  Orientation of the symbol.
 */
UISubcircuit::UISubcircuit(const GridPoint position, const Rotation rotation)
    : UIElement(std::make_unique<Core::SubcircuitInstance>(), position, rotation) {
}

/**
 * @brief   The grid points that the instance name above the box and the block name below it cover, so routed wires
 *          keep off them.
 * @return  World points of the row just past each end of the box, leads included, across the width of the box; the
 *          point just outside each terminal is left out, so a wire can always reach it. A name wider than the box
 *          goes past the points, as its width depends on the font.
 */
std::vector<GridPoint> UISubcircuit::GetNameLabelPoints() const {
    const LocalBounds bounds = GetLocalBounds();
    const int half_width = MeasureBox(Instance(GetComponent())).HalfWidth;
    const auto half_height = static_cast<int>(bounds.Max.y);
    const std::vector<GridPoint> terminals = GetLocalTerminals();
    std::vector<GridPoint> ways_out;
    for (std::size_t terminal = 0; terminal < terminals.size(); ++terminal) {
        ways_out.push_back(terminals[terminal] - GetLocalTerminalInward(terminal));
    }
    std::vector<GridPoint> points;
    // The names are drawn half a unit past the bounds and are shorter than a unit, so each covers one row
    for (const int row : {-half_height - 1, half_height + 1}) {
        for (int column = -half_width; column <= half_width; ++column) {
            GridPoint point = {column, row};
            if (std::ranges::contains(ways_out, point)) {
                continue;
            }
            if (IsMirrored()) {
                point.X = -point.X;
            }
            points.push_back(GetPosition() + Rotate(point, GetRotation()));
        }
    }
    return points;
}

void UISubcircuit::DrawTerminals(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    const BoxSize box = MeasureBox(Instance(GetComponent()));
    for (const GridPoint pin : GetLocalTerminals()) {
        const ImVec2 edge = GetEdgePoint(pin, box);
        canvas.AddLine(LocalToScreen(view, static_cast<float>(pin.X), static_cast<float>(pin.Y)),
                       LocalToScreen(view, edge.x, edge.y), color, LINE_THICKNESS);
    }
}

void UISubcircuit::DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color,
                              const SymbolStyle) const {
    const BoxSize box = MeasureBox(Instance(GetComponent()));
    const auto half_width = static_cast<float>(box.HalfWidth);
    const auto half_height = static_cast<float>(box.HalfHeight);
    const std::array corners = {
        LocalToScreen(view, -half_width, -half_height), LocalToScreen(view, half_width, -half_height),
        LocalToScreen(view, half_width, half_height), LocalToScreen(view, -half_width, half_height)};
    canvas.AddPolyline(corners.data(), static_cast<int>(corners.size()), color, ImDrawFlags_Closed, LINE_THICKNESS);
}

/**
 * @brief   The instance name above, the definition below, and each pin name inside the box next to its lead.
 */
void UISubcircuit::DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    const auto &instance = Instance(GetComponent());
    const LocalBounds bounds = GetLocalBounds();
    DrawLabel(canvas, view, {0.0f, bounds.Min.y - NAME_LABEL_GAP}, {0.0f, -1.0f}, GetComponent().GetName(), color);
    DrawLabel(canvas, view, {0.0f, bounds.Max.y + NAME_LABEL_GAP}, {0.0f, 1.0f}, instance.GetDefinitionName(), color);
    const BoxSize box = MeasureBox(instance);
    const std::vector<GridPoint> terminals = GetLocalTerminals();
    for (std::size_t pin = 0; pin < terminals.size(); ++pin) {
        const ImVec2 edge = GetEdgePoint(terminals[pin], box);
        // From the edge toward the middle of the box
        const ImVec2 inward = {edge.x - static_cast<float>(terminals[pin].X),
                               edge.y - static_cast<float>(terminals[pin].Y)};
        DrawLabel(canvas, view, {edge.x + inward.x * PIN_LABEL_INSET, edge.y + inward.y * PIN_LABEL_INSET}, inward,
                  instance.GetPins()[pin], color);
    }
}

/**
 * @brief   One terminal per pin, in pin order, on the edge its marker gave it; the pins of each edge are centered on it
 *          in the order they come.
 */
std::vector<GridPoint> UISubcircuit::GetLocalTerminals() const {
    const auto &instance = Instance(GetComponent());
    const BoxSize box = MeasureBox(instance);
    std::map<Core::PinSide, int> placed;
    std::vector<GridPoint> terminals;
    for (const std::string &pin : instance.GetPins()) {
        const Core::PinSide side = instance.GetPinSide(pin);
        const int offset = PinOffset(placed[side]++, CountPins(instance, side));
        switch (side) {
        case Core::PinSide::RIGHT:
            terminals.push_back({box.HalfWidth + LEAD_LENGTH, offset});
            break;
        case Core::PinSide::TOP:
            terminals.push_back({offset, -box.HalfHeight - LEAD_LENGTH});
            break;
        case Core::PinSide::BOTTOM:
            terminals.push_back({offset, box.HalfHeight + LEAD_LENGTH});
            break;
        case Core::PinSide::LEFT:
        default:
            terminals.push_back({-box.HalfWidth - LEAD_LENGTH, offset});
            break;
        }
    }
    return terminals;
}

LocalBounds UISubcircuit::GetLocalBounds() const {
    const BoxSize box = MeasureBox(Instance(GetComponent()));
    const auto half_width = static_cast<float>(box.HalfWidth + LEAD_LENGTH);
    const auto half_height = static_cast<float>(box.HalfHeight + (box.HasVerticalPins ? LEAD_LENGTH : 0));
    return {{-half_width, -half_height}, {half_width, half_height}};
}

} // namespace GUI
