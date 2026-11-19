/**
 * @file    ui_dependent_source.h
 * @brief   Drawing shared by the schematic symbols of dependent sources: the behavioral and the controlled ones.
 */

#ifndef IMCSIM_UI_DEPENDENT_SOURCE_H
#define IMCSIM_UI_DEPENDENT_SOURCE_H

#include "ui_elements/ui_element.h"
#include <memory>
#include <string>
#include <vector>

namespace GUI {

/**
 * @class   UIDependentSource
 * @brief   A diamond, the usual mark of a dependent source, with + and - inside when it sets a voltage and an arrow
 *          when it sets a current.
 * @details The output runs vertically: terminal 1 at (0, -2) on top and terminal 2 at (0, 2) below, with leads that
 *          reach the diamond. Derived symbols add what tells them apart, such as control terminals, and label the
 *          name and the value to the right of the diamond.
 */
class UIDependentSource : public UIElement {
public:
    UIDependentSource(std::unique_ptr<Core::Component> component, GridPoint position, Rotation rotation);
    ~UIDependentSource() override = default;

protected:
    /// The diamond reaches this far from the middle along both axes.
    static constexpr float DIAMOND_RADIUS = 0.8f;

    void DrawTerminals(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const override;
    void DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color, SymbolStyle style) const override;

    std::vector<GridPoint> GetLocalTerminals() const override;

    // Helpers for derived classes
    /**
     * @brief   Tells whether the source sets a voltage, drawn with + and -, or a current, drawn with an arrow.
     * @return  True for a voltage source, false for a current source.
     */
    virtual bool SetsVoltage() const = 0;
    void
    DrawNameAndValue(SchematicCanvas &canvas, const ViewTransform &view, const std::string &value, ImU32 color) const;
};

} // namespace GUI

#endif // IMCSIM_UI_DEPENDENT_SOURCE_H
