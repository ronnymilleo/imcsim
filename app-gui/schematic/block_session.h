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
 * @struct  BlockSettings
 * @brief   The name, pins and parameters of a block, as the settings dialog edits them.
 */
struct BlockSettings {
    std::string Name;
    std::vector<SavedSubcircuitPin> Pins;
    std::vector<Core::SubcircuitParameter> Parameters;
};

/**
 * @struct  OpenBlock
 * @brief   A block whose contents the editor opened, on the way down from the schematic.
 * @details Settings are those of this block, kept while the editor goes into a block inside it, so they are still
 *          there when it comes back. The name is the one of the definition and does not change while it is open.
 */
struct OpenBlock {
    std::unique_ptr<Schematic> Content;
    BlockSettings Settings;
};

/**
 * @class   BlockSession
 * @brief   The stack of open blocks of a document, and the settings and error the dialog shows.
 * @details The root schematic belongs to the caller and must outlive the session. Every open block is a copy of the
 *          block's contents that holds the same definitions as the root, so a part can place any block that does not
 *          contain the one being edited. The dialog edits the settings of the innermost open block, or those of the
 *          block to make from the selection while the schematic is shown.
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
    [[nodiscard]] std::expected<void, std::string> Open(const std::string &name);
    [[nodiscard]] std::expected<void, std::string> ApplyAndClose();
    [[nodiscard]] std::expected<void, std::string> DiscardAndClose();
    std::expected<void, std::string> ApplySpiceUpdate(const std::string &name,
                                                      const std::vector<SavedSubcircuitPin> &pins,
                                                      const std::vector<Core::SubcircuitParameter> &parameters);

    // Settings of the dialog
    [[nodiscard]] std::expected<void, std::string> SyncPins();
    [[nodiscard]] std::expected<void, std::string> ApplyPinNames();
    [[nodiscard]] std::expected<void, std::string> CreateBlock();
    void RestoreSettings();
    std::string &GetName();
    std::vector<SavedSubcircuitPin> &GetPins();
    std::vector<Core::SubcircuitParameter> &GetParameters();
    const std::string &GetError() const;
    void ShowError(std::string reason);
    void ClearError();
    void ReportError(std::string reason);
    void RequestDialog();
    bool TakeDialogRequest();

private:
    Schematic &m_Root;
    /// The blocks open, from the outermost to the one shown; empty while the schematic is shown.
    std::vector<OpenBlock> m_OpenBlocks;

    // Settings of the dialog
    /// The settings of the block to make from the selection, which the dialog shows while no block is open.
    BlockSettings m_NewBlock;
    std::string m_Error;
    bool m_IsDialogRequested = false;

    std::expected<void, std::string> ApplyInnermost();
    void LeaveInnermost();
    BlockSettings &GetShownSettings();
};

} // namespace GUI

#endif // IMCSIM_BLOCK_SESSION_H
