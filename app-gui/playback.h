/**
 * @file    playback.h
 * @brief   The instant of a transient the schematic shows, and the controls that move it.
 */

#ifndef IMCSIM_PLAYBACK_H
#define IMCSIM_PLAYBACK_H

namespace GUI {

// Seconds a whole transient takes to play, whatever its length
inline constexpr double PlaybackSeconds = 5.0;

/**
 * @struct  TransientPlayback
 * @brief   The instant of the last transient shown on the schematic, and whether it advances by itself.
 * @details The editor draws its controls and the Output window its playhead; both move the same time. It is view
 *          state: it is not saved with the schematic and does not count as a change.
 */
struct TransientPlayback {
    double Time = 0.0;
    bool Playing = false;
    bool Loop = true;

    void Advance(double elapsed, double stop_time);
    void Reset();
};

} // namespace GUI

#endif // IMCSIM_PLAYBACK_H
