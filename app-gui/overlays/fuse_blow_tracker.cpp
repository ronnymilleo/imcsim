/**
 * @file    fuse_blow_tracker.cpp
 * @brief   Follows the fuses of the transient on display from frame to frame: which are open, and how strong the flash
 *          of their blow still is.
 */

#include "overlays/fuse_blow_tracker.h"

#include "overlays/fuse_flash.h"
#include "results/fuse_readings.h"
#include <utility>

namespace GUI {

/**
 * @brief   Works out what each fuse shows this frame.
 * @param[in] elements      The parts of the schematic.
 * @param[in] connectivity  The nodes of the schematic, which the blow times are read on.
 * @param[in] shown         The transient on display, or null when the schematic shows an operating point or nothing,
 *                          which see every fuse whole.
 * @param[in] now           The screen clock, in seconds, which times the flash.
 * @return  One entry per element: what a fuse shows, and none for the other parts.
 */
std::vector<std::optional<FuseDisplay>> FuseBlowTracker::Update(const std::vector<std::unique_ptr<UIElement>> &elements,
                                                                const Connectivity &connectivity,
                                                                const ShownTransient *shown,
                                                                const double now) {
    ReadBlowTimes(elements, connectivity, shown);
    const bool has_previous_frame = shown != nullptr && m_WasBlown.size() == elements.size();
    std::vector<bool> is_blown(elements.size(), false);
    m_BlownSince.resize(elements.size(), 0.0);
    std::vector<std::optional<FuseDisplay>> displays(elements.size());
    for (std::size_t index = 0; index < elements.size(); ++index) {
        if (elements[index]->GetComponent().GetType() != Core::ComponentType::FUSE) {
            continue;
        }
        is_blown[index] = shown != nullptr && IsFuseBlownAt(m_BlowTimes[index], shown->Instant);
        if (is_blown[index] && has_previous_frame && !m_WasBlown[index]) {
            m_BlownSince[index] = now;
        }
        displays[index] = FuseDisplay{
            .IsBlown = is_blown[index],
            .FlashLevel = is_blown[index] ? GetFuseFlashLevel(now - m_BlownSince[index]) : 0.0f,
        };
    }
    m_WasBlown = shown != nullptr ? std::move(is_blown) : std::vector<bool>();
    return displays;
}

/**
 * @brief   When each fuse of the transient on display blew.
 * @return  The blow times by the index of the element, as of the last Update(); none for a fuse still whole and for the
 *          other parts, and empty while no transient is on display.
 */
const std::vector<std::optional<double>> &FuseBlowTracker::GetBlowTimes() const {
    return m_BlowTimes;
}

/**
 * @brief   Reads the blow times again when the transient data or the parts changed, and forgets them when no
 *          transient is on display.
 */
void FuseBlowTracker::ReadBlowTimes(const std::vector<std::unique_ptr<UIElement>> &elements,
                                    const Connectivity &connectivity,
                                    const ShownTransient *shown) {
    if (shown == nullptr) {
        m_BlowTimes.clear();
        m_BlowTimesVersion.reset();
    } else if (m_BlowTimesVersion != shown->DataVersion || m_BlowTimes.size() != elements.size()) {
        m_BlowTimes = FindFuseBlowTimes(elements, connectivity, shown->Data);
        m_BlowTimesVersion = shown->DataVersion;
    }
}

} // namespace GUI
