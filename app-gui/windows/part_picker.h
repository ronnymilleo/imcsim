/**
 * @file    part_picker.h
 * @brief   The part search of the command bar: a field whose list of matches hangs below it as the user types.
 */

#ifndef IMCSIM_PART_PICKER_H
#define IMCSIM_PART_PICKER_H

#include "document/ready_blocks.h"
#include "document/schematic_file.h"
#include "editing/editor_mode.h"
#include "preferences.h"
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace GUI {

/**
 * @class   PartPicker
 * @brief   Finds a part or a block by name and asks the editor to start placing it.
 * @details Space over the editor, All parts on the rail or Find a part in the context menu focus the field. Up and Down
 *          move through the matches, Enter or a click picks the highlighted one, and Esc leaves the field. Picking
 * hands the focus back to the editor, so its placing keys work at once.
 */
class PartPicker {
public:
    explicit PartPicker(const Preferences &preferences);

    void RequestOpen();
    void WatchShortcut();
    std::optional<EditorMode> Draw(const std::vector<const SavedSubcircuit *> &blocks, float width);
    bool TakeEditorFocus();

private:
    /**
     * @struct  PartMatches
     * @brief   What the query finds: the parts first, then the ready blocks, then the blocks of the document.
     */
    struct PartMatches {
        std::vector<Core::ComponentType> Parts;
        std::vector<const ReadyBlock *> ReadyBlocks;
        std::vector<const SavedSubcircuit *> Blocks;

        std::size_t GetCount() const;
        std::optional<EditorMode> Get(std::size_t index) const;
    };

    /// The symbol standard of the part icons.
    const Preferences &m_Preferences;
    /// The query typed in the picker.
    std::string m_Query;
    /// The highlighted match.
    std::size_t m_Highlighted = 0;
    /// Whether a menu, the rail or Space asked to focus the field on the next frame.
    bool m_IsOpenRequested = false;
    /// Whether the list of matches shows: while the field is active, and while the mouse is on the list it clicks.
    bool m_IsListShown = false;
    /// Whether the mouse was on the list the last time it showed.
    bool m_IsListHovered = false;
    /// Whether a part was picked, so the editor takes the focus back.
    bool m_IsEditorFocusRequested = false;

    static PartMatches FindMatches(const std::string &query, const std::vector<const SavedSubcircuit *> &blocks);
    std::optional<EditorMode> DrawList(const PartMatches &matches, ImVec2 position, bool moved);
    bool MoveHighlight(std::size_t match_count);
    std::optional<EditorMode> DrawMatches(const PartMatches &matches, bool moved);
    bool DrawMatchRow(const PartMatches &matches, std::size_t index, bool is_highlighted, float width);
};

} // namespace GUI

#endif // IMCSIM_PART_PICKER_H
