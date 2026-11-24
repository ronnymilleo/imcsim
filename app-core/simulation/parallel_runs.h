/**
 * @file    parallel_runs.h
 * @brief   Runs independent circuit simulations across worker threads when the backend supports concurrent runs.
 * @details Used by the truth table, parameter stepping and Monte Carlo runs. On a single-threaded backend such as
 *          ngspice, the items run one after another on the calling thread; on Nodalys, a team of threads up to the
 *          hardware concurrency takes items in order from an atomic counter and stops taking new ones as soon as the
 *          caller cancels or a worker asks to stop early.
 */

#ifndef IMCSIM_PARALLEL_RUNS_H
#define IMCSIM_PARALLEL_RUNS_H

#include "simulation/run_control.h"
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <thread>
#include <vector>

namespace Core {

/**
 * @brief   Chooses how many worker threads to use for a batch of independent runs.
 * @param[in] count          Number of runs in the batch.
 * @param[in] is_concurrent  Whether the active backend supports concurrent runs.
 * @return  1 when the backend is single-threaded or the batch has at most one run; otherwise up to the hardware
 *          concurrency, never exceeding count.
 */
inline std::size_t GetBatchThreadCount(const std::size_t count, const bool is_concurrent) {
    if (!is_concurrent || count <= 1) {
        return 1;
    }
    const unsigned int cores = std::thread::hardware_concurrency();
    const std::size_t limit = cores > 1 ? static_cast<std::size_t>(cores) : std::size_t{2};
    return std::min(count, limit);
}

/**
 * @brief   Runs a batch sequentially on the calling thread, reporting progress before each item starts.
 */
template <typename RunItem>
void RunSerialBatch(const std::size_t count,
                    RunControl *control,
                    const RunItem &run_item,
                    const std::size_t progress_offset,
                    const std::size_t total) {
    for (std::size_t index = 0; index < count; ++index) {
        if (control != nullptr) {
            if (control->IsCancelled()) {
                return;
            }
            control->SetProgress(static_cast<double>(index + progress_offset) / static_cast<double>(total));
        }
        if (!run_item(index)) {
            return;
        }
    }
}

/**
 * @brief   Runs a batch concurrently across a team of worker threads pulling indices from an atomic counter.
 */
template <typename RunItem>
void RunConcurrentBatch(const std::size_t count,
                        const std::size_t thread_count,
                        RunControl *control,
                        const RunItem &run_item,
                        const std::size_t progress_offset,
                        const std::size_t total) {
    std::atomic<std::size_t> next_index = 0;
    std::atomic<std::size_t> finished_count = progress_offset;
    std::atomic<bool> should_stop = false;

    const auto worker = [&]() {
        while (!should_stop.load(std::memory_order_relaxed)) {
            if (control != nullptr && control->IsCancelled()) {
                return;
            }
            const std::size_t index = next_index.fetch_add(1, std::memory_order_relaxed);
            if (index >= count) {
                return;
            }
            if (!run_item(index)) {
                should_stop.store(true, std::memory_order_relaxed);
                return;
            }
            const std::size_t done = finished_count.fetch_add(1, std::memory_order_relaxed) + 1;
            if (control != nullptr && !control->IsCancelled()) {
                control->SetProgress(static_cast<double>(done) / static_cast<double>(total));
            }
        }
    };

    std::vector<std::jthread> workers;
    workers.reserve(thread_count - 1);
    for (std::size_t thread_index = 1; thread_index < thread_count; ++thread_index) {
        workers.emplace_back(worker);
    }
    worker();
}

/**
 * @brief   Runs a batch of independent indexed items, in parallel when the backend supports concurrent runs.
 * @details Each call `run_item(index)` returns true to keep going or false to stop scheduling further items (for
 *          example, when a step fails). On a single thread, progress is reported as `(index + progress_offset) /
 *          progress_total` before each item starts; on multiple threads, as `(done + progress_offset) /
 *          progress_total` as items complete. No new items are started once `control` is cancelled.
 * @param[in]     count            Number of items (`0` to `count - 1`).
 * @param[in]     is_concurrent    Whether the backend allows concurrent runs.
 * @param[in,out] control          Optional run control for stop requests and progress reporting, or null.
 * @param[in]     run_item         Callable `bool(std::size_t index)` executed once per scheduled item.
 * @param[in]     progress_offset  Completed items before this batch starts (for example, 1 after a nominal run).
 * @param[in]     progress_total   Total items for the progress denominator, or 0 to use `count + progress_offset`.
 */
template <typename RunItem>
void RunBatch(const std::size_t count,
              const bool is_concurrent,
              RunControl *control,
              const RunItem &run_item,
              const std::size_t progress_offset = 0,
              const std::size_t progress_total = 0) {
    if (count == 0) {
        return;
    }
    const std::size_t total = progress_total > 0 ? progress_total : count + progress_offset;
    const std::size_t thread_count = GetBatchThreadCount(count, is_concurrent);
    if (thread_count <= 1) {
        RunSerialBatch(count, control, run_item, progress_offset, total);
        return;
    }
    RunConcurrentBatch(count, thread_count, control, run_item, progress_offset, total);
}

} // namespace Core

#endif // IMCSIM_PARALLEL_RUNS_H
