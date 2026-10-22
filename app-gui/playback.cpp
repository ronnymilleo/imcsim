/**
 * @file    playback.cpp
 * @brief   The instant of a transient the schematic shows, and the controls that move it.
 */

#include "playback.h"

#include <algorithm>
#include <cmath>

namespace GUI {

/**
 * @brief   Moves the instant forward while playing.
 * @param[in] elapsed    Real seconds since the last call.
 * @param[in] stop_time  Length of the transient in seconds.
 * @note    The whole run plays in PlaybackSeconds. At the end it starts over when Loop is on, and otherwise stops
 *          there. Outside playing, or without a run, the instant only stays within the run.
 */
void TransientPlayback::Advance(const double elapsed, const double stop_time) {
    if (stop_time <= 0.0) {
        Time = 0.0;
        Playing = false;
        return;
    }
    if (Playing) {
        Time += elapsed * stop_time / PLAYBACK_SECONDS;
    }
    if (Time < stop_time) {
        Time = std::max(Time, 0.0);
        return;
    }
    if (Playing && Loop) {
        Time = std::fmod(Time, stop_time);
        return;
    }
    Time = stop_time;
    Playing = false;
}

/**
 * @brief   Goes back to the start of the run and stops, as for a new result.
 */
void TransientPlayback::Reset() {
    Time = 0.0;
    Playing = false;
}

} // namespace GUI
