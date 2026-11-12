/**
 * @file    spice_block_dialog.h
 * @brief   The dialogs of blocks kept as SPICE text: picking what to import from a library, and their pin settings.
 */

#ifndef IMCSIM_SPICE_BLOCK_DIALOG_H
#define IMCSIM_SPICE_BLOCK_DIALOG_H

#include "circuit/spice_library.h"
#include "document/schematic_file.h"
#include <optional>
#include <string>
#include <vector>

namespace GUI {

/**
 * @struct  SpiceBlockResult
 * @brief   What the user confirmed in a SpiceBlockDialog, for the editor to apply to the schematic.
 * @details An import fills Imported with blocks and Models with device models; the settings of a block fill
 *          UpdatedName, Pins and Parameters.
 */
struct SpiceBlockResult {
    std::vector<SavedSubcircuit> Imported;
    std::vector<Core::LibraryModel> Models;
    std::optional<std::string> UpdatedName;
    std::vector<SavedSubcircuitPin> Pins;
    std::vector<Core::SubcircuitParameter> Parameters;
};

/**
 * @class   SpiceBlockDialog
 * @brief   A modal dialog that imports the subcircuits and device models of a SPICE library, or changes the pins of a
 *          subcircuit imported before.
 * @details Each pin of a library subcircuit is one of its ports: the dialog asks for its name and the edge of the block
 *          it goes on, starting from what the port's name suggests. Draw() runs every frame from the same place, so the
 *          popup opens where it is drawn; it returns the result once the user confirms.
 */
class SpiceBlockDialog {
public:
    // Opening
    void OpenImport(std::string file_name, Core::SpiceLibrary library);
    void OpenSettings(const SavedSubcircuit &definition);

    // Frame
    std::optional<SpiceBlockResult> Draw();

private:
    enum class Mode {
        IMPORT,
        SETTINGS
    };

    /// A subcircuit of the library and what the user chose for it.
    struct Choice {
        Core::LibrarySubcircuit Subcircuit;
        bool IsChosen = false;
        std::vector<SavedSubcircuitPin> Pins;
    };

    Mode m_Mode = Mode::IMPORT;
    bool m_IsOpenRequested = false;
    std::string m_Error;

    // Import
    std::string m_FileName;
    std::vector<std::string> m_Warnings;
    std::vector<Choice> m_Choices;
    std::vector<Core::LibraryModel> m_Models;
    /// One per model, whether it is picked; not a vector<bool>, which ImGui cannot point at.
    std::vector<char> m_AreModelsChosen;

    // Settings
    std::string m_BlockName;
    std::vector<std::string> m_Ports;
    std::vector<SavedSubcircuitPin> m_Pins;
    std::vector<Core::SubcircuitParameter> m_Parameters;
    std::string m_Text;

    std::optional<SpiceBlockResult> DrawImport();
    void DrawSubcircuitChoice(Choice &choice);
    void DrawModelChoices();
    std::optional<SpiceBlockResult> TakeChosen();
    std::optional<SpiceBlockResult> DrawSettings();
};

} // namespace GUI

#endif // IMCSIM_SPICE_BLOCK_DIALOG_H
