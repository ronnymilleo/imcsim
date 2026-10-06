/**
 * @file    editor.h
 * @brief   Schematic editor window with a pannable and zoomable grid canvas.
 */

#ifndef IMCSIM_EDITOR_H
#define IMCSIM_EDITOR_H

#include "components/component.h"
#include "connectivity.h"
#include "helpers.h"
#include "imgui.h"
#include "ui_elements/ui_element.h"
#include "ui_elements/ui_wire.h"
#include <memory>
#include <optional>
#include <vector>

namespace GUI {

/**
 * @class   Editor
 * @brief   Schematic editor window where circuit components are placed and wired on a grid.
 * @details Positions are stored in world units (one unit per grid cell) and converted to screen pixels
 *          using the current pan offset and zoom factor. The editor is either selecting (the default), placing
 *          a component or drawing wires; starting one mode leaves the other.
 */
class Editor {
public:
    Editor() = default;
    ~Editor() = default;

    void Draw();

private:
    /**
     * @struct  ElementDrag
     * @brief   Schematic state captured when an element drag starts; every frame of the drag is computed from it.
     */
    struct ElementDrag {
        GridPoint StartCursor;
        GridPoint StartPosition;
        std::vector<GridPoint> StartTerminals;
        std::vector<UIWire> StartWires;
    };

    std::vector<std::unique_ptr<UIElement>> m_Elements;
    std::vector<UIWire> m_Wires;
    std::optional<Core::ComponentType> m_PlacingType;
    Rotation m_PlacingRotation = Rotation::R0;
    bool m_DrawingWires = false;
    std::optional<GridPoint> m_WireStart;
    bool m_WireVerticalFirst = false;
    std::optional<std::size_t> m_SelectedElement;
    std::optional<std::size_t> m_SelectedWire;
    std::optional<ElementDrag> m_Drag;
    // Rebuilt lazily after the schematic changes
    std::optional<Connectivity> m_Connectivity;
    bool m_ShowNodes = false;
    bool m_ShowNetlist = false;
    SymbolStyle m_SymbolStyle = SymbolStyle::IEC;
    ImVec2 m_Pan = {0, 0};
    float m_Zoom = 20.0f;

    void DrawToolbar();
    void DrawWires(ImDrawList *draw_list, const ViewTransform &view);
    void DrawNetlistWindow();
    void StartPlacing(Core::ComponentType type);
    void StartDrawingWires();
    void HandlePanAndZoom(ImVec2 origin, bool hovered, bool active);
    void HandlePlacement(ImDrawList *draw_list, const ViewTransform &view, bool hovered);
    void HandleWireDrawing(ImDrawList *draw_list, const ViewTransform &view, bool hovered);
    void StopWire();
    void AddWire(GridPoint start, GridPoint end);
    bool IsTerminal(GridPoint point) const;
    void HandleSelection(const ViewTransform &view, bool hovered);
    void SelectAt(ImVec2 world_pos);
    void ClearSelection();
    void EndDrag();
    void RotateSelectedElement();
    void DeleteSelection();
    void SimplifyAllWires();
    std::vector<GridPoint> CollectTerminals() const;
};

} // namespace GUI

#endif // IMCSIM_EDITOR_H
