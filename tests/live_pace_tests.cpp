/**
 * @file    live_pace_tests.cpp
 * @brief   Tests for dynamic speed multipliers and incremental wall-clock pacing of live runs.
 */

#include "simulation/live_pace.h"
#include "simulation/run_control.h"
#include "simulation/simulation_service.h"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <chrono>
#include <limits>
#include <thread>

namespace {

using namespace std::chrono_literals;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

double ElapsedSeconds(const std::chrono::steady_clock::time_point from,
                      const std::chrono::steady_clock::time_point to) {
    return std::chrono::duration<double>(to - from).count();
}

} // namespace

TEST_CASE("RunControl clamps live speed multiplier strictly above zero and resets it to 1x", "[live_pace]") {
    Core::RunControl control;
    CHECK(control.GetSpeedMultiplier() == 1.0);

    control.SetSpeedMultiplier(0.25);
    CHECK(control.GetSpeedMultiplier() == 0.25);

    control.SetSpeedMultiplier(0.0);
    CHECK(control.GetSpeedMultiplier() == Core::MIN_SPEED_MULTIPLIER);
    CHECK(control.GetSpeedMultiplier() > 0.0);

    control.SetSpeedMultiplier(-5.0);
    CHECK(control.GetSpeedMultiplier() == Core::MIN_SPEED_MULTIPLIER);

    control.SetSpeedMultiplier(std::numeric_limits<double>::quiet_NaN());
    CHECK(control.GetSpeedMultiplier() == 1.0);

    control.SetSpeedMultiplier(5.0);
    control.Reset();
    CHECK(control.GetSpeedMultiplier() == 1.0);
}

TEST_CASE("SimulationService initializes new runs with its speed multiplier and forwards mid-run updates",
          "[live_pace]") {
    Core::SimulationService service;
    service.SetSpeedMultiplier(0.5);
    CHECK(service.GetSpeedMultiplier() == 0.5);

    std::atomic<double> initial_speed = 0.0;
    std::atomic<double> updated_speed = 0.0;
    std::atomic<bool> ready = false;
    REQUIRE(service.Submit([&](Core::RunControl &control) -> Core::Delivery {
        initial_speed = control.GetSpeedMultiplier();
        ready = true;
        while (!control.IsCancelled()) {
            updated_speed = control.GetSpeedMultiplier();
            std::this_thread::sleep_for(1ms);
        }
        return {};
    }));
    while (!ready) {
        std::this_thread::sleep_for(1ms);
    }
    CHECK(initial_speed == 0.5);

    service.SetSpeedMultiplier(4.0);
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (updated_speed != 4.0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(1ms);
    }
    CHECK(updated_speed == 4.0);
    service.Stop();
    while (service.IsBusy()) {
        service.Poll();
        std::this_thread::sleep_for(1ms);
    }
}

TEST_CASE("Changing speed mid-run advances pacing incrementally without retroactive stalls", "[live_pace]") {
    const auto start = std::chrono::steady_clock::now();
    Core::LivePacer pacer{.Due = start, .LastReached = 0.0};
    constexpr double PACE = 1.0;

    // Chunk 1: 10s of simulated time at 1x -> due advances by 10s
    Core::AdvanceLivePace(pacer, 10.0, PACE, 1.0);
    CHECK_THAT(ElapsedSeconds(start, pacer.Due), WithinAbs(10.0, 1e-6));
    CHECK(pacer.LastReached == 10.0);

    // Drop speed to 0.1x for chunk 2 (only 0.003s of simulated time): due advances by 0.03s, not 90s
    Core::AdvanceLivePace(pacer, 10.003, PACE, 0.1);
    CHECK_THAT(ElapsedSeconds(start, pacer.Due), WithinAbs(10.03, 1e-6));
    CHECK(pacer.LastReached == 10.003);

    // Increase speed to 10x for chunk 3 (0.3s of simulated time): due advances by 0.03s
    Core::AdvanceLivePace(pacer, 10.303, PACE, 10.0);
    CHECK_THAT(ElapsedSeconds(start, pacer.Due), WithinAbs(10.06, 1e-6));
}

TEST_CASE("Live chunk duration scales with the current speed multiplier", "[live_pace]") {
    Core::RunControl control;
    constexpr double PACE = 0.5;
    CHECK_THAT(Core::GetLiveChunkDuration(PACE, nullptr), WithinRel(PACE * Core::LIVE_CHUNK_SECONDS, 1e-12));

    control.SetSpeedMultiplier(2.0);
    CHECK_THAT(Core::GetLiveChunkDuration(PACE, &control), WithinRel(PACE * 2.0 * Core::LIVE_CHUNK_SECONDS, 1e-12));

    control.SetSpeedMultiplier(0.1);
    CHECK_THAT(Core::GetLiveChunkDuration(PACE, &control), WithinRel(PACE * 0.1 * Core::LIVE_CHUNK_SECONDS, 1e-12));
}

TEST_CASE("DropLiveDelay discards delays larger than LIVE_CATCH_UP_LIMIT and keeps smaller ones", "[live_pace]") {
    const auto t0 = std::chrono::steady_clock::now();
    Core::LivePacer pacer{.Due = t0, .LastReached = 0.0};

    // Behind by half of LIVE_CATCH_UP_LIMIT: preserved so small oversleeps average out
    const auto slight_delay =
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(Core::LIVE_CATCH_UP_LIMIT / 2);
    Core::DropLiveDelay(pacer, t0 + slight_delay);
    CHECK(pacer.Due == t0);

    // Behind by twice LIVE_CATCH_UP_LIMIT: dropped to now
    const auto big_delay =
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(Core::LIVE_CATCH_UP_LIMIT * 2);
    const auto now = t0 + big_delay;
    Core::DropLiveDelay(pacer, now);
    CHECK(pacer.Due == now);
}

TEST_CASE("KeepLivePace respects pause, cancel, and mid-run speed changes on a RunControl", "[live_pace]") {
    Core::RunControl control;
    constexpr double PACE = 1.0;
    const auto start = std::chrono::steady_clock::now();
    Core::LivePacer pacer{.Due = start, .LastReached = 0.0};

    // First chunk: 0.04s at 1x
    CHECK(Core::KeepLivePace(pacer, 0.04, PACE, &control));

    // Pause during the next chunk and resume after 80ms; pacer shifts forward by the pause
    control.SetSpeedMultiplier(0.5);
    control.Pause();
    std::jthread resumer([&control] {
        std::this_thread::sleep_for(80ms);
        control.Resume();
    });
    const auto before_second = std::chrono::steady_clock::now();
    CHECK(Core::KeepLivePace(pacer, 0.05, PACE, &control));
    // 0.01s simulated at 0.5x = 20ms, plus 80ms pause -> at least ~90ms
    CHECK(ElapsedSeconds(before_second, std::chrono::steady_clock::now()) >= 0.08);

    // Cancel aborts waiting immediately
    control.Cancel();
    CHECK_FALSE(Core::KeepLivePace(pacer, 10.0, PACE, &control));
}
