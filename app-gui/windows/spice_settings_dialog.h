/**
 * @file    spice_settings_dialog.h
 * @brief   The dialog that sets the pins and parameter defaults of a block kept as SPICE text.
 */

#ifndef IMCSIM_SPICE_SETTINGS_DIALOG_H
#define IMCSIM_SPICE_SETTINGS_DIALOG_H

#include "circuit/subcircuit.h"
#include "schematic/schematic_file.h"
#include <optional>
#include <string>
#include <vector>

namespace GUI {

/**
 * @struct  SpiceSettingsResult
 * @brief   The new settings of a block kept as SPICE text, for the editor to apply to every instance of it.
 */
struct SpiceSettingsResult {
    std::string Name; ///< The name of the block, which does not change.
    std::vector<SavedSubcircuitPin> Pins;
    std::vector<Core::SubcircuitParameter> Parameters;
};

/**
 * @class   SpiceSettingsDialog
 * @brief   A modal dialog that changes the pins and parameter defaults of a subcircuit imported before.
 * @details The text of the block stays as it came, shown read only. Draw() runs every frame from the same place, so
 *          the popup opens where it is drawn; it returns the result once the user confirms.
 */
class SpiceSettingsDialog {
public:
    // Opening
    void Open(const SavedSubcircuit &definition);

    // Frame
    std::optional<SpiceSettingsResult> Draw();

private:
    bool m_IsOpenRequested = false;
    std::string m_Error;
    std::string m_BlockName;
    std::vector<std::string> m_Ports;
    std::vector<SavedSubcircuitPin> m_Pins;
    std::vector<Core::SubcircuitParameter> m_Parameters;
    std::string m_Text;

    void DrawParameterDefaults();
    void DrawText();
};

} // namespace GUI

#endif // IMCSIM_SPICE_SETTINGS_DIALOG_H
