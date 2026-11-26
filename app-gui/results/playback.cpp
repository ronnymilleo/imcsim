/**
 * @file    playback.cpp
 * @brief   The instant of a transient the schematic shows, and the controls that move it.
 */

#include "results/playback.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace GUI {

/**
 * @brief   Moves the instant forward while playing.
 * @param[in] elapsed  Real seconds since the last call.
 * @param[in] from     Start of the window to play, in seconds.
 * @param[in] to       End of the window, in seconds.
 * @note    The whole window plays in PLAYBACK_SECONDS / Speed. At the end it starts over from its start when Loop is
 *          on, and otherwise stops there. Outside playing, or without a window, the instant only stays within it.
 */
void TransientPlayback::Advance(const double elapsed, const double from, const double to) {
    const double span = to - from;
    if (span <= 0.0) {
        Time = from;
        Playing = false;
        return;
    }
    if (Playing) {
        Time += elapsed * std::max(Speed, 0.0) * span / PLAYBACK_SECONDS;
    }
    if (Time < to) {
        Time = std::max(Time, from);
        return;
    }
    if (Playing && Loop) {
        Time = from + std::fmod(Time - from, span);
        return;
    }
    Time = to;
    Playing = false;
}

/**
 * @brief   Goes back to the start of the run and stops, as for a new result, preserving Speed.
 */
void TransientPlayback::Reset() {
    Time = 0.0;
    Playing = false;
}

/**
 * @brief   Formats a speed multiplier as a compact label such as "1x" or "0.25x".
 * @param[in] speed  Speed multiplier to format.
 * @return  Label ending in "x".
 */
std::string FormatPlaybackSpeed(const double speed) {
    return std::format("{:g}x", speed);
}

} // namespace GUI
