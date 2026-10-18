/**
 * @file    schematic_file_actions.h
 * @brief   New, Open, Save, Import and Export of the schematic, with their dialogs, confirmations and messages.
 */

#ifndef IMCSIM_SCHEMATIC_FILE_ACTIONS_H
#define IMCSIM_SCHEMATIC_FILE_ACTIONS_H

#include "clipboard.h"
#include "editor_preferences.h"
#include "examples.h"
#include "file_dialog.h"
#include "schematic.h"
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace GUI {

/**
 * @struct  FileActionHooks
 * @brief   What the editor does around a file action, which the file actions cannot do themselves.
 */
struct FileActionHooks {
    // Leaves any drag, paste, placement or wire in progress, before the schematic is cleared or replaced
    std::function<void()> StopEditing;
    // Shows the whole schematic that was just opened
    std::function<void()> FrameSchematic;
    // Lets an imported file or an inserted example follow the cursor until a click places it
    std::function<void(PastedGroup)> PlaceGroup;
};

/**
 * @class   SchematicFileActions
 * @brief   Reads and writes the schematic through the native file dialogs of SDL, asks before discarding unsaved
 *          changes, and reports what went wrong in a popup.
 * @details Requests that come from outside a frame (quitting, examples, dropped files) wait for the next Update().
 *          The dialogs answer asynchronously, so their result is also picked up by a later Update(). The popups are
 *          drawn by DrawPopups(), outside any window, so they show whichever window is in front.
 */
class SchematicFileActions {
public:
    SchematicFileActions(Schematic &schematic, const EditorPreferences &preferences, FileActionHooks hooks);

    // Frame
    void Update();
    void DrawPopups();

    // Quitting
    void RequestQuit();
    bool IsQuitConfirmed() const;

    // Requests that wait for the next Update()
    void RequestExample(const Example &example);
    void RequestInsertExample(const Example &example);
    void RequestImport(std::filesystem::path path);

    // Commands
    void RequestNew();
    void RequestOpen();
    void Save();
    void SaveAs();
    void Import();
    void OpenExportPopup();

private:
    /**
     * @enum    FileAction
     * @brief   File operation waiting for a file dialog or for the user to confirm discarding changes.
     */
    enum class FileAction {
        New,
        Open,
        Save,
        Quit,
        Example,
        ExportSchematic,
        Import
    };

    Schematic &m_Schematic;
    const EditorPreferences &m_Preferences;
    FileActionHooks m_Hooks;

    // Quitting: requested by the application between frames, handled by the next Update()
    bool m_QuitRequested = false;
    bool m_QuitConfirmed = false;

    // Examples: requested from the menu and handled by the next Update(), then kept while discarding is confirmed
    std::optional<Example> m_RequestedExample;
    // Insertions, which add to the schematic instead of replacing it, so they need no confirmation
    std::optional<Example> m_RequestedInsertExample;
    std::optional<std::filesystem::path> m_RequestedImport;
    std::optional<Example> m_ExampleToOpen;

    // The dialog in progress and the confirmation are touched only by the main thread
    std::optional<FileAction> m_ActionToConfirm;
    std::optional<FileAction> m_DialogAction;
    FileDialog m_FileDialog;
    std::string m_MessagesTitle;
    std::vector<std::string> m_Messages;
    // Export: dark keeps the colors of the editor, light suits print
    bool m_ExportDark = false;

    void HandleQuitRequest();
    void HandleExampleRequest();
    void NewSchematic();
    void OpenFile(const std::filesystem::path &path);
    void OpenExample(const Example &example);
    void ImportFile(const std::filesystem::path &path);
    void InsertExample(const Example &example);
    void SaveFile(std::filesystem::path path);
    void ShowFileDialog(FileAction action);
    void ProcessDialogResult();
    void DrawExportPopup();
    void ExportSchematic(std::filesystem::path path);
    void ShowMessages(std::string title, std::vector<std::string> messages);
};

} // namespace GUI

#endif // IMCSIM_SCHEMATIC_FILE_ACTIONS_H
