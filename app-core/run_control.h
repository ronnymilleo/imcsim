/**
 * @file    run_control.h
 * @brief   How the caller of a long run asks it to stop and learns how far it got, how the run hands over what it
 *          has so far, and how the caller pauses and changes a live run.
 */

#ifndef IMCSIM_RUN_CONTROL_H
#define IMCSIM_RUN_CONTROL_H

#include "live_change.h"
#include <atomic>
#include <functional>
#include <mutex>
#include <vector>

namespace Core {

/**
 * @brief   What a job hands to the calling thread to run, such as storing a result or a part of one.
 */
using Delivery = std::function<void()>;

/**
 * @class   RunControl
 * @brief   A stop request, a progress figure, a mailbox, and the pause and the changes of a live run, shared between
 *          the thread that runs a job and the thread that watches.
 * @details A run checks IsCancelled() between its steps and stops early; it cannot stop inside a single ngspice call.
 *          A stop keeps what the run has done so far and a discard does not: IsDiscarded() tells the run that nobody
 *          wants its result, as when the circuit it simulates was edited. The progress and the flags are atomics, and
 *          the mailbox holds a lock only to move deliveries in and out, so neither side waits for the other. The
 * changes and the pause go the other way: the watching thread sends them and a live transient takes them between its
 * chunks, and holds where it is while paused.
 */
class RunControl {
public:
    // Stop request
    void Cancel();
    bool IsCancelled() const;

    void Discard();
    bool IsDiscarded() const;

    // Pause
    void Pause();
    void Resume();
    bool IsPaused() const;

    // Progress
    void SetProgress(double fraction);
    double GetProgress() const;

    // Mailbox
    void Post(Delivery delivery);
    std::vector<Delivery> TakePosted();

    // Live changes
    void SendChanges(const std::vector<LiveChange> &changes);
    std::vector<LiveChange> TakeChanges();

    // Reuse
    void Reset();

private:
    std::atomic<bool> m_IsCancelled = false;
    std::atomic<bool> m_IsDiscarded = false;
    std::atomic<bool> m_IsPaused = false;
    std::atomic<double> m_Progress = 0.0;
    std::mutex m_MailboxMutex;
    std::vector<Delivery> m_Posted;
    std::vector<LiveChange> m_Changes;
};

} // namespace Core

#endif // IMCSIM_RUN_CONTROL_H
