/**
 * @file    part_picker.h
 * @brief   The list of every part that Space opens over the editor, filtered by what is typed.
 */

#ifndef IMCSIM_PART_PICKER_H
#define IMCSIM_PART_PICKER_H

#include "document/schematic_file.h"
#include "editing/editor_mode.h"
#include "preferences.h"
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace GUI {

/**
 * @class   PartPicker
 * @brief   Finds a part or a block by name and asks the editor to start placing it.
 * @details Up and Down move through the matches, and Enter or a click picks the highlighted one.
 */
class PartPicker {
public:
    explicit PartPicker(const Preferences &preferences);

    void RequestOpen();
    std::optional<PlacingMode> Draw(std::span<const SavedSubcircuit> blocks);

private:
    /**
     * @struct  PartMatches
     * @brief   What the query finds: the parts first, then the blocks of the document.
     */
    struct PartMatches {
        std::vector<Core::ComponentType> Parts;
        std::vector<const SavedSubcircuit *> Blocks;

        std::size_t GetCount() const;
        PlacingMode Get(std::size_t index) const;
    };

    /// The symbol standard of the part icons.
    const Preferences &m_Preferences;
    /// The query typed in the picker.
    std::string m_Query;
    /// The highlighted match.
    std::size_t m_Highlighted = 0;
    /// Whether a menu asked to open the picker on the next frame.
    bool m_IsOpenRequested = false;

    static PartMatches FindMatches(const std::string &query, std::span<const SavedSubcircuit> blocks);
    void OpenIfAsked();
    bool MoveHighlight(std::size_t match_count);
    std::optional<PlacingMode> DrawMatches(const PartMatches &matches, bool moved);
};

} // namespace GUI

#endif // IMCSIM_PART_PICKER_H
