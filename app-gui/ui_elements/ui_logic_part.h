/**
 * @file    ui_logic_part.h
 * @brief   Schematic symbol of a logic gate and of a flip-flop.
 */

#ifndef IMCSIM_UI_LOGIC_PART_H
#define IMCSIM_UI_LOGIC_PART_H

#include "ui_element.h"

namespace GUI {

/**
 * @class   UILogicPart
 * @brief   A gate drawn with its ANSI shape or as an IEC box with a qualifier, and a flip-flop drawn as a box with
 *          its inputs on the left, its outputs on the right and SET and RESET above and below.
 * @details A two-input gate has A at (-3, -1) and B at (-3, 1), the output at (3, 0); NOT and the buffer have A at
 *          (-3, 0). A gate with more inputs keeps those two where they are and puts C at (-3, 3), D at (-3, 5) and so
 *          on, two units apart, with the output at the middle of the body, (3, n - 2) for n inputs. The body grows
 *          downward, so adding an input leaves the wired inputs in place and only moves the output. A D or T flip-flop
 * has its data input at (-3, -1), CLK at (-3, 1), Q at (3, -1) and QN at (3, 1), SET at (0, -3) and RESET at (0, 3); a
 * JK or SR flip-flop has its two data inputs at (-3, -2) and
 *          (-3, 2), CLK at (-3, 0), Q at (3, -2), QN at (3, 2), SET at (0, -4) and RESET at (0, 4).
 */
class UILogicPart : public UIElement {
public:
    UILogicPart(Core::ComponentType type, GridPoint position, Rotation rotation);
    ~UILogicPart() override = default;

protected:
    void DrawTerminals(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const override;
    void DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color, SymbolStyle style) const override;
    void DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const override;

    std::vector<GridPoint> GetLocalTerminals() const override;
    GridPoint GetLocalTerminalInward(std::size_t terminal) const override;
    LocalBounds GetLocalBounds() const override;

private:
    bool IsFlipFlop() const;
    float GetGateHalfHeight() const;
    float GetGateCenterY() const;
    void DrawGateTerminals(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const;
    void DrawFlipFlopTerminals(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const;
    void DrawAnsiGate(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const;
    void DrawIECGate(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const;
    void DrawFlipFlop(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const;
    void DrawBubble(SchematicCanvas &canvas, const ViewTransform &view, float center_x, ImU32 color) const;
};

} // namespace GUI

#endif // IMCSIM_UI_LOGIC_PART_H
