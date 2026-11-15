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

bool KeepLivePace(std::chrono::steady_clock::time_point &start, double reached, double pace, const RunControl *control);

} // namespace Core

#endif // IMCSIM_LIVE_PACE_H
