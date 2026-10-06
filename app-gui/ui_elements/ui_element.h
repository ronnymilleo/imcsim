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
 * @details Owns its Core::Component. Every element has two terminals at local x = -2 and x = +2;
 *          derived classes only draw the symbol between them.
 */
class UIElement {
public:
    UIElement(std::unique_ptr<Core::Component> component, ImVec2 position, Rotation rotation);
    virtual ~UIElement() = default;

    void Draw(ImDrawList *draw_list, const ViewTransform &view, ImU32 color, SymbolStyle style) const;

    const Core::Component &GetComponent() const;
    ImVec2 GetPosition() const;
    Rotation GetRotation() const;

protected:
    static constexpr float LineThickness = 2.0f;

    virtual void DrawSymbol(ImDrawList *draw_list, const ViewTransform &view, ImU32 color, SymbolStyle style) const = 0;
    ImVec2 LocalToScreen(const ViewTransform &view, float x, float y) const;

private:
    std::unique_ptr<Core::Component> m_Component;
    ImVec2 m_Position;
    Rotation m_Rotation;
};

} // namespace GUI

#endif // IMCSIM_UI_ELEMENT_H
