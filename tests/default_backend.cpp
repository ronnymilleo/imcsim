/**
 * @file    default_backend.cpp
 * @brief   Makes ngspice the default backend of the tests, before any test runs.
 */

#include "backends/ngspice/ngspice_backend.h"
#include <catch2/catch_test_run_info.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

namespace {

/// The library registers no default backend itself, as the program does at startup.
class DefaultBackendListener : public Catch::EventListenerBase {
public:
    using Catch::EventListenerBase::EventListenerBase;

    void testRunStarting(const Catch::TestRunInfo & /*test_run_info*/) override { Core::UseNgspiceByDefault(); }
};

} // namespace

CATCH_REGISTER_LISTENER(DefaultBackendListener)
