/**
 * @file    ui_diac.cpp
 * @brief   Schematic symbol of a DIAC.
 */

#include "ui_elements/ui_diac.h"

#include "circuit/components/diac.h"
#include "spice/spice_value.h"
#include <format>
#include <memory>

namespace GUI {

namespace {

/// The bars stand at x = -0.5 and 0.5, as tall as a diode's, with the triangles between them.
constexpr float BODY_HALF_LENGTH = 0.5f;
constexpr float BODY_HALF_HEIGHT = 0.6f;
/// The two anti-parallel triangles, their tips this far off the axis and their bases this tall, as on a TRIAC.
constexpr float TIP_OFFSET = 0.25f;
constexpr float BASE_HALF_HEIGHT = 0.35f;
/// Labels sit just outside the symbol.
constexpr float LABEL_OFFSET = 1.0f;

} // namespace

/**
 * @brief   Creates a DIAC part placed on the grid, together with its simulation component.
 * @param[in] position  Grid position in world units.
 * @param[in] rotation  Orientation on the grid.
 */
UIDiac::UIDiac(const GridPoint position, const Rotation rotation)
    : UIElement(std::make_unique<Core::Diac>(), position, rotation) {
}

/**
 * @brief   The MT2 and MT1 leads run along the axis to the bars.
 */
void UIDiac::DrawTerminals(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    canvas.AddLine(LocalToScreen(view, -2, 0), LocalToScreen(view, -1, 0), color, LINE_THICKNESS);
    canvas.AddLine(LocalToScreen(view, 1, 0), LocalToScreen(view, 2, 0), color, LINE_THICKNESS);
}

void UIDiac::DrawSymbol(SchematicCanvas &canvas,
                        const ViewTransform &view,
                        const ImU32 color,
                        SymbolStyle /*style*/) const {
    canvas.AddLine(LocalToScreen(view, -1, 0), LocalToScreen(view, -BODY_HALF_LENGTH, 0), color, LINE_THICKNESS);
    canvas.AddLine(LocalToScreen(view, BODY_HALF_LENGTH, 0), LocalToScreen(view, 1, 0), color, LINE_THICKNESS);
    // The upper triangle conducts MT2 to MT1 and the lower MT1 to MT2, each tip on the opposite bar
    canvas.AddTriangle(LocalToScreen(view, -BODY_HALF_LENGTH, -TIP_OFFSET - BASE_HALF_HEIGHT),
                       LocalToScreen(view, -BODY_HALF_LENGTH, -TIP_OFFSET + BASE_HALF_HEIGHT),
                       LocalToScreen(view, BODY_HALF_LENGTH, -TIP_OFFSET),
                       color,
                       LINE_THICKNESS);
    canvas.AddTriangle(LocalToScreen(view, BODY_HALF_LENGTH, TIP_OFFSET - BASE_HALF_HEIGHT),
                       LocalToScreen(view, BODY_HALF_LENGTH, TIP_OFFSET + BASE_HALF_HEIGHT),
                       LocalToScreen(view, -BODY_HALF_LENGTH, TIP_OFFSET),
                       color,
                       LINE_THICKNESS);
    for (const float bar_x : {-BODY_HALF_LENGTH, BODY_HALF_LENGTH}) {
        canvas.AddLine(LocalToScreen(view, bar_x, -BODY_HALF_HEIGHT),
                       LocalToScreen(view, bar_x, BODY_HALF_HEIGHT),
                       color,
                       LINE_THICKNESS);
    }
}

/**
 * @brief   Draws the DIAC name above the symbol and its breakover voltage below, in local orientation.
 * @param[in,out] canvas Where it is drawn: the editor window or an exported image.
 * @param[in] view       Transform of the current frame.
 * @param[in] color      Text color.
 */
void UIDiac::DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    const auto &diac = static_cast<const Core::Diac &>(GetComponent());
    DrawLabel(canvas, view, {0.0f, -LABEL_OFFSET}, {0.0f, -1.0f}, diac.GetName(), color);
    DrawLabel(canvas,
              view,
              {0.0f, LABEL_OFFSET},
              {0.0f, 1.0f},
              std::format("VBO {}V", Core::FormatValue(diac.GetParameters().BreakoverVoltage)),
              color);
}

/**
 * @brief   Returns the terminals in component order, in local grid units before rotation.
 * @return  MT2 at (-2, 0) and MT1 at (2, 0).
 */
std::vector<GridPoint> UIDiac::GetLocalTerminals() const {
    return {{-2, 0}, {2, 0}};
}

/**
 * @brief   Returns the pick area of the part, in local grid units before rotation.
 * @return  The box from lead to lead around the bars.
 */
LocalBounds UIDiac::GetLocalBounds() const {
    return {{-2.0f, -0.8f}, {2.0f, 0.8f}};
}

} // namespace GUI
