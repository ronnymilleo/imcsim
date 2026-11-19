/**
 * @file    ui_behavioral_source.h
 * @brief   Schematic symbol of a behavioral source, whose voltage or current is an expression.
 */

#ifndef IMCSIM_UI_BEHAVIORAL_SOURCE_H
#define IMCSIM_UI_BEHAVIORAL_SOURCE_H

#include "circuit/components/behavioral_source.h"
#include "ui_elements/ui_dependent_source.h"
#include <string>

namespace GUI {

/**
 * @class   UIBehavioralSource
 * @brief   The diamond of a dependent source, labeled with its expression.
 * @details Terminal 1 (+) is at (0, -2) on top and terminal 2 (-) at (0, 2) below. The name and the expression, as
 *          "V=V(IN)*2" or "I=1m*V(CTL)", are labeled to the right; a long expression is cut with an ellipsis, and the
 *          part editor shows it whole.
 */
class UIBehavioralSource : public UIDependentSource {
public:
    UIBehavioralSource(Core::ComponentType type, GridPoint position, Rotation rotation);
    ~UIBehavioralSource() override = default;

protected:
    void DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const override;

    LocalBounds GetLocalBounds() const override;
    bool SetsVoltage() const override;

private:
    const Core::BehavioralSource &GetSource() const;
};

// Labels
std::string FormatBehavioralLabel(const Core::BehavioralSource &source);

} // namespace GUI

#endif // IMCSIM_UI_BEHAVIORAL_SOURCE_H
