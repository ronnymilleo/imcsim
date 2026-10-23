/**
 * @file    run_control.cpp
 * @brief   How the caller of a long run asks it to stop and learns how far it got.
 */

#include "run_control.h"

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
 * @brief   Clears the stop request and the progress, for the next run.
 */
void RunControl::Reset() {
    m_IsCancelled = false;
    m_Progress = 0.0;
}

} // namespace Core
