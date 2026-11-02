/**
 * @file    playback.h
 * @brief   The instant of a transient the schematic shows, and the controls that move it.
 */

#ifndef IMCSIM_PLAYBACK_H
#define IMCSIM_PLAYBACK_H

#include "transient_window.h"

namespace GUI {

// Seconds a whole window takes to play, whatever its length
inline constexpr double PLAYBACK_SECONDS = 5.0;

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
    TransientWindow Window;

    void Advance(double elapsed, double from, double to);
    void Reset();
};

} // namespace GUI

#endif // IMCSIM_PLAYBACK_H
