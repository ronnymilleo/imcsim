/**
 * @file    ui_behavioral_source.h
 * @brief   Schematic symbol of a behavioral source, whose voltage or current is an expression.
 */

#ifndef IMCSIM_UI_BEHAVIORAL_SOURCE_H
#define IMCSIM_UI_BEHAVIORAL_SOURCE_H

#include "components/behavioral_source.h"
#include "ui_elements/ui_element.h"

namespace GUI {

/**
 * @class   UIBehavioralSource
 * @brief   The diamond of a dependent source, with + and - inside when it sets a voltage and an arrow when it sets a
 *          current, labeled with its expression.
 * @details Terminal 1 (+) is at (0, -2) on top and terminal 2 (-) at (0, 2) below. The name and the expression, as
 *          "V=V(IN)*2" or "I=1m*V(CTL)", are labeled to the right; a long expression is cut with an ellipsis, and the
 *          part editor shows it whole.
 */
class UIBehavioralSource : public UIElement {
public:
    UIBehavioralSource(Core::ComponentType type, GridPoint position, Rotation rotation);
    ~UIBehavioralSource() override = default;

protected:
    void DrawTerminals(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const override;
    void DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color, SymbolStyle style) const override;
    void DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const override;

    std::vector<GridPoint> GetLocalTerminals() const override;
    LocalBounds GetLocalBounds() const override;

private:
    const Core::BehavioralSource &GetSource() const;
    void DrawSign(SchematicCanvas &canvas, const ViewTransform &view, ImVec2 center, bool plus, ImU32 color) const;
};

// Labels
std::string FormatBehavioralLabel(const Core::BehavioralSource &source);

} // namespace GUI

#endif // IMCSIM_UI_BEHAVIORAL_SOURCE_H
