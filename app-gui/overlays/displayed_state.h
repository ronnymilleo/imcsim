/**
 * @file    displayed_state.h
 * @brief   Resolves what the schematic shows each frame: the last operating point or the transient at the instant of
 * the playback, the heat scale its colors use and what the fuses and logic displays read.
 */

#ifndef IMCSIM_DISPLAYED_STATE_H
#define IMCSIM_DISPLAYED_STATE_H

#include "base/logic_state.h"
#include "overlays/fuse_blow_tracker.h"
#include "results/circuit_snapshot.h"
#include "results/playback.h"
#include "results/point_window.h"
#include "results/result_store.h"
#include "schematic/schematic.h"
#include "schematic/simulation_settings.h"
#include "simulation/simulation_results.h"
#include <cstddef>
#include <optional>
#include <vector>

namespace GUI {

/**
 * @struct  DisplayedState
 * @brief   The voltages, currents and powers the schematic shows this frame, and the scale their colors use.
 * @details It is the last operating point, or the last transient read at the instant of the playback; the drawing code
 *          does not tell them apart. Time is set for a transient only.
 */
struct DisplayedState {
    Core::OperatingPoint Point;
    HeatScale Scale;
    std::vector<ComponentPower> Powers;
    std::optional<double> Time;
    /// When each fuse blew in the transient on display, by the index of its element; none for a fuse still whole and
    /// for the other parts.
    std::vector<std::optional<double>> FuseBlowTimes;
    /// What the terminals of each logic display read, by the index of its element, in terminal order; empty for the
    /// other parts, and none for a terminal whose node the result has no voltage for.
    std::vector<std::vector<std::optional<LogicState>>> LogicReadings;
};

/**
 * @enum    ShownResult
 * @brief   Which result the schematic shows.
 */
enum class ShownResult {
    NONE,
    OPERATING_POINT,
    TRANSIENT
};

/**
 * @struct  ScaleKey
 * @brief   What the heat scale of a transient is worked out for: a version of its data and the window of points that
 *          plays.
 */
struct ScaleKey {
    std::size_t DataVersion = 0;
    PointWindow Window;

    bool operator==(const ScaleKey &) const = default;
};

/**
 * @struct  TransientScaleCache
 * @brief   The heat scale of a transient, kept with what it was worked out for and when.
 */
struct TransientScaleCache {
    /// What the scale was worked out for; none before the first one.
    std::optional<ScaleKey> Key;
    /// When the scale was last worked out, on the screen clock, in seconds.
    double LastTime = 0.0;
    HeatScale Scale;
};

// Policies
ShownResult ChooseShownResult(bool has_operating_point, bool has_transient_points, std::optional<Analysis> last_run);
bool IsScaleDue(const TransientScaleCache &cache, const ScaleKey &wanted, bool is_complete, double now);

/**
 * @class   DisplayedStateResolver
 * @brief   Works out once per frame what the schematic shows, keeping the heat scale of a transient and the state of
 * its fuses from frame to frame.
 * @details The heat scale of a transient goes over the window of points that plays, so a value keeps its color while
 *          the time moves, and is kept until the result or the window changes.
 */
class DisplayedStateResolver {
public:
    explicit DisplayedStateResolver(const TransientPlayback &playback);

    // Resolving
    void Resolve(Schematic &schematic, const ResultStore &results, std::optional<Analysis> last_run);
    void Reset();

    // What the schematic shows
    const std::optional<DisplayedState> &GetDisplayed() const;
    const std::vector<std::optional<FuseDisplay>> &GetFuseDisplays() const;

private:
    /// The instant of the transient shown, and the window of points the scale goes over.
    const TransientPlayback &m_Playback;
    /// What the schematic shows, resolved at the start of each frame.
    std::optional<DisplayedState> m_Displayed;
    TransientScaleCache m_TransientScale;
    FuseBlowTracker m_Fuses;
    /// What each fuse shows this frame, by element; none for the other parts.
    std::vector<std::optional<FuseDisplay>> m_FuseDisplays;

    std::optional<DisplayedState> BuildDisplayedState(Schematic &schematic,
                                                      const ShownTransient *transient,
                                                      const std::optional<Core::OperatingPoint> &operating_point,
                                                      double now);
    const HeatScale &RefreshTransientScale(const ShownTransient &transient, Schematic &schematic, double now);
};

} // namespace GUI

#endif // IMCSIM_DISPLAYED_STATE_H
