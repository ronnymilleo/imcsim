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
/// A live run further behind the clock than one chunk drops the delay instead of making it up; smaller delays, such as
/// the oversleep of a wait, are made up, so they do not add up over a long run.
constexpr std::chrono::duration<double> LIVE_CATCH_UP_LIMIT{LIVE_CHUNK_SECONDS};

bool IsCancelled(const RunControl *control) {
    return control != nullptr && control->IsCancelled();
}

/**
 * @brief   Waits until the clock catches up with the simulated time a live run has reached, unless the run is asked to
 *          stop.
 */
void WaitForClock(const std::chrono::steady_clock::time_point due, const RunControl *control) {
    while (!IsCancelled(control)) {
        const std::chrono::duration<double> left = due - std::chrono::steady_clock::now();
        if (left <= std::chrono::duration<double>::zero()) {
            return;
        }
        std::this_thread::sleep_for(std::min(left, PACE_WAIT_SLICE));
    }
}

/**
 * @brief   Holds a paused live run until it is resumed or asked to stop, and returns how long it held, by which the
 *          clock the run keeps pace with moves on.
 */
std::chrono::steady_clock::duration WaitWhilePaused(const RunControl *control) {
    const auto start = std::chrono::steady_clock::now();
    while (control != nullptr && control->IsPaused() && !IsCancelled(control)) {
        std::this_thread::sleep_for(PACE_WAIT_SLICE);
    }
    return std::chrono::steady_clock::now() - start;
}

} // namespace

/**
 * @brief   Keeps a live run at its pace once it reached a simulated time: waits for the clock, then holds while it is
 *          paused, moving the start of its clock by the time paused so it never catches up.
 * @details A run that fell more than a chunk behind, because the circuit was slow for a while, moves the start by its
 *          delay too, so it goes on at its pace instead of hurrying to make the delay up once the circuit gets easy.
 * @param[in,out] start    When the clock of the run started, moved later by the time paused and the delays dropped.
 * @param[in]     reached  Simulated time the run has reached, in seconds.
 * @param[in]     pace     Simulated seconds the run covers per second of the clock, above 0.
 * @param[in]     control  Where to look for a stop request and the pause, or null.
 * @return  True when the run goes on, false when it was asked to stop.
 */
bool KeepLivePace(std::chrono::steady_clock::time_point &start, const double reached, const double pace,
                  const RunControl *control) {
    const auto due = start + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                 std::chrono::duration<double>(reached / pace));
    WaitForClock(due, control);
    if (const auto late = std::chrono::steady_clock::now() - due; late > LIVE_CATCH_UP_LIMIT) {
        start += late;
    }
    start += WaitWhilePaused(control);
    return !IsCancelled(control);
}

} // namespace Core
