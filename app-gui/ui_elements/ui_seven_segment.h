/**
 * @file    ui_seven_segment.h
 * @brief   Schematic symbol of a 7-segment display that lights the segments whose inputs read 1.
 */

#ifndef IMCSIM_UI_SEVEN_SEGMENT_H
#define IMCSIM_UI_SEVEN_SEGMENT_H

#include "base/logic_state.h"
#include "ui_elements/ui_element.h"
#include <array>
#include <optional>
#include <span>

namespace GUI {

/**
 * @class   UISevenSegment
 * @brief   A box with the inputs a to g down its left edge and the seven segments of a digit drawn inside it.
 * @details The symbol alone shows the segments as outlines; the result shown fills those that read 1, through
 *          DrawReading().
 */
class UISevenSegment : public UIElement {
public:
    UISevenSegment(GridPoint position, Rotation rotation);
    ~UISevenSegment() override = default;

    void DrawReading(SchematicCanvas &canvas, const ViewTransform &view,
                     std::span<const std::optional<LogicState>> segments, ImU32 color, bool are_lights_on) const;

protected:
    void DrawTerminals(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const override;
    void DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color, SymbolStyle style) const override;
    void DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const override;

    std::vector<GridPoint> GetLocalTerminals() const override;
    GridPoint GetLocalTerminalInward(std::size_t terminal) const override;
    LocalBounds GetLocalBounds() const override;

private:
    void FillSegment(SchematicCanvas &canvas, const ViewTransform &view, std::size_t segment, ImU32 color) const;
    void OutlineSegment(SchematicCanvas &canvas, const ViewTransform &view, std::size_t segment, ImU32 color,
                        float thickness) const;
    std::array<ImVec2, 6> GetSegmentOnScreen(const ViewTransform &view, std::size_t segment) const;
};

} // namespace GUI

#endif // IMCSIM_UI_SEVEN_SEGMENT_H
