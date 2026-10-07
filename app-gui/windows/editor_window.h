/**
 * @file    editor_window.h
 * @brief   Schematic editor window with a pannable and zoomable grid canvas.
 */

#ifndef IMCSIM_EDITOR_WINDOW_H
#define IMCSIM_EDITOR_WINDOW_H

#include "components/component.h"
#include "helpers.h"
#include "imgui.h"
#include "schematic.h"
#include "ui_elements/ui_element.h"
#include "ui_elements/ui_wire.h"
#include "windows/app_window.h"
#include <SDL3/SDL_dialog.h>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace GUI {

/**
 * @class   EditorWindow
 * @brief   Window where the schematic is drawn and edited: components are placed and wired on a grid.
 * @details Positions are stored in world units (one unit per grid cell) and converted to screen pixels
 *          using the current pan offset and zoom factor. The editor is either selecting (the default), placing
 *          a component or drawing wires; starting one mode leaves the other. Schematics are saved to and opened
 *          from JSON files through the native file dialogs of SDL. The schematic itself belongs to the
 *          application; this window only keeps interaction state such as the current mode, drag and view.
 */
class EditorWindow : public AppWindow {
public:
    explicit EditorWindow(Schematic &schematic);
    ~EditorWindow() override = default;

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

    Schematic &m_Schematic;

    // View
    ImVec2 m_Pan = {0, 0};
    float m_Zoom = 20.0f;
    SymbolStyle m_SymbolStyle = SymbolStyle::IEC;
    bool m_ShowNodes = false;

    // Placement
    std::optional<Core::ComponentType> m_PlacingType;
    Rotation m_PlacingRotation = Rotation::R0;

    // Wiring
    bool m_DrawingWires = false;
    std::optional<GridPoint> m_WireStart;
    bool m_WireVerticalFirst = false;

    // Selection and dragging
    std::optional<ElementDrag> m_Drag;

    // Files: the dialog in progress and the confirmation are touched only by the main thread
    std::optional<FileAction> m_ActionToConfirm;
    std::optional<FileAction> m_DialogAction;
    std::string m_DialogLocation;
    // Written by the dialog callback, which SDL may call from another thread
    std::mutex m_DialogMutex;
    std::optional<DialogResult> m_DialogResult;
    std::string m_FileMessagesTitle;
    std::vector<std::string> m_FileMessages;

    // Frame
    void Draw() override;
    void DrawToolbar();
    void DrawWires(ImDrawList *draw_list, const ViewTransform &view);
    void HandlePanAndZoom(ImVec2 origin, bool hovered, bool active);

    // Modes
    void StartPlacing(Core::ComponentType type);
    void StartDrawingWires();

    // Placement
    void HandlePlacement(ImDrawList *draw_list, const ViewTransform &view, bool hovered);

    // Wiring
    void HandleWireDrawing(ImDrawList *draw_list, const ViewTransform &view, bool hovered);
    void StopWire();

    // Selection and dragging
    void HandleSelection(const ViewTransform &view, bool hovered);
    void SelectAt(ImVec2 world_pos);
    void ClearSelection();
    void EndDrag();
    void RotateSelectedElement();

    // File commands
    void DrawFileButtons();
    void HandleFileShortcuts();
    void RequestNew();
    void RequestOpen();
    void Save();
    void NewSchematic();
    void OpenFile(const std::filesystem::path &path);
    void SaveFile(std::filesystem::path path);

    // File dialogs and messages
    void ShowFileDialog(FileAction action);
    static void SDLCALL HandleFileDialogResult(void *userdata, const char *const *file_list, int filter);
    void ProcessDialogResult();
    void DrawFilePopups();
    void ShowFileMessages(std::string title, std::vector<std::string> messages);
};

} // namespace GUI

#endif // IMCSIM_EDITOR_WINDOW_H
