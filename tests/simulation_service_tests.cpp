/**
 * @file    simulation_service_tests.cpp
 * @brief   Tests for the worker thread that runs simulations while the interface keeps drawing.
 */

#include "simulation_service.h"

#include "circuit_copy.h"
#include "components/capacitor.h"
#include "components/ground.h"
#include "components/resistor.h"
#include "components/voltage_source.h"
#include "simulator.h"
#include <algorithm>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;

// The longest a test waits for the worker before it calls the job lost
constexpr auto WAIT_LIMIT = 10s;

// Polls until the service delivers, or the limit passes
bool PollUntilDelivered(Core::SimulationService &service) {
    const auto deadline = std::chrono::steady_clock::now() + WAIT_LIMIT;
    while (std::chrono::steady_clock::now() < deadline) {
        if (service.Poll()) {
            return true;
        }
        std::this_thread::sleep_for(1ms);
    }
    return false;
}

bool WaitUntilIdle(const Core::SimulationService &service) {
    const auto deadline = std::chrono::steady_clock::now() + WAIT_LIMIT;
    while (std::chrono::steady_clock::now() < deadline) {
        if (!service.IsBusy()) {
            return true;
        }
        std::this_thread::sleep_for(1ms);
    }
    return false;
}

// Runs until the control says stop, reporting a little progress on the way
Core::Job EndlessJob(std::atomic<bool> &has_started) {
    return [&has_started](Core::RunControl &control) -> Core::Delivery {
        has_started = true;
        control.SetProgress(0.25);
        while (!control.IsCancelled()) {
            std::this_thread::sleep_for(1ms);
        }
        return [] {};
    };
}

} // namespace

TEST_CASE("A job runs on the worker and its delivery runs on the thread that polls", "[simulation_service]") {
    Core::SimulationService service;
    std::optional<std::thread::id> job_thread;
    std::optional<std::thread::id> delivery_thread;
    REQUIRE(service.Submit([&](Core::RunControl &) -> Core::Delivery {
        job_thread = std::this_thread::get_id();
        return [&] { delivery_thread = std::this_thread::get_id(); };
    }));
    REQUIRE(PollUntilDelivered(service));

    REQUIRE(job_thread.has_value());
    CHECK(*job_thread != std::this_thread::get_id());
    CHECK(delivery_thread == std::this_thread::get_id());
    CHECK_FALSE(service.IsBusy());
}

TEST_CASE("A second job is refused until the first one has been polled", "[simulation_service]") {
    Core::SimulationService service;
    CHECK(service.Submit([](Core::RunControl &) -> Core::Delivery { return [] {}; }));
    // Busy while it runs and while its result waits for the poll
    CHECK(service.IsBusy());
    CHECK_FALSE(service.Submit([](Core::RunControl &) -> Core::Delivery { return [] {}; }));
    REQUIRE(PollUntilDelivered(service));
    CHECK(service.Submit([](Core::RunControl &) -> Core::Delivery { return [] {}; }));
    CHECK(PollUntilDelivered(service));
}

TEST_CASE("Cancelling a job stops it and drops its result", "[simulation_service]") {
    Core::SimulationService service;
    std::atomic<bool> has_started = false;
    bool has_delivered = false;
    REQUIRE(service.Submit([&](Core::RunControl &control) -> Core::Delivery {
        has_started = true;
        while (!control.IsCancelled()) {
            std::this_thread::sleep_for(1ms);
        }
        return [&has_delivered] { has_delivered = true; };
    }));
    while (!has_started) {
        std::this_thread::sleep_for(1ms);
    }
    CHECK_FALSE(service.IsCancelling());
    service.Cancel();
    CHECK(service.IsCancelling());

    REQUIRE(WaitUntilIdle(service));
    CHECK_FALSE(service.IsCancelling());
    CHECK_FALSE(service.Poll());
    CHECK_FALSE(has_delivered);
    // The worker takes the next job as if nothing had happened
    CHECK(service.Submit([](Core::RunControl &) -> Core::Delivery { return [] {}; }));
    CHECK(PollUntilDelivered(service));
}

TEST_CASE("Cancelling after the job has returned drops its result", "[simulation_service]") {
    Core::SimulationService service;
    std::atomic<bool> has_returned = false;
    bool has_delivered = false;
    REQUIRE(service.Submit([&](Core::RunControl &) -> Core::Delivery {
        has_returned = true;
        return [&has_delivered] { has_delivered = true; };
    }));
    // The result may be done and waiting for the poll or about to be; either way the circuit changed, so it is stale
    while (!has_returned) {
        std::this_thread::sleep_for(1ms);
    }
    service.Cancel();
    REQUIRE(WaitUntilIdle(service));
    CHECK_FALSE(service.Poll());
    CHECK_FALSE(has_delivered);
}

TEST_CASE("The progress of the job in progress can be read from the other thread", "[simulation_service]") {
    Core::SimulationService service;
    std::atomic<bool> has_started = false;
    REQUIRE(service.Submit(EndlessJob(has_started)));
    while (!has_started) {
        std::this_thread::sleep_for(1ms);
    }
    CHECK(service.GetProgress() == 0.25);
    service.Cancel();
    REQUIRE(WaitUntilIdle(service));
}

TEST_CASE("Destroying the service stops a job that is running and waits for it", "[simulation_service]") {
    std::atomic<bool> has_started = false;
    std::atomic<bool> has_ended = false;
    {
        Core::SimulationService service;
        REQUIRE(service.Submit([&](Core::RunControl &control) -> Core::Delivery {
            has_started = true;
            while (!control.IsCancelled()) {
                std::this_thread::sleep_for(1ms);
            }
            has_ended = true;
            return {};
        }));
        while (!has_started) {
            std::this_thread::sleep_for(1ms);
        }
    }
    CHECK(has_ended);
}

TEST_CASE("A copy of the circuit simulates on the worker to the result of the same run on this thread",
          "[simulation_service]") {
    Core::VoltageSource source;
    source.SetName("V1");
    source.SetValue(5.0);
    Core::Resistor resistor;
    resistor.SetName("R1");
    resistor.SetValue(1e3);
    Core::Capacitor capacitor;
    capacitor.SetName("C1");
    capacitor.SetValue(1e-6);
    const Core::Ground ground;
    auto circuit = std::make_unique<Core::Circuit>();
    circuit->Add(source, {1, 0});
    circuit->Add(resistor, {1, 2});
    circuit->Add(capacitor, {2, 0});
    circuit->Add(ground, {0});
    const Core::TransientSettings settings{.StopTime = 5e-3, .TimeStep = 10e-6};
    const Core::TransientRun expected = Core::RunTransient(*circuit, settings);
    REQUIRE(expected.Result.has_value());

    Core::SimulationService service;
    auto copy = std::make_shared<const Core::CircuitCopy>(*circuit);
    std::optional<Core::TransientRun> delivered;
    REQUIRE(service.Submit([copy, settings, &delivered](Core::RunControl &) -> Core::Delivery {
        auto run = std::make_shared<Core::TransientRun>(Core::RunTransient(copy->GetCircuit(), settings));
        return [run, &delivered] { delivered = std::move(*run); };
    }));
    // The user edits and deletes the circuit while the worker simulates its copy
    resistor.SetValue(1.0);
    circuit.reset();
    REQUIRE(PollUntilDelivered(service));

    REQUIRE(delivered.has_value());
    REQUIRE(delivered->Result.has_value());
    CHECK(delivered->Result->Times == expected.Result->Times);
    CHECK(delivered->Result->NodeVoltages == expected.Result->NodeVoltages);
}

TEST_CASE("A stop keeps what the job hands over, where a cancel drops it", "[simulation_service]") {
    Core::SimulationService service;
    std::atomic<bool> has_started = false;
    bool has_delivered = false;
    const auto make_job = [&] {
        return [&](Core::RunControl &control) -> Core::Delivery {
            has_started = true;
            while (!control.IsCancelled()) {
                std::this_thread::sleep_for(1ms);
            }
            return [&has_delivered] { has_delivered = true; };
        };
    };

    REQUIRE(service.Submit(make_job()));
    while (!has_started) {
        std::this_thread::sleep_for(1ms);
    }
    service.Stop();
    CHECK(service.IsCancelling());
    REQUIRE(PollUntilDelivered(service));
    CHECK(has_delivered);

    has_started = false;
    has_delivered = false;
    REQUIRE(service.Submit(make_job()));
    while (!has_started) {
        std::this_thread::sleep_for(1ms);
    }
    service.Cancel();
    REQUIRE(WaitUntilIdle(service));
    CHECK_FALSE(service.Poll());
    CHECK_FALSE(has_delivered);
}

TEST_CASE("What a job posts while it runs reaches the polling thread, in order", "[simulation_service]") {
    Core::SimulationService service;
    std::atomic<bool> may_finish = false;
    std::vector<int> received;
    std::vector<std::thread::id> threads;
    REQUIRE(service.Submit([&](Core::RunControl &control) -> Core::Delivery {
        for (int chunk = 1; chunk <= 3; ++chunk) {
            control.Post([&received, &threads, chunk] {
                received.push_back(chunk);
                threads.push_back(std::this_thread::get_id());
            });
        }
        while (!may_finish) {
            std::this_thread::sleep_for(1ms);
        }
        return {};
    }));
    const auto deadline = std::chrono::steady_clock::now() + WAIT_LIMIT;
    while (received.size() < 3 && std::chrono::steady_clock::now() < deadline) {
        service.Poll();
        std::this_thread::sleep_for(1ms);
    }
    // The job still runs, and its chunks are already here
    CHECK(service.IsBusy());
    CHECK(received == std::vector<int>{1, 2, 3});
    CHECK(std::ranges::all_of(threads, [](const std::thread::id id) { return id == std::this_thread::get_id(); }));

    may_finish = true;
    while (service.IsBusy()) {
        service.Poll();
        std::this_thread::sleep_for(1ms);
    }
    CHECK_FALSE(service.IsBusy());
}

TEST_CASE("What a discarded job posted is dropped", "[simulation_service]") {
    Core::SimulationService service;
    std::atomic<bool> has_posted = false;
    std::atomic<bool> may_finish = false;
    bool has_delivered = false;
    REQUIRE(service.Submit([&](Core::RunControl &control) -> Core::Delivery {
        control.Post([&has_delivered] { has_delivered = true; });
        has_posted = true;
        while (!may_finish) {
            std::this_thread::sleep_for(1ms);
        }
        return {};
    }));
    while (!has_posted) {
        std::this_thread::sleep_for(1ms);
    }
    service.Cancel();
    CHECK_FALSE(service.Poll());
    may_finish = true;
    REQUIRE(WaitUntilIdle(service));
    CHECK_FALSE(has_delivered);
}
