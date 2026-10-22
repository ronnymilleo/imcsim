/**
 * @file    schematic_file_actions.cpp
 * @brief   New, Open, Save, Import and Export of the schematic, with their dialogs, confirmations and messages.
 */

#include "schematic_file_actions.h"

#include "schematic_drawing.h"
#include "schematic_file.h"
#include "theme.h"
#include <array>
#include <format>
#include <utility>

namespace GUI {

namespace {

constexpr const char *DISCARD_POPUP = "Discard changes?";
constexpr const char *MESSAGES_POPUP = "File messages";
constexpr const char *EXPORT_POPUP = "Export schematic";
// Must stay valid until the dialog callback runs, so it lives for the whole program
constexpr std::array<SDL_DialogFileFilter, 1> SCHEMATIC_FILTERS = {{{"imcsim schematic", "imcsim"}}};
constexpr std::array<SDL_DialogFileFilter, 1> SVG_FILTERS = {{{"SVG image", "svg"}}};

} // namespace

/**
 * @brief   Creates the file actions of a schematic.
 * @param[in] schematic    Schematic to read and write; it must outlive the actions.
 * @param[in] preferences  Symbol style and terminal numbers, which the exported image follows; they must outlive the
 *                         actions.
 * @param[in] hooks        What the editor does around an action; every hook must be set.
 */
SchematicFileActions::SchematicFileActions(Schematic &schematic, const EditorPreferences &preferences,
                                           FileActionHooks hooks)
    : m_Schematic(schematic), m_Preferences(preferences), m_Hooks(std::move(hooks)) {
}

/**
 * @brief   Runs the requests waiting for a frame: the result of a file dialog, quitting, and examples or files to open
 *          or insert.
 * @note    Call it every frame, outside any window, before DrawPopups().
 */
void SchematicFileActions::Update() {
    ProcessDialogResult();
    HandleQuitRequest();
    HandleExampleRequest();
}

/**
 * @brief   Draws the popups the actions open: the export settings, the confirmation to discard unsaved changes and the
 *          messages of a file that could not be read or written.
 * @note    Call it every frame, outside any window, so the popups show whichever window is in front.
 */
void SchematicFileActions::DrawPopups() {
    DrawExportPopup();
    // Centered each time they appear, like the export popup, instead of where imgui.ini remembers them
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(DISCARD_POPUP, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("The schematic has unsaved changes. Discard them?");
        if (ImGui::Button("Discard")) {
            if (m_ActionToConfirm == FileAction::NEW) {
                NewSchematic();
            } else if (m_ActionToConfirm == FileAction::OPEN) {
                ShowFileDialog(FileAction::OPEN);
            } else if (m_ActionToConfirm == FileAction::QUIT) {
                m_QuitConfirmed = true;
            } else if (m_ActionToConfirm == FileAction::EXAMPLE) {
                OpenExample(*m_ExampleToOpen);
            }
            m_ActionToConfirm.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            m_ActionToConfirm.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(MESSAGES_POPUP, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(m_MessagesTitle.c_str());
        ImGui::Separator();
        for (const std::string &message : m_Messages) {
            ImGui::BulletText("%s", message.c_str());
        }
        if (PrimaryButton("OK")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

/**
 * @brief   Asks to quit, as when the user closes the main window. Call it between frames.
 * @note    Quitting is confirmed right away when there is nothing to lose; otherwise the next frame asks the user
 *          to discard the changes. Check IsQuitConfirmed() after drawing the frame.
 */
void SchematicFileActions::RequestQuit() {
    m_QuitRequested = true;
}

/**
 * @brief   Tells whether the application may quit.
 * @return  True once a quit request was confirmed, by the user or because there were no unsaved changes.
 */
bool SchematicFileActions::IsQuitConfirmed() const {
    return m_QuitConfirmed;
}

/**
 * @brief   Asks to open an example, as when the user picks one from the menu.
 * @param[in] example  One of GetExamples().
 * @note    Like quitting, the next frame opens it, after asking the user to discard unsaved changes if there are
 *          any. The example opens untitled, so saving it asks for a file.
 */
void SchematicFileActions::RequestExample(const Example &example) {
    m_RequestedExample = example;
}

/**
 * @brief   Asks to insert an example into the schematic, as when the user picks it from the insert menu.
 * @param[in] example  One of GetExamples().
 * @note    The next frame starts placing it like an import. Nothing is replaced, so nothing is asked.
 */
void SchematicFileActions::RequestInsertExample(const Example &example) {
    m_RequestedInsertExample = example;
}

/**
 * @brief   Asks to import a file into the schematic, as when the user drops it on the window.
 * @param[in] path  A schematic file.
 * @note    The next frame starts placing it, as File > Import does after its dialog.
 */
void SchematicFileActions::RequestImport(std::filesystem::path path) {
    m_RequestedImport = std::move(path);
}

/**
 * @brief   Starts an empty schematic, after asking to discard unsaved changes if there are any.
 */
void SchematicFileActions::RequestNew() {
    if (m_Schematic.IsModified()) {
        m_ActionToConfirm = FileAction::NEW;
        ImGui::OpenPopup(DISCARD_POPUP);
    } else {
        NewSchematic();
    }
}

/**
 * @brief   Shows the dialog to open a schematic, after asking to discard unsaved changes if there are any.
 */
void SchematicFileActions::RequestOpen() {
    if (m_Schematic.IsModified()) {
        m_ActionToConfirm = FileAction::OPEN;
        ImGui::OpenPopup(DISCARD_POPUP);
    } else {
        ShowFileDialog(FileAction::OPEN);
    }
}

/**
 * @brief   Saves the schematic to its file, or asks for one when it has none yet.
 */
void SchematicFileActions::Save() {
    if (const auto &file_path = m_Schematic.GetFilePath()) {
        SaveFile(*file_path);
    } else {
        ShowFileDialog(FileAction::SAVE);
    }
}

/**
 * @brief   Shows the dialog to save the schematic to a new file.
 */
void SchematicFileActions::SaveAs() {
    ShowFileDialog(FileAction::SAVE);
}

/**
 * @brief   Shows the dialog to pick a file whose circuit joins the schematic.
 */
void SchematicFileActions::Import() {
    ShowFileDialog(FileAction::IMPORT);
}

/**
 * @brief   Opens the popup that exports the schematic as an SVG image.
 * @note    Call it from where DrawPopups() runs, so ImGui finds the popup in the same ID stack.
 */
void SchematicFileActions::OpenExportPopup() {
    ImGui::OpenPopup(EXPORT_POPUP);
}

void SchematicFileActions::HandleQuitRequest() {
    if (!std::exchange(m_QuitRequested, false)) {
        return;
    }
    if (m_Schematic.IsModified()) {
        m_ActionToConfirm = FileAction::QUIT;
        ImGui::OpenPopup(DISCARD_POPUP);
    } else {
        m_QuitConfirmed = true;
    }
}

void SchematicFileActions::HandleExampleRequest() {
    if (const std::optional<Example> insert = std::exchange(m_RequestedInsertExample, std::nullopt)) {
        InsertExample(*insert);
    }
    if (const std::optional<std::filesystem::path> path = std::exchange(m_RequestedImport, std::nullopt)) {
        ImportFile(*path);
    }
    const std::optional<Example> example = std::exchange(m_RequestedExample, std::nullopt);
    if (!example) {
        return;
    }
    if (m_Schematic.IsModified()) {
        m_ExampleToOpen = example;
        m_ActionToConfirm = FileAction::EXAMPLE;
        ImGui::OpenPopup(DISCARD_POPUP);
    } else {
        OpenExample(*example);
    }
}

void SchematicFileActions::NewSchematic() {
    m_Hooks.StopEditing();
    m_Schematic.Clear();
    m_Schematic.ClearMeasurements();
}

// Skipped parts are reported, and the schematic counts as modified because saving it would drop them
void SchematicFileActions::OpenFile(const std::filesystem::path &path) {
    const auto text = ReadTextFile(path);
    if (!text) {
        ShowMessages("Could not open the schematic", {text.error()});
        return;
    }
    auto loaded = LoadSchematic(*text);
    if (!loaded) {
        ShowMessages(std::format("Could not open {}", path.filename().string()), {loaded.error()});
        return;
    }

    NewSchematic();
    m_Schematic.Replace(std::move(loaded->Elements), std::move(loaded->Wires), std::move(loaded->Settings));
    m_Schematic.RestoreMeasurements(loaded->Measurements);
    m_Schematic.MarkSaved(path);
    m_Hooks.FrameSchematic();
    if (!loaded->Warnings.empty()) {
        m_Schematic.MarkModified();
        ShowMessages(std::format("{} was opened, but some parts were changed or skipped", path.filename().string()),
                     std::move(loaded->Warnings));
    }
}

void SchematicFileActions::OpenExample(const Example &example) {
    auto loaded = LoadExample(example);
    if (!loaded) {
        ShowMessages(std::format("Could not open the example {}", example.Title), {loaded.error()});
        return;
    }

    NewSchematic();
    m_Schematic.Replace(std::move(loaded->Elements), std::move(loaded->Wires), std::move(loaded->Settings));
    m_Schematic.RestoreMeasurements(loaded->Measurements);
    m_Hooks.FrameSchematic();
}

// The circuit of the file joins the schematic as a pasted group: it follows the cursor until a click places it, and
// the settings and probes of the file are left behind. Skipped parts are reported, but the rest still comes in
void SchematicFileActions::ImportFile(const std::filesystem::path &path) {
    const auto text = ReadTextFile(path);
    if (!text) {
        ShowMessages("Could not import the schematic", {text.error()});
        return;
    }
    auto group = ParseCopiedSchematic(*text);
    if (!group) {
        ShowMessages(std::format("Could not import {}", path.filename().string()), {group.error()});
        return;
    }
    if (!group->Warnings.empty()) {
        ShowMessages(std::format("{} was imported, but some parts were changed or skipped", path.filename().string()),
                     group->Warnings);
    }
    m_Hooks.PlaceGroup(std::move(*group));
}

// Like an import, the example follows the cursor until a click places it; its analysis settings stay behind
void SchematicFileActions::InsertExample(const Example &example) {
    auto loaded = LoadExample(example);
    if (!loaded) {
        ShowMessages(std::format("Could not insert the example {}", example.Title), {loaded.error()});
        return;
    }
    auto group = MakePastedGroup(std::move(*loaded));
    if (!group) {
        ShowMessages(std::format("Could not insert the example {}", example.Title), {group.error()});
        return;
    }
    m_Hooks.PlaceGroup(std::move(*group));
}

// Dialogs do not always add the extension, so it is added here when missing
void SchematicFileActions::SaveFile(std::filesystem::path path) {
    if (path.extension() != SCHEMATIC_EXTENSION) {
        path += SCHEMATIC_EXTENSION;
    }
    const auto written =
        WriteTextFile(path, SaveSchematic(m_Schematic.GetElements(), m_Schematic.GetWires(),
                                          m_Schematic.GetSimulationSettings(), m_Schematic.SaveMeasurements()));
    if (!written) {
        ShowMessages("Could not save the schematic", {written.error()});
        return;
    }
    m_Schematic.MarkSaved(std::move(path));
}

// The dialog runs asynchronously: its result is picked up by ProcessDialogResult() on a later frame
void SchematicFileActions::ShowFileDialog(const FileAction action) {
    if (m_FileDialog.IsPending()) {
        return;
    }
    m_DialogAction = action;
    const auto &file_path = m_Schematic.GetFilePath();
    if (action == FileAction::EXPORT_SCHEMATIC) {
        // Next to the schematic and named after it, like the exported plots
        const std::string stem = file_path ? file_path->stem().string() : "circuit";
        const std::filesystem::path name = std::format("{}-schematic.svg", stem);
        m_FileDialog.ShowSave(SVG_FILTERS, (file_path ? file_path->parent_path() / name : name).string());
        return;
    }
    std::string location = file_path ? file_path->string() : "";
    if (action == FileAction::OPEN || action == FileAction::IMPORT) {
        m_FileDialog.ShowOpen(SCHEMATIC_FILTERS, std::move(location));
    } else {
        m_FileDialog.ShowSave(SCHEMATIC_FILTERS, std::move(location));
    }
}

void SchematicFileActions::ProcessDialogResult() {
    const std::optional<std::optional<std::filesystem::path>> result = m_FileDialog.TakeResult();
    if (!result) {
        return;
    }
    const std::optional<FileAction> action = std::exchange(m_DialogAction, std::nullopt);
    if (!*result) {
        return;
    }
    if (action == FileAction::OPEN) {
        OpenFile(**result);
    } else if (action == FileAction::SAVE) {
        SaveFile(**result);
    } else if (action == FileAction::EXPORT_SCHEMATIC) {
        ExportSchematic(**result);
    } else if (action == FileAction::IMPORT) {
        ImportFile(**result);
    }
}

// The image follows the editor: the symbol style chosen in the View menu, names, values and probes
void SchematicFileActions::DrawExportPopup() {
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(EXPORT_POPUP, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    ImGui::TextDisabled("As shown: symbols, names, values, terminal numbers and the probes of the plots");
    ImGui::Separator();
    if (ImGui::RadioButton("Light, for print", !m_ExportDark)) {
        m_ExportDark = false;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("As on screen", m_ExportDark)) {
        m_ExportDark = true;
    }
    ImGui::Separator();
    const bool empty = m_Schematic.GetElements().empty() && m_Schematic.GetWires().empty();
    if (empty) {
        ImGui::TextDisabled("Place some parts to export the schematic");
    }
    ImGui::BeginDisabled(empty || m_FileDialog.IsPending());
    if (PrimaryButton("Save SVG...")) {
        ShowFileDialog(FileAction::EXPORT_SCHEMATIC);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// Dialogs do not always add the extension, so it is added here when missing
void SchematicFileActions::ExportSchematic(std::filesystem::path path) {
    if (path.extension() != ".svg") {
        path += ".svg";
    }
    const auto svg = RenderSchematicSvg(m_Schematic, m_Preferences.GetSymbolStyle(),
                                        m_Preferences.ShowsTerminalNumbers(), m_ExportDark);
    const auto written =
        svg ? WriteTextFile(path, *svg) : std::expected<void, std::string>(std::unexpected(svg.error()));
    if (!written) {
        ShowMessages("Could not export the schematic", {written.error()});
    }
}

void SchematicFileActions::ShowMessages(std::string title, std::vector<std::string> messages) {
    m_MessagesTitle = std::move(title);
    m_Messages = std::move(messages);
    ImGui::OpenPopup(MESSAGES_POPUP);
}

} // namespace GUI
