/**
 * @file    ui_logic_output.h
 * @brief   Schematic symbol of a logic output that shows the logic level of its node.
 */

#ifndef IMCSIM_UI_LOGIC_OUTPUT_H
#define IMCSIM_UI_LOGIC_OUTPUT_H

#include "plots/logic_trace.h"
#include "ui_elements/ui_logic_box.h"

namespace GUI {

/**
 * @class   UILogicOutput
 * @brief   A box on a lead with an arrow into it, which shows 1, 0 or X once there is a result to read.
 * @details The box is the one of the logic input, so the two read as a pair; the arrow on the lead tells that the
 *          signal comes in. The symbol alone is the empty box: the reading comes from the result shown, through
 *          DrawReading().
 */
class UILogicOutput : public UILogicBox {
public:
    UILogicOutput(GridPoint position, Rotation rotation);
    ~UILogicOutput() override = default;

    void DrawReading(SchematicCanvas &canvas, const ViewTransform &view, LogicState state, ImU32 color,
                     bool is_lit) const;

protected:
    void DrawTerminals(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const override;
    void DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color, SymbolStyle style) const override;
};

} // namespace GUI

#endif // IMCSIM_UI_LOGIC_OUTPUT_H
