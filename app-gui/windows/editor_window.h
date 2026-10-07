/**
 * @file    editor_window.h
 * @brief   Schematic editor window with a pannable and zoomable grid canvas.
 */

#ifndef IMCSIM_EDITOR_WINDOW_H
#define IMCSIM_EDITOR_WINDOW_H

#include "components/component.h"
#include "connectivity.h"
#include "helpers.h"
#include "imgui.h"
#include "ui_elements/ui_element.h"
#include "ui_elements/ui_wire.h"
#include "windows/app_window.h"
#include <SDL3/SDL_dialog.h>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace GUI {

/**
 * @class   EditorWindow
 * @brief   Schematic editor window where circuit components are placed and wired on a grid.
 * @details Positions are stored in world units (one unit per grid cell) and converted to screen pixels
 *          using the current pan offset and zoom factor. The editor is either selecting (the default), placing
 *          a component or drawing wires; starting one mode leaves the other. Schematics are saved to and opened
 *          from JSON files through the native file dialogs of SDL. It owns the schematic; the Properties and
 *          Netlist windows read and edit it through the public methods.
 */
class EditorWindow : public AppWindow {
public:
    EditorWindow();
    ~EditorWindow() override = default;

    UIElement *GetSelectedElement();
    bool IsWireSelected() const;
    std::size_t GetSelectionVersion() const;
    void MarkModified();
    std::string BuildSpiceNetlist();

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

    /**
     * @enum    FileAction
     * @brief   File operation waiting for a file dialog or for the user to confirm discarding changes.
     */
    enum class FileAction {
        New,
        Open,
        Save
    };

    /**
     * @struct  DialogResult
     * @brief   What the file dialog returned: a path, or no value when it was canceled or failed.
     */
    struct DialogResult {
        std::optional<std::filesystem::path> Path;
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
    // Changes whenever the selection changes, so other windows can tell even when an index is reused
    std::size_t m_SelectionVersion = 0;
    // Rebuilt lazily after the schematic changes
    std::optional<Connectivity> m_Connectivity;
    bool m_ShowNodes = false;
    SymbolStyle m_SymbolStyle = SymbolStyle::IEC;
    ImVec2 m_Pan = {0, 0};
    float m_Zoom = 20.0f;
    std::optional<std::filesystem::path> m_FilePath;
    bool m_Modified = false;
    std::optional<FileAction> m_ActionToConfirm;
    // The dialog in progress, touched only by the main thread
    std::optional<FileAction> m_DialogAction;
    std::string m_DialogLocation;
    // Written by the dialog callback, which SDL may call from another thread
    std::mutex m_DialogMutex;
    std::optional<DialogResult> m_DialogResult;
    std::string m_FileMessagesTitle;
    std::vector<std::string> m_FileMessages;

    void Draw() override;
    void DrawToolbar();
    void DrawFileButtons();
    void HandleFileShortcuts();
    void DrawFilePopups();
    void RequestNew();
    void RequestOpen();
    void Save();
    void ShowFileDialog(FileAction action);
    static void SDLCALL HandleFileDialogResult(void *userdata, const char *const *file_list, int filter);
    void ProcessDialogResult();
    void NewSchematic();
    void OpenFile(const std::filesystem::path &path);
    void SaveFile(std::filesystem::path path);
    void ShowFileMessages(std::string title, std::vector<std::string> messages);
    const Connectivity &GetConnectivity();
    void DrawWires(ImDrawList *draw_list, const ViewTransform &view);
    void AssignName(Core::Component &component) const;
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

#endif // IMCSIM_EDITOR_WINDOW_H
