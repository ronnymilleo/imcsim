/**
 * @file    simulation_service.cpp
 * @brief   Runs simulations on a worker thread, so the interface keeps drawing while they take their time.
 */

#include "simulation_service.h"

#include <utility>

namespace Core {

/**
 * @brief   Stops the job that is running, if any, and joins the worker.
 * @note    Waits for the step in progress to end, as a stop always does.
 */
SimulationService::~SimulationService() {
    {
        const std::lock_guard lock(m_Mutex);
        m_IsQuitting = true;
        m_Control.Cancel();
    }
    m_Wake.notify_all();
    if (m_Worker.joinable()) {
        m_Worker.join();
    }
}

/**
 * @brief   Hands a job to the worker.
 * @param[in] job  Work to run on the worker thread; it must not touch anything the calling thread edits.
 * @return  True when the job was accepted; false while another job is running or waits to be polled.
 */
bool SimulationService::Submit(Job job) {
    {
        const std::lock_guard lock(m_Mutex);
        if (m_State != State::IDLE) {
            return false;
        }
        m_Job = std::move(job);
        m_Control.Reset();
        m_State = State::QUEUED;
        if (!m_Worker.joinable()) {
            m_Worker = std::thread(&SimulationService::Work, this);
        }
    }
    m_Wake.notify_all();
    return true;
}

/**
 * @brief   Asks the job to stop, and drops its result.
 * @note    A job checks between its steps, so a long single step ends first; IsBusy() tells when the worker is free
 *          again. A result that is done but not yet polled is dropped at once.
 */
void SimulationService::Cancel() {
    const std::lock_guard lock(m_Mutex);
    if (m_State == State::DONE) {
        m_Delivery = nullptr;
        m_State = State::IDLE;
    } else if (m_State != State::IDLE) {
        m_Control.Cancel();
    }
}

/**
 * @brief   Runs the delivery of a finished job on the calling thread.
 * @return  True when a delivery ran; false while the job still runs, when none was submitted, and when it was stopped.
 * @note    Call it every frame from the thread that owns what the delivery changes.
 */
bool SimulationService::Poll() {
    Delivery delivery;
    {
        const std::lock_guard lock(m_Mutex);
        if (m_State != State::DONE) {
            return false;
        }
        delivery = std::move(m_Delivery);
        m_Delivery = nullptr;
        m_State = State::IDLE;
    }
    if (!delivery) {
        return false;
    }
    delivery();
    return true;
}

/**
 * @brief   Tells whether a job is queued, running, or done and not yet polled.
 * @return  True until Poll() takes the result or Cancel() drops it.
 */
bool SimulationService::IsBusy() const {
    const std::lock_guard lock(m_Mutex);
    return m_State != State::IDLE;
}

/**
 * @brief   Tells whether the job in progress was asked to stop.
 * @return  True from Cancel() until the worker finishes the job.
 */
bool SimulationService::IsCancelling() const {
    const std::lock_guard lock(m_Mutex);
    return (m_State == State::QUEUED || m_State == State::RUNNING) && m_Control.IsCancelled();
}

/**
 * @brief   Returns how much of the job in progress is done.
 * @return  A fraction from 0 to 1; 0 when no job runs.
 */
double SimulationService::GetProgress() const {
    return m_Control.GetProgress();
}

// The worker waits for a job, runs it without holding the lock, and leaves its delivery for Poll(). A job that was
// stopped delivers nothing
void SimulationService::Work() {
    std::unique_lock lock(m_Mutex);
    while (true) {
        m_Wake.wait(lock, [this] { return m_IsQuitting || m_State == State::QUEUED; });
        if (m_IsQuitting) {
            return;
        }
        m_State = State::RUNNING;
        const Job job = std::move(m_Job);
        lock.unlock();
        Delivery delivery = job(m_Control);
        lock.lock();
        if (m_Control.IsCancelled()) {
            m_State = State::IDLE;
        } else {
            m_Delivery = std::move(delivery);
            m_State = State::DONE;
        }
    }
}

} // namespace Core
