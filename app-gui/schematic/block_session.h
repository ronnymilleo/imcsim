/**
 * @file    block_session.h
 * @brief   The blocks the editor has open on the way down from the schematic, and the settings of the one shown.
 * @details Opening a block, applying it back to the document as one undo step and the checks of its pins and
 * parameters, apart from the window that draws them: it works on the schematics it is given.
 */

#ifndef IMCSIM_BLOCK_SESSION_H
#define IMCSIM_BLOCK_SESSION_H

#include "circuit/components/subcircuit.h"
#include "schematic/schematic.h"
#include "schematic/schematic_file.h"
#include "ui_elements/ui_element.h"
#include <cstddef>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace GUI {

/**
 * @struct  OpenBlock
 * @brief   A block whose contents the editor opened, on the way down from the schematic.
 * @details Pins and Parameters hold its settings while a block inside it is open; the innermost block keeps them in the
 *          session instead.
 */
struct OpenBlock {
    std::string Name;
    std::unique_ptr<Schematic> Content;
    std::vector<SavedSubcircuitPin> Pins;
    std::vector<Core::SubcircuitParameter> Parameters;
};

/**
 * @class   BlockSession
 * @brief   The stack of open blocks of a document, and the name, pins, parameters and error of the settings dialog.
 * @details The root schematic belongs to the caller and must outlive the session. Every open block is a copy of the
 *          block's contents that holds the same definitions as the root, so a part can place any block that does not
 *          contain the one being edited.
 */
class BlockSession {
public:
    explicit BlockSession(Schematic &root);

    // Levels
    bool IsOpen() const;
    const std::vector<OpenBlock> &GetOpenBlocks() const;
    Schematic &GetShown();
    const Schematic &GetShown() const;

    // Placing
    bool CanPlaceBlock(std::string_view name) const;
    std::vector<const SavedSubcircuit *> ListPlaceableBlocks() const;
    void SyncDefinitions();

    // Opening and closing
    static std::optional<std::string> FindBlockName(const UIElement *element);
    const SavedSubcircuit *FindDefinition(const std::string &name) const;
    void RequestCreate();
    bool Open(const std::string &name);
    bool Close(bool apply);
    std::expected<void, std::string> ApplySpiceUpdate(const std::string &name,
                                                      const std::vector<SavedSubcircuitPin> &pins,
                                                      const std::vector<Core::SubcircuitParameter> &parameters);

    // Settings of the dialog
    bool SyncPins();
    bool ApplyPinNames();
    bool CreateBlock();
    void RestoreSettings();
    std::string &GetName();
    std::vector<SavedSubcircuitPin> &GetPins();
    std::vector<Core::SubcircuitParameter> &GetParameters();
    std::string &GetError();
    void RequestDialog();
    bool TakeDialogRequest();

private:
    Schematic &m_Root;
    /// The blocks open, from the outermost to the one shown; empty while the schematic is shown.
    std::vector<OpenBlock> m_OpenBlocks;
    std::vector<SavedSubcircuitPin> m_Pins;
    std::vector<Core::SubcircuitParameter> m_Parameters;
    std::string m_Name;
    std::string m_Error;
    bool m_IsDialogRequested = false;
};

} // namespace GUI

#endif // IMCSIM_BLOCK_SESSION_H
