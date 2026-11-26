/**
 * @file    playback.h
 * @brief   The instant of a transient the schematic shows, and the controls that move it.
 */

#ifndef IMCSIM_PLAYBACK_H
#define IMCSIM_PLAYBACK_H

#include "results/transient_window.h"
#include <array>
#include <string>

namespace GUI {

/// Seconds a whole window takes to play at 1x speed, whatever its length.
inline constexpr double PLAYBACK_SECONDS = 5.0;
/// Default speed multiplier for transient playback and live runs.
inline constexpr double DEFAULT_PLAYBACK_SPEED = 1.0;
/// Preset speed multipliers offered in the playback and live speed selectors.
inline constexpr std::array<double, 7> PLAYBACK_SPEED_PRESETS = {0.1, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0};

/**
 * @struct  TransientPlayback
 * @brief   The instant of the last transient shown on the schematic, whether it advances by itself, and the window of
 *          the transient that plays.
 * @details The editor draws its controls and the Output window its playhead; both move the same time, within the window
 *          that the Output window plots, so the schematic plays what the plot shows. It is view state: it is not saved
 *          with the schematic and does not count as a change.
 */
struct TransientPlayback {
    double Time = 0.0;
    bool Playing = false;
    bool Loop = true;
    /// Speed multiplier applied to playback and shared with live runs; preserved across Reset().
    double Speed = DEFAULT_PLAYBACK_SPEED;
    TransientWindow Window;

    void Advance(double elapsed, double from, double to);
    void Reset();
};

std::string FormatPlaybackSpeed(double speed);

} // namespace GUI

#endif // IMCSIM_PLAYBACK_H
