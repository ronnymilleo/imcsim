/**
 * @file    part_catalog.h
 * @brief   How the placeable parts are grouped on the part rail, the one list the rail and the part search share.
 */

#ifndef IMCSIM_PART_CATALOG_H
#define IMCSIM_PART_CATALOG_H

#include "circuit/components/component.h"
#include <span>
#include <string_view>

namespace GUI {

/**
 * @struct  RailEntry
 * @brief   One button of the part rail: a part of its own, a group of related parts that share a button, or the ready
 *          logic blocks.
 * @details A group button places the part last picked from its list, the first one until then.
 */
struct RailEntry {
    std::string_view Name;
    std::span<const Core::ComponentType> Parts;
    /// The ready logic blocks, which come from their own list rather than Parts.
    bool IsReadyBlocks = false;

    bool IsGroup() const;
};

std::span<const std::span<const RailEntry>> GetRailSections();

} // namespace GUI

#endif // IMCSIM_PART_CATALOG_H
