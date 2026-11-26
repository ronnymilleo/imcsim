/**
 * @file    live_pace.h
 * @brief   Keeps a live transient at its pace against the clock, and holds it while it is paused.
 * @details Any backend that runs a live transient in chunks calls KeepLivePace() between them, so ngspice and Nodalys
 *          follow the clock and the pause the same way.
 */

#ifndef IMCSIM_LIVE_PACE_H
#define IMCSIM_LIVE_PACE_H

#include "simulation/run_control.h"
#include <chrono>

namespace Core {

/// Clock time a chunk of a live run covers: short enough that a change or a stop shows quickly.
inline constexpr double LIVE_CHUNK_SECONDS = 0.03;
/// A live run further behind the clock than one chunk drops the delay instead of making it up; smaller delays, such as
/// the oversleep of a wait, are made up, so they do not add up over a long run.
inline constexpr std::chrono::duration<double> LIVE_CATCH_UP_LIMIT{LIVE_CHUNK_SECONDS};

/**
 * @struct  LivePacer
 * @brief   Incremental wall-clock state of a live transient so speed changes apply only to future simulated time.
 */
struct LivePacer {
    std::chrono::steady_clock::time_point Due = std::chrono::steady_clock::now();
    double LastReached = 0.0;
};

double GetLiveChunkDuration(double pace, const RunControl *control);
void AdvanceLivePace(LivePacer &pacer, double reached, double pace, double speed_multiplier);
void DropLiveDelay(LivePacer &pacer, std::chrono::steady_clock::time_point now);
bool KeepLivePace(LivePacer &pacer, double reached, double pace, const RunControl *control);

} // namespace Core

#endif // IMCSIM_LIVE_PACE_H
