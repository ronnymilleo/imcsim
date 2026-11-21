/**
 * @file    fuse_blow_tracker.h
 * @brief   Follows the fuses of the transient on display from frame to frame: which are open, and how strong the flash
 *          of their blow still is.
 */

#ifndef IMCSIM_FUSE_BLOW_TRACKER_H
#define IMCSIM_FUSE_BLOW_TRACKER_H

#include "schematic/connectivity.h"
#include "simulation/simulation_results.h"
#include "ui_elements/ui_element.h"
#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace GUI {

/**
 * @struct  ShownTransient
 * @brief   The transient on display, the version of its data and the instant the playback is at.
 */
struct ShownTransient {
    const Core::Transient &Data;
    /// Version of the data in the result store, which changes as the chunks of a run arrive.
    std::size_t DataVersion = 0;
    double Instant = 0.0;
};

/**
 * @struct  FuseDisplay
 * @brief   What a fuse shows this frame.
 */
struct FuseDisplay {
    bool IsBlown = false;
    /// Strength of the flash that marks the blow, from 0 to 1.
    float FlashLevel = 0.0f;

    bool operator==(const FuseDisplay &) const = default;
};

/**
 * @class   FuseBlowTracker
 * @brief   Finds when each fuse of the transient on display blew, and starts the flash of a fuse the frame it is seen
 *          going from whole to open.
 * @details The blow times are read once per version of the transient data. A result that arrives with a fuse already
 *          blown does not flash, and a flash lasts FUSE_FLASH_SECONDS of the screen clock, whatever the speed of the
 *          playback.
 */
class FuseBlowTracker {
public:
    std::vector<std::optional<FuseDisplay>> Update(const std::vector<std::unique_ptr<UIElement>> &elements,
                                                   const Connectivity &connectivity,
                                                   const ShownTransient *shown,
                                                   double now);
    const std::vector<std::optional<double>> &GetBlowTimes() const;

private:
    /// Blow times of the fuses of the transient shown, for the version of its data they were read from.
    std::vector<std::optional<double>> m_BlowTimes;
    std::optional<std::size_t> m_BlowTimesVersion;
    /// Whether each element was a fuse shown open in the last frame; empty when that frame showed no transient.
    std::vector<bool> m_WasBlown;
    /// When the screen clock last saw each fuse blow, by element.
    std::vector<double> m_BlownSince;

    void ReadBlowTimes(const std::vector<std::unique_ptr<UIElement>> &elements,
                       const Connectivity &connectivity,
                       const ShownTransient *shown);
};

} // namespace GUI

#endif // IMCSIM_FUSE_BLOW_TRACKER_H
