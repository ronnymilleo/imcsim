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
#include <string>
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
 * @struct  LocalBounds
 * @brief   Axis-aligned box around a symbol, in local grid units before rotation.
 */
struct LocalBounds {
    ImVec2 Min;
    ImVec2 Max;
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

    // Drawing
    void Draw(ImDrawList *draw_list, const ViewTransform &view, ImU32 color, SymbolStyle style) const;

    // Component and placement
    const Core::Component &GetComponent() const;
    Core::Component &GetComponent();
    GridPoint GetPosition() const;
    void SetPosition(GridPoint position);
    Rotation GetRotation() const;
    void SetRotation(Rotation rotation);

    // Geometry in world units
    std::vector<GridPoint> GetTerminals() const;
    bool Contains(ImVec2 world_pos) const;

protected:
    // Parts of the symbol, in the order Draw() draws them
    virtual void DrawTerminals(ImDrawList *draw_list, const ViewTransform &view, ImU32 color) const;
    virtual void DrawSymbol(ImDrawList *draw_list, const ViewTransform &view, ImU32 color, SymbolStyle style) const = 0;
    virtual void DrawLabels(ImDrawList *draw_list, const ViewTransform &view, ImU32 color) const;

    // Geometry in local grid units, before rotation and translation
    virtual std::vector<GridPoint> GetLocalTerminals() const;
    virtual LocalBounds GetLocalBounds() const;

    // Helpers for derived classes
    void DrawLabel(ImDrawList *draw_list, const ViewTransform &view, ImVec2 local_anchor, ImVec2 local_direction,
                   const std::string &text, ImU32 color) const;
    ImVec2 LocalToScreen(const ViewTransform &view, float x, float y) const;

private:
    std::unique_ptr<Core::Component> m_Component;
    GridPoint m_Position;
    Rotation m_Rotation;
};

} // namespace GUI

#endif // IMCSIM_UI_ELEMENT_H
