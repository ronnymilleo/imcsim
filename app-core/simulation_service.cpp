/**
 * @file    simulation_service.cpp
 * @brief   Runs simulations on a worker thread, so the interface keeps drawing while they take their time.
 */

#include "simulation_service.h"

#include <utility>
#include <vector>

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
 * @brief   Asks the job to stop at the end of its current step and to hand over what it has made.
 * @note    A job checks between its steps, so a long single step ends first; IsBusy() tells when the worker is free
 *          again. A job that cannot hand over a partial result delivers nothing.
 */
void SimulationService::Stop() {
    const std::lock_guard lock(m_Mutex);
    if (m_State == State::QUEUED || m_State == State::RUNNING) {
        m_Control.Cancel();
    }
}

/**
 * @brief   Asks the job to stop and drops everything it made or will make.
 * @note    The same wait as Stop(). A result that is done but not yet polled is dropped at once.
 */
void SimulationService::Cancel() {
    const std::lock_guard lock(m_Mutex);
    if (m_State == State::DONE) {
        m_Delivery = nullptr;
        m_State = State::IDLE;
    } else if (m_State != State::IDLE) {
        m_Control.Discard();
    }
}

/**
 * @brief   Runs what the job has posted, and the delivery of a finished job, on the calling thread.
 * @return  True when a delivery ran; false while the job runs and has posted nothing, when none was submitted, and
 *          when the job delivered nothing.
 * @note    Call it every frame from the thread that owns what the deliveries change. What a discarded job posted is
 *          dropped.
 */
bool SimulationService::Poll() {
    bool has_delivered = false;
    std::vector<Delivery> posted = m_Control.TakePosted();
    if (!m_Control.IsDiscarded()) {
        for (const Delivery &delivery : posted) {
            delivery();
            has_delivered = true;
        }
    }
    Delivery delivery;
    {
        const std::lock_guard lock(m_Mutex);
        if (m_State != State::DONE) {
            return has_delivered;
        }
        delivery = std::move(m_Delivery);
        m_Delivery = nullptr;
        m_State = State::IDLE;
    }
    if (!delivery) {
        return has_delivered;
    }
    delivery();
    return true;
}

/**
 * @brief   Passes changes of the circuit to the job in progress, which a live transient applies before its next chunk.
 * @param[in] changes  New values of netlist elements, in the order they happened.
 * @note    Without a job queued or running the changes are dropped, since the next job starts from a new copy of the
 *          circuit, which has them.
 */
void SimulationService::SendChanges(const std::vector<LiveChange> &changes) {
    const std::lock_guard lock(m_Mutex);
    if (m_State == State::QUEUED || m_State == State::RUNNING) {
        m_Control.SendChanges(changes);
    }
}

/**
 * @brief   Holds a live transient in progress after its current chunk, with ngspice and its points kept for Resume().
 * @note    Without a job queued or running nothing happens; Stop() and Cancel() still end a paused job.
 */
void SimulationService::Pause() {
    const std::lock_guard lock(m_Mutex);
    if (m_State == State::QUEUED || m_State == State::RUNNING) {
        m_Control.Pause();
    }
}

/**
 * @brief   Lets a paused live transient go on from where it held.
 */
void SimulationService::Resume() {
    m_Control.Resume();
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
 * @brief   Tells whether the job in progress is held by Pause().
 * @return  True from Pause() until Resume() or the end of the job.
 */
bool SimulationService::IsPaused() const {
    const std::lock_guard lock(m_Mutex);
    return (m_State == State::QUEUED || m_State == State::RUNNING) && m_Control.IsPaused();
}

/**
 * @brief   Returns how much of the job in progress is done.
 * @return  A fraction from 0 to 1; 0 when no job runs.
 */
double SimulationService::GetProgress() const {
    return m_Control.GetProgress();
}

// The worker waits for a job, runs it without holding the lock, and leaves its delivery for Poll(). A job that was
// discarded delivers nothing
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
        if (m_Control.IsDiscarded()) {
            m_State = State::IDLE;
        } else {
            m_Delivery = std::move(delivery);
            m_State = State::DONE;
        }
    }
}

} // namespace Core
