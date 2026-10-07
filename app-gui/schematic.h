/**
 * @file    schematic.h
 * @brief   The schematic document shared by every window: elements, wires, selection and file state.
 */

#ifndef IMCSIM_SCHEMATIC_H
#define IMCSIM_SCHEMATIC_H

#include "connectivity.h"
#include "helpers.h"
#include "ui_elements/ui_element.h"
#include "ui_elements/ui_wire.h"
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace GUI {

/**
 * @class   Schematic
 * @brief   The circuit being edited, owned by the application and shared by the windows that show or edit it.
 * @details Adding and deleting mark the schematic as modified. Geometry updates that may be undone within the
 *          same gesture, such as wires following a dragged element, only invalidate the nodes; the caller
 *          decides when the gesture counts as a change and calls MarkModified(). Nodes are computed lazily and
 *          cached until the next change. Indices returned by the selection stay valid until an element or wire
 *          is added, deleted or the wires are replaced.
 */
class Schematic {
public:
    // Content
    const std::vector<std::unique_ptr<UIElement>> &GetElements() const;
    UIElement &GetElement(std::size_t index);
    const std::vector<UIWire> &GetWires() const;
    bool IsTerminal(GridPoint point) const;
    std::vector<GridPoint> CollectTerminals() const;

    // Editing
    void AddElement(std::unique_ptr<UIElement> element);
    void AddWire(GridPoint start, GridPoint end);
    void SetWires(std::vector<UIWire> wires);
    void SimplifyAllWires();
    void DeleteSelection();
    void Clear();
    void Replace(std::vector<std::unique_ptr<UIElement>> elements, std::vector<UIWire> wires);

    // Selection
    void SelectElement(std::size_t index);
    void SelectWire(std::size_t index);
    void ClearSelection();
    std::optional<std::size_t> GetSelectedElementIndex() const;
    std::optional<std::size_t> GetSelectedWireIndex() const;
    UIElement *GetSelectedElement();
    std::size_t GetSelectionVersion() const;

    // Document state
    bool IsModified() const;
    void MarkModified();
    const std::optional<std::filesystem::path> &GetFilePath() const;
    void MarkSaved(std::filesystem::path path);

    // Derived data
    const Connectivity &GetConnectivity();
    std::string BuildSpiceNetlist();

private:
    // Content
    std::vector<std::unique_ptr<UIElement>> m_Elements;
    std::vector<UIWire> m_Wires;

    // Selection; the version changes with every change, so windows can tell even when an index is reused
    std::optional<std::size_t> m_SelectedElement;
    std::optional<std::size_t> m_SelectedWire;
    std::size_t m_SelectionVersion = 0;

    // Document state
    bool m_Modified = false;
    std::optional<std::filesystem::path> m_FilePath;

    // Derived data, rebuilt on first use after a change
    std::optional<Connectivity> m_Connectivity;
};

} // namespace GUI

#endif // IMCSIM_SCHEMATIC_H
