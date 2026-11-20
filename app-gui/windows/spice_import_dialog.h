/**
 * @file    spice_import_dialog.h
 * @brief   The dialog that picks what to import from a SPICE library and sets up the pins of the blocks it becomes.
 */

#ifndef IMCSIM_SPICE_IMPORT_DIALOG_H
#define IMCSIM_SPICE_IMPORT_DIALOG_H

#include "circuit/spice_library.h"
#include "schematic/schematic_file.h"
#include <optional>
#include <string>
#include <vector>

namespace GUI {

/**
 * @struct  SpiceImportResult
 * @brief   What the user picked to import from a SPICE library, for the editor to add to the schematic.
 */
struct SpiceImportResult {
    /// The picked subcircuits, as blocks with the pins the user set up.
    std::vector<SavedSubcircuit> Blocks;
    /// The picked device models.
    std::vector<Core::LibraryModel> Models;
};

/**
 * @class   SpiceImportDialog
 * @brief   A modal dialog that imports the subcircuits and device models of a SPICE library.
 * @details Each pin of a library subcircuit is one of its ports: the dialog asks for its name and the edge of the block
 *          it goes on, starting from what the port's name suggests. Draw() runs every frame from the same place, so the
 *          popup opens where it is drawn; it returns the result once the user confirms.
 */
class SpiceImportDialog {
public:
    // Opening
    void Open(std::string file_name, Core::SpiceLibrary library);

    // Frame
    std::optional<SpiceImportResult> Draw();

private:
    /// A subcircuit of the library and what the user chose for it.
    struct Choice {
        Core::LibrarySubcircuit Subcircuit;
        bool IsChosen = false;
        std::vector<SavedSubcircuitPin> Pins;
    };

    bool m_IsOpenRequested = false;
    std::string m_Error;
    std::string m_FileName;
    std::vector<std::string> m_Warnings;
    std::vector<Choice> m_Choices;
    std::vector<Core::LibraryModel> m_Models;
    /// One per model, whether it is picked; not a vector<bool>, which ImGui cannot point at.
    std::vector<char> m_AreModelsChosen;

    void DrawSubcircuitChoice(Choice &choice);
    void DrawModelChoices();
    std::optional<SpiceImportResult> TakeChosen();
};

} // namespace GUI

#endif // IMCSIM_SPICE_IMPORT_DIALOG_H
