/**
 * @file    ui_element.h
 * @brief   Visual representation of a circuit component inside the schematic editor.
 */

#ifndef IMCSIM_UI_ELEMENT_H
#define IMCSIM_UI_ELEMENT_H

#include "components/component.h"
#include "helpers.h"
#include "imgui.h"
#include <memory>
#include <vector>

namespace GUI {

/**
 * @enum    SymbolStyle
 * @brief   Drawing standard for schematic symbols.
 * @details Only symbols that differ between standards use it; the resistor is a rectangle in IEC and a zig-zag
 *          in ANSI.
 */
enum class SymbolStyle {
    IEC,
    ANSI
};

/**
 * @class   UIElement
 * @brief   A component placed on the schematic grid, with its position and rotation.
 * @details Owns its Core::Component. By default an element has two terminals at local x = -2 and x = +2 and
 *          derived classes only draw the symbol between them; single-terminal symbols override DrawTerminals()
 *          and GetLocalTerminals().
 */
class UIElement {
public:
    UIElement(std::unique_ptr<Core::Component> component, GridPoint position, Rotation rotation);
    virtual ~UIElement() = default;

    void Draw(ImDrawList *draw_list, const ViewTransform &view, ImU32 color, SymbolStyle style) const;

    const Core::Component &GetComponent() const;
    GridPoint GetPosition() const;
    Rotation GetRotation() const;
    std::vector<GridPoint> GetTerminals() const;

protected:
    virtual void DrawSymbol(ImDrawList *draw_list, const ViewTransform &view, ImU32 color, SymbolStyle style) const = 0;
    virtual void DrawTerminals(ImDrawList *draw_list, const ViewTransform &view, ImU32 color) const;
    virtual std::vector<GridPoint> GetLocalTerminals() const;
    ImVec2 LocalToScreen(const ViewTransform &view, float x, float y) const;

private:
    std::unique_ptr<Core::Component> m_Component;
    GridPoint m_Position;
    Rotation m_Rotation;
};

} // namespace GUI

#endif // IMCSIM_UI_ELEMENT_H
