/**
 * @file    run_control.h
 * @brief   How the caller of a long run asks it to stop and learns how far it got.
 */

#ifndef IMCSIM_RUN_CONTROL_H
#define IMCSIM_RUN_CONTROL_H

#include <atomic>

namespace Core {

/**
 * @class   RunControl
 * @brief   A stop request and a progress figure shared between the thread that runs a job and the thread that watches.
 * @details Both sides only touch atomics, so neither waits for the other. A run checks IsCancelled() between its steps
 *          and stops early; it cannot stop inside a single ngspice call.
 */
class RunControl {
public:
    // Stop request
    void Cancel();
    bool IsCancelled() const;

    // Progress
    void SetProgress(double fraction);
    double GetProgress() const;

    // Reuse
    void Reset();

private:
    std::atomic<bool> m_IsCancelled = false;
    std::atomic<double> m_Progress = 0.0;
};

} // namespace Core

#endif // IMCSIM_RUN_CONTROL_H
