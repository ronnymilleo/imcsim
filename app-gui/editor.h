/**
 * @file    editor.h
 * @brief   Schematic editor window with a pannable and zoomable grid canvas.
 */

#ifndef IMCSIM_EDITOR_H
#define IMCSIM_EDITOR_H

#include "components/component.h"
#include "helpers.h"
#include "imgui.h"
#include "ui_elements/ui_element.h"
#include <memory>
#include <optional>
#include <vector>

namespace GUI {

/**
 * @class   Editor
 * @brief   Schematic editor window where circuit components are placed on a grid.
 * @details Positions are stored in world units (one unit per grid cell) and converted to screen pixels
 *          using the current pan offset and zoom factor.
 */
class Editor {
public:
    Editor() = default;
    ~Editor() = default;

    void Draw();

private:
    std::vector<std::unique_ptr<UIElement>> m_Elements;
    std::optional<Core::ComponentType> m_PlacingType;
    Rotation m_PlacingRotation = Rotation::R0;
    SymbolStyle m_SymbolStyle = SymbolStyle::IEC;
    ImVec2 m_Pan = {0, 0};
    float m_Zoom = 20.0f;

    void DrawToolbar();
    void HandlePanAndZoom(ImVec2 origin, bool hovered, bool active);
    void HandlePlacement(ImDrawList *draw_list, const ViewTransform &view, bool hovered);
};

} // namespace GUI

#endif // IMCSIM_EDITOR_H
