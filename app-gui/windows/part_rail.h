/**
 * @file    part_rail.h
 * @brief   The column of part buttons beside the schematic, from the part catalog.
 */

#ifndef IMCSIM_PART_RAIL_H
#define IMCSIM_PART_RAIL_H

#include "imgui.h"
#include "schematic/editor_mode.h"
#include "schematic/part_catalog.h"
#include "schematic/preferences.h"
#include "ui_elements/ui_element.h"
#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace GUI {

/**
 * @struct  RailRequest
 * @brief   What the rail asks the editor for this frame: a part or a block to place, or the part search.
 */
struct RailRequest {
    std::optional<EditorMode> Mode;
    bool IsSearchAsked = false;
};

/**
 * @class   PartRail
 * @brief   A narrow column of icon buttons at the left of the canvas, one per entry of the part catalog, with All parts
 *          at the bottom.
 * @details A part of its own places it; a group places the part last picked from it, and its arrow or a right click
 *          opens the list of its parts to the right of the rail. The ready logic blocks work the same way.
 */
class PartRail {
public:
    explicit PartRail(const Preferences &preferences);

    float GetWidth() const;
    RailRequest Draw(const EditorMode &mode, float height);

private:
    /// The symbol standard of the part icons.
    const Preferences &m_Preferences;
    /// Part each group places, the last one picked from its list, indexed like the entries of every section in turn.
    std::vector<Core::ComponentType> m_GroupChoices;
    /// An instance of each ready block, read once, which its button and its row draw as their icon.
    std::vector<std::unique_ptr<UIElement>> m_ReadyBlockIcons;
    /// The ready block the button places, the last one picked from its list.
    std::size_t m_ReadyBlockChoice = 0;

    // Buttons
    std::optional<EditorMode> DrawEntry(const RailEntry &entry, std::size_t index, const EditorMode &mode);
    bool DrawPartButton(Core::ComponentType type, const EditorMode &mode, const char *tooltip);
    std::optional<Core::ComponentType> DrawGroup(const RailEntry &entry, std::size_t index, const EditorMode &mode);
    std::optional<EditorMode> DrawReadyBlocks();
    void DrawReadyBlockIcon(std::size_t index, ImVec2 center) const;
};

} // namespace GUI

#endif // IMCSIM_PART_RAIL_H
