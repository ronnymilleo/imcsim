/**
 * @file    simulation_service.h
 * @brief   Runs simulations on a worker thread, so the interface keeps drawing while they take their time.
 */

#ifndef IMCSIM_SIMULATION_SERVICE_H
#define IMCSIM_SIMULATION_SERVICE_H

#include "run_control.h"
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace Core {

/**
 * @brief   Work for the worker thread. It checks the RunControl between its steps and returns the delivery of its
 *          results, or an empty one when it has nothing to hand over. It may post deliveries on the way, such as the
 *          points of a transient made so far.
 */
using Job = std::function<Delivery(RunControl &)>;

/**
 * @class   SimulationService
 * @brief   One worker thread that runs one job at a time and hands the result back to the thread that polls.
 * @details ngspice is a single global instance, so the worker is the only thread that runs a simulation, and a job
 *          works on copies of what it needs (see CircuitCopy), never on what the interface edits. The interface
 *          submits a job, calls Poll() every frame and runs the deliveries that come back on its own thread, so
 *          nothing it owns is touched from the worker. Stop() ends a job early and keeps what it has made, which a job
 *          that can hand over a partial result does; Cancel() ends it and drops everything. SendChanges() passes
 * changes of the circuit to a live transient in progress, and Pause() holds it where it is until Resume().
 */
class SimulationService {
public:
    SimulationService() = default;
    SimulationService(const SimulationService &) = delete;
    SimulationService &operator=(const SimulationService &) = delete;
    ~SimulationService();

    // Jobs
    bool Submit(Job job);
    void Stop();
    void Cancel();
    bool Poll();
    void SendChanges(const std::vector<LiveChange> &changes);
    void Pause();
    void Resume();

    // State
    bool IsBusy() const;
    bool IsCancelling() const;
    bool IsPaused() const;
    double GetProgress() const;

private:
    enum class State {
        IDLE,
        QUEUED,
        RUNNING,
        DONE
    };

    mutable std::mutex m_Mutex;
    std::condition_variable m_Wake;
    State m_State = State::IDLE;
    bool m_IsQuitting = false;
    Job m_Job;
    Delivery m_Delivery;
    RunControl m_Control;
    // Started with the first job, so a program that never simulates has no thread
    std::thread m_Worker;

    void Work();
};

} // namespace Core

#endif // IMCSIM_SIMULATION_SERVICE_H
