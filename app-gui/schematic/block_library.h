/**
 * @file    block_library.h
 * @brief   The block definitions and library models a schematic keeps, and the blocks as the simulator takes them.
 */

#ifndef IMCSIM_BLOCK_LIBRARY_H
#define IMCSIM_BLOCK_LIBRARY_H

#include "circuit/library_model.h"
#include "circuit/subcircuit.h"
#include "schematic/schematic_file.h"
#include "ui_elements/ui_element.h"
#include "ui_elements/ui_wire.h"
#include <cstddef>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace GUI {

/**
 * @struct  DefinitionChange
 * @brief   A definition whose pins or parameters changed, and how its old pins map onto the new ones, so the instances
 *          outside the library can follow it as those inside already did.
 */
struct DefinitionChange {
    /// The definition as it is now, kept by the library; never null, and valid until the library changes again.
    const SavedSubcircuit *Definition = nullptr;
    std::vector<std::string> OldNames; ///< The names the pins had before, in their old order.
    /// For each old pin, the index of the new pin it became, or none when it went away.
    std::vector<std::optional<std::size_t>> Matches;
};

/**
 * @struct  MergeResult
 * @brief   What merging definitions or models into the library did.
 */
struct MergeResult {
    /// The name each one has here, in the order they came: its own, or the free name it was given.
    std::vector<std::string> Names;
    bool IsAnyAdded = false; ///< Whether any was added, so the document changed.
};

/**
 * @struct  BlockCopy
 * @brief   A copy of a drawn block to edit, with copies of the other blocks its parts may place.
 */
struct BlockCopy {
    SavedSubcircuit Definition;
    std::vector<SavedSubcircuit> OtherDefinitions;
};

/**
 * @class   BlockLibrary
 * @brief   The blocks of a schematic and the device models it imported, with the rules for adding and changing them.
 * @details The blocks are kept as drawn, or as SPICE text, and built into the definitions the simulator takes on first
 *          use after a change. An instance of a block that changed is brought up to date inside the other blocks here;
 *          the owner brings its own instances up to date from the DefinitionChange it gets back, and counts the change
 *          as an edit.
 */
class BlockLibrary {
public:
    BlockLibrary() = default;
    BlockLibrary(std::vector<SavedSubcircuit> subcircuits, std::vector<Core::LibraryModel> models);

    // Content
    const std::vector<SavedSubcircuit> &GetSubcircuits() const;
    const std::vector<Core::LibraryModel> &GetLibraryModels() const;

    // Blocks
    [[nodiscard]] std::expected<void, std::string> CheckNewName(const std::string &name) const;
    void AddSubcircuit(SavedSubcircuit definition);
    [[nodiscard]] std::expected<BlockCopy, std::string> CopyForEditing(const std::string &name) const;
    [[nodiscard]] std::expected<DefinitionChange, std::string>
    UpdateDrawn(const std::string &name,
                const std::vector<std::unique_ptr<UIElement>> &elements,
                const std::vector<UIWire> &wires,
                const std::vector<SavedSubcircuitPin> &pins,
                const std::vector<Core::SubcircuitParameter> &parameters);
    [[nodiscard]] std::expected<DefinitionChange, std::string>
    UpdateSpice(const std::string &name,
                const std::vector<SavedSubcircuitPin> &pins,
                const std::vector<Core::SubcircuitParameter> &parameters);
    MergeResult MergeSubcircuits(std::vector<SavedSubcircuit> definitions,
                                 std::vector<std::unique_ptr<UIElement>> &instances);

    // Library models
    MergeResult MergeLibraryModels(std::vector<Core::LibraryModel> models,
                                   std::vector<std::unique_ptr<UIElement>> &elements,
                                   std::vector<SavedSubcircuit> &definitions);

    // Copies
    void CopyFrom(const BlockLibrary &library, std::string_view left_out);

    // Simulation
    const std::vector<std::shared_ptr<Core::SubcircuitDefinition>> &GetBuiltDefinitions();

private:
    std::vector<SavedSubcircuit> m_Subcircuits;
    /// Device models imported from libraries, which diodes and transistors anywhere in the document can use.
    std::vector<Core::LibraryModel> m_LibraryModels;
    /// The blocks as the simulator takes them, built once from m_Subcircuits and dropped when it changes.
    std::optional<std::vector<std::shared_ptr<Core::SubcircuitDefinition>>> m_BuiltSubcircuits;

    DefinitionChange ApplyPinChange(SavedSubcircuit &definition,
                                    const std::vector<SavedSubcircuitPin> &pins,
                                    const std::vector<Core::SubcircuitParameter> &parameters,
                                    std::vector<std::optional<std::size_t>> matches);
};

} // namespace GUI

#endif // IMCSIM_BLOCK_LIBRARY_H
