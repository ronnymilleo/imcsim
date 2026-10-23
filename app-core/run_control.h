/**
 * @file    run_control.h
 * @brief   How the caller of a long run asks it to stop and learns how far it got, and how the run hands over what it
 *          has so far.
 */

#ifndef IMCSIM_RUN_CONTROL_H
#define IMCSIM_RUN_CONTROL_H

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
 * @brief   A stop request, a progress figure and a mailbox shared between the thread that runs a job and the thread
 *          that watches.
 * @details A run checks IsCancelled() between its steps and stops early; it cannot stop inside a single ngspice call.
 *          A stop keeps what the run has done so far and a discard does not: IsDiscarded() tells the run that nobody
 *          wants its result, as when the circuit it simulates was edited. The progress and the flags are atomics, and
 *          the mailbox holds a lock only to move deliveries in and out, so neither side waits for the other.
 */
class RunControl {
public:
    // Stop request
    void Cancel();
    bool IsCancelled() const;

    void Discard();
    bool IsDiscarded() const;

    // Progress
    void SetProgress(double fraction);
    double GetProgress() const;

    // Mailbox
    void Post(Delivery delivery);
    std::vector<Delivery> TakePosted();

    // Reuse
    void Reset();

private:
    std::atomic<bool> m_IsCancelled = false;
    std::atomic<bool> m_IsDiscarded = false;
    std::atomic<double> m_Progress = 0.0;
    std::mutex m_MailboxMutex;
    std::vector<Delivery> m_Posted;
};

} // namespace Core

#endif // IMCSIM_RUN_CONTROL_H
