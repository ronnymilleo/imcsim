/**
 * @file    ui_gate.h
 * @brief   Schematic symbol of a logic gate.
 */

#ifndef IMCSIM_UI_GATE_H
#define IMCSIM_UI_GATE_H

#include "ui_elements/ui_element.h"
#include <cstddef>
#include <vector>

namespace GUI {

/**
 * @class   UIGate
 * @brief   A gate drawn with its ANSI shape or as an IEC box with a qualifier.
 * @details A two-input gate has A at (-3, -1) and B at (-3, 1), the output at (3, 0); NOT and the buffer have A at
 *          (-3, 0). A gate with more inputs keeps those two where they are and puts C at (-3, 3), D at (-3, 5) and so
 *          on, two units apart, with the output at the middle of the body, (3, n - 2) for n inputs. The body grows
 *          downward, so adding an input leaves the wired inputs in place and only moves the output.
 */
class UIGate : public UIElement {
public:
    UIGate(Core::ComponentType type, GridPoint position, Rotation rotation);
    ~UIGate() override = default;

protected:
    void DrawTerminals(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const override;
    void DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color, SymbolStyle style) const override;
    void DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const override;

    std::vector<GridPoint> GetLocalTerminals() const override;
    GridPoint GetLocalTerminalInward(std::size_t terminal) const override;
    LocalBounds GetLocalBounds() const override;

private:
    /// Where the body of an ANSI gate is, in local grid units.
    struct GateBody {
        float Tip = 0.0f; ///< The x where the body ends and the output lead starts.
        float HalfHeight = 0.0f;
        float Center = 0.0f; ///< The y of the middle of the body, where the output is.
    };

    // Layout
    float GetGateHalfHeight() const;
    float GetGateCenterY() const;
    GateBody GetAnsiBody() const;

    // ANSI shapes
    void DrawAnsiGate(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const;
    void DrawBufferShape(SchematicCanvas &canvas, const ViewTransform &view, const GateBody &body, ImU32 color) const;
    void DrawAndShape(SchematicCanvas &canvas, const ViewTransform &view, const GateBody &body, ImU32 color) const;
    void DrawOrShape(SchematicCanvas &canvas, const ViewTransform &view, const GateBody &body, ImU32 color) const;
    void
    DrawXorSecondCurve(SchematicCanvas &canvas, const ViewTransform &view, const GateBody &body, ImU32 color) const;

    // IEC box and the bubble of both
    void DrawIECGate(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const;
    void DrawBubble(SchematicCanvas &canvas, const ViewTransform &view, float center_x, ImU32 color) const;
};

} // namespace GUI

#endif // IMCSIM_UI_GATE_H
