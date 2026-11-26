/**
 * @file    live_pace.cpp
 * @brief   Keeps a live transient at its pace against the clock, and holds it while it is paused.
 */

#include "simulation/live_pace.h"

#include <algorithm>
#include <thread>

namespace Core {

namespace {

/// A live run that is ahead of the clock waits in slices this long, so a stop waits little.
constexpr std::chrono::duration<double> PACE_WAIT_SLICE{0.01};

bool IsCancelled(const RunControl *control) {
    return control != nullptr && control->IsCancelled();
}

/**
 * @brief   Holds a paused live run until it is resumed or asked to stop, and returns how long it held, by which the
 *          due clock moves on.
 */
std::chrono::steady_clock::duration WaitWhilePaused(const RunControl *control) {
    const auto start = std::chrono::steady_clock::now();
    while (control != nullptr && control->IsPaused() && !IsCancelled(control)) {
        std::this_thread::sleep_for(PACE_WAIT_SLICE);
    }
    return std::chrono::steady_clock::now() - start;
}

/**
 * @brief   Waits until the clock catches up with the pacer's due time, shifting it when paused and returning early on
 *          cancel.
 */
void WaitForClock(LivePacer &pacer, const RunControl *control) {
    while (!IsCancelled(control)) {
        if (control != nullptr && control->IsPaused()) {
            pacer.Due += WaitWhilePaused(control);
            continue;
        }
        const std::chrono::duration<double> left = pacer.Due - std::chrono::steady_clock::now();
        if (left <= std::chrono::duration<double>::zero()) {
            return;
        }
        std::this_thread::sleep_for(std::min(left, PACE_WAIT_SLICE));
    }
}

} // namespace

/**
 * @brief   Computes the simulated time span of one live chunk scaled by the current speed multiplier.
 * @param[in] pace     Base simulated seconds per wall-clock second.
 * @param[in] control  Run control providing the speed multiplier, or null for 1x.
 * @return  Chunk duration in simulated seconds.
 */
double GetLiveChunkDuration(const double pace, const RunControl *control) {
    const double speed = control != nullptr ? control->GetSpeedMultiplier() : 1.0;
    return pace * ClampSpeedMultiplier(speed) * LIVE_CHUNK_SECONDS;
}

/**
 * @brief   Advances the pacer's wall-clock target by the simulated time covered since the last chunk at the given
 * speed.
 * @param[in,out] pacer             Incremental pacer state updated in place.
 * @param[in]     reached           Simulated time reached at the end of the chunk, in seconds.
 * @param[in]     pace              Base simulated seconds per wall-clock second, above 0.
 * @param[in]     speed_multiplier  Speed multiplier for this chunk, clamped strictly above 0.
 */
void AdvanceLivePace(LivePacer &pacer, const double reached, const double pace, const double speed_multiplier) {
    const double delta_sim = std::max(0.0, reached - pacer.LastReached);
    pacer.LastReached = std::max(pacer.LastReached, reached);
    const double effective_pace = pace * ClampSpeedMultiplier(speed_multiplier);
    if (effective_pace <= 0.0 || delta_sim <= 0.0) {
        return;
    }
    pacer.Due += std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(delta_sim / effective_pace));
}

/**
 * @brief   Drops accumulated wall-clock delay beyond LIVE_CATCH_UP_LIMIT so the run does not rush to catch up.
 * @param[in,out] pacer  Incremental pacer state whose due time shifts forward when behind.
 * @param[in]     now    Current wall-clock time.
 */
void DropLiveDelay(LivePacer &pacer, const std::chrono::steady_clock::time_point now) {
    if (const auto late = now - pacer.Due; late > LIVE_CATCH_UP_LIMIT) {
        pacer.Due += late;
    }
}

/**
 * @brief   Keeps a live run at its current speed once it reached a simulated time: advances the incremental target,
 * waits for the clock, drops excessive lateness and holds while paused.
 * @param[in,out] pacer    Incremental pacer state tracking the due clock and last reached simulated time.
 * @param[in]     reached  Simulated time the run has reached, in seconds.
 * @param[in]     pace     Base simulated seconds the run covers per second of the clock, above 0.
 * @param[in]     control  Where to look for speed multiplier, stop request and pause, or null.
 * @return  True when the run goes on, false when it was asked to stop.
 */
bool KeepLivePace(LivePacer &pacer, const double reached, const double pace, const RunControl *control) {
    const double speed = control != nullptr ? control->GetSpeedMultiplier() : 1.0;
    AdvanceLivePace(pacer, reached, pace, speed);
    WaitForClock(pacer, control);
    DropLiveDelay(pacer, std::chrono::steady_clock::now());
    pacer.Due += WaitWhilePaused(control);
    return !IsCancelled(control);
}

} // namespace Core
