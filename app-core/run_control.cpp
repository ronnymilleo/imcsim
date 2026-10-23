/**
 * @file    run_control.cpp
 * @brief   How the caller of a long run asks it to stop and learns how far it got, and how the run hands over what it
 *          has so far.
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
 * @brief   Clears the stop request, the discard, the progress and the mailbox, for the next run.
 */
void RunControl::Reset() {
    m_IsCancelled = false;
    m_IsDiscarded = false;
    m_Progress = 0.0;
    const std::lock_guard lock(m_MailboxMutex);
    m_Posted.clear();
}

} // namespace Core
