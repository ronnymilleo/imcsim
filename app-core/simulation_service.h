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

namespace Core {

/**
 * @brief   What a job hands back for the calling thread to run once the job is done, such as storing its results.
 */
using Delivery = std::function<void()>;

/**
 * @brief   Work for the worker thread. It checks the RunControl between its steps and returns the delivery of its
 *          results, or an empty one when it was stopped.
 */
using Job = std::function<Delivery(RunControl &)>;

/**
 * @class   SimulationService
 * @brief   One worker thread that runs one job at a time and hands the result back to the thread that polls.
 * @details ngspice is a single global instance, so the worker is the only thread that runs a simulation, and a job
 *          works on copies of what it needs (see CircuitCopy), never on what the interface edits. The interface
 *          submits a job, calls Poll() every frame and runs the delivery that comes back on its own thread, so
 *          nothing it owns is touched from the worker.
 */
class SimulationService {
public:
    SimulationService() = default;
    SimulationService(const SimulationService &) = delete;
    SimulationService &operator=(const SimulationService &) = delete;
    ~SimulationService();

    // Jobs
    bool Submit(Job job);
    void Cancel();
    bool Poll();

    // State
    bool IsBusy() const;
    bool IsCancelling() const;
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
