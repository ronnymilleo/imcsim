/**
 * @file    run_control.cpp
 * @brief   How the caller of a long run asks it to stop and learns how far it got, how the run hands over what it
 *          has so far, and how the caller pauses and changes a live run.
 */

#include "run_control.h"

#include <utility>

namespace Core {

/**
 * @brief   Asks the run to stop at the end of its current step.
 */
void RunControl::Cancel() {
    m_IsCancelled = true;
}

/**
 * @brief   Tells whether a stop was asked for.
 * @return  True from Cancel() until Reset().
 */
bool RunControl::IsCancelled() const {
    return m_IsCancelled;
}

/**
 * @brief   Says that nobody wants the result of the run, which also stops it.
 */
void RunControl::Discard() {
    m_IsDiscarded = true;
    m_IsCancelled = true;
}

/**
 * @brief   Tells whether the result of the run is unwanted.
 * @return  True from Discard() until Reset().
 */
bool RunControl::IsDiscarded() const {
    return m_IsDiscarded;
}

/**
 * @brief   Asks a live run to hold where it is after its current chunk, keeping its state for Resume().
 * @note    A run that is not live never pauses; a stop request still ends a paused run.
 */
void RunControl::Pause() {
    m_IsPaused = true;
}

/**
 * @brief   Lets a paused live run go on from where it held.
 */
void RunControl::Resume() {
    m_IsPaused = false;
}

/**
 * @brief   Tells whether the run is asked to hold.
 * @return  True from Pause() until Resume() or Reset().
 */
bool RunControl::IsPaused() const {
    return m_IsPaused;
}

/**
 * @brief   Reports how much of the run is done.
 * @param[in] fraction  From 0 for nothing done to 1 for finished.
 */
void RunControl::SetProgress(const double fraction) {
    m_Progress = fraction;
}

/**
 * @brief   Returns how much of the run is done.
 * @return  The last fraction reported, 0 before the first.
 */
double RunControl::GetProgress() const {
    return m_Progress;
}

/**
 * @brief   Leaves a delivery for the watching thread, which runs it when it polls.
 * @param[in] delivery  Work for the watching thread, such as storing the points the run has made so far.
 */
void RunControl::Post(Delivery delivery) {
    const std::lock_guard lock(m_MailboxMutex);
    m_Posted.push_back(std::move(delivery));
}

/**
 * @brief   Takes what the run has posted since the last call.
 * @return  The deliveries in the order they were posted.
 */
std::vector<Delivery> RunControl::TakePosted() {
    const std::lock_guard lock(m_MailboxMutex);
    return std::exchange(m_Posted, {});
}

/**
 * @brief   Hands changes of the circuit to a live run, which applies them before its next chunk.
 * @param[in] changes  New values of netlist elements, in the order they happened.
 * @note    A run that is not live never takes them; Reset() drops what is left.
 */
void RunControl::SendChanges(const std::vector<LiveChange> &changes) {
    const std::lock_guard lock(m_MailboxMutex);
    m_Changes.insert(m_Changes.end(), changes.begin(), changes.end());
}

/**
 * @brief   Takes the changes sent since the last call.
 * @return  The changes in the order they were sent.
 */
std::vector<LiveChange> RunControl::TakeChanges() {
    const std::lock_guard lock(m_MailboxMutex);
    return std::exchange(m_Changes, {});
}

/**
 * @brief   Clears the stop request, the discard, the pause, the progress, the mailbox and the changes, for the next
 * run.
 */
void RunControl::Reset() {
    m_IsCancelled = false;
    m_IsDiscarded = false;
    m_IsPaused = false;
    m_Progress = 0.0;
    const std::lock_guard lock(m_MailboxMutex);
    m_Posted.clear();
    m_Changes.clear();
}

} // namespace Core
