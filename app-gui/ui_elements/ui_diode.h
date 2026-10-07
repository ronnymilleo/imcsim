/**
 * @file    ui_diode.h
 * @brief   Schematic symbol of a diode, a Zener diode or an LED.
 */

#ifndef IMCSIM_UI_DIODE_H
#define IMCSIM_UI_DIODE_H

#include "ui_element.h"

namespace GUI {

/**
 * @class   UIDiode
 * @brief   Diode drawn as a triangle pointing at a bar, from the anode (x = -2) to the cathode (x = +2).
 * @details One class serves the three diode parts: a Zener bends the ends of the bar and an LED adds two emission
 *          arrows. The model name is shown where other parts show their value.
 */
class UIDiode : public UIElement {
public:
    UIDiode(Core::ComponentType type, GridPoint position, Rotation rotation);
    ~UIDiode() override = default;

protected:
    void DrawSymbol(ImDrawList *draw_list, const ViewTransform &view, ImU32 color, SymbolStyle style) const override;
    void DrawLabels(ImDrawList *draw_list, const ViewTransform &view, ImU32 color) const override;

    LocalBounds GetLocalBounds() const override;

private:
    void DrawArrow(ImDrawList *draw_list, const ViewTransform &view, ImVec2 start, ImU32 color) const;
};

} // namespace GUI

#endif // IMCSIM_UI_DIODE_H
